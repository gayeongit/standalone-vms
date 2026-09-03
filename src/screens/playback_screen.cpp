#include "playback_screen.h"
#include "app_state.h"
#include "common_ui.h"
#include "common_widgets.h"
#include "event_service.h"
#include "event_ui_helpers.h"
#include "playback_screen_helpers.h"
#include "playback_service.h"
#include "popup_manager.h"
#include "stream_player.h"

#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSlider>
#include <QSaveFile>
#include <QStackedWidget>
#include <QTimer>
#include <QTimeEdit>
#include <QTimeZone>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

using namespace PlaybackScreenHelpers;

namespace {
class PassiveTimelineSlider final : public QSlider
{
public:
    explicit PassiveTimelineSlider(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSlider(orientation, parent)
    {
    }

    void setUserSeekEnabled(bool enabled)
    {
        m_userSeekEnabled = enabled;
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (!m_userSeekEnabled) {
            event->ignore();
            return;
        }
        QSlider::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_userSeekEnabled) {
            event->ignore();
            return;
        }
        QSlider::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!m_userSeekEnabled) {
            event->ignore();
            return;
        }
        QSlider::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (!m_userSeekEnabled) {
            event->ignore();
            return;
        }
        QSlider::wheelEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (!m_userSeekEnabled) {
            event->ignore();
            return;
        }
        QSlider::keyPressEvent(event);
    }

private:
    bool m_userSeekEnabled = false;
};
} // namespace

PlaybackScreen::PlaybackScreen(QWidget *parent)
    : QWidget(parent)
{
    m_player = new StreamPlayer(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    TopbarWidget::Config topbarConfig;
    topbarConfig.showNotification = true;
    topbarConfig.showSettings = true;
    topbarConfig.showLogout = true;
    auto *topbar = new TopbarWidget(topbarConfig, this);
    applyNotificationUnreadState(topbar, EventUiHelpers::currentUnreadCount() > 0, EventUiHelpers::currentUnreadCount());
    if (auto *service = EventUiHelpers::eventService()) {
        connect(service, &EventService::unreadChanged, this, [this, topbar](int count) {
            m_pendingSnackbarEvents = count;
            applyNotificationUnreadState(topbar, count > 0, count);
        });
    }

    auto *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    SidebarWidget::Config sidebarConfig;
    sidebarConfig.primaryBottomText = "스냅샷";
    sidebarConfig.secondaryBottomText = "내보내기";
    auto *sidebar = new SidebarWidget(sidebarConfig, this);
    auto *channelTab = sidebar->channelTab();
    auto *playbackTab = sidebar->playbackTab();
    auto *channelPage = sidebar->channelPage();
    auto *channelTree = sidebar->cctvTree();
    auto *ugvTree = sidebar->ugvTree();
    auto *playbackTree = sidebar->playbackTree();
    m_playbackTree = playbackTree;
    auto *treeStack = sidebar->treeStack();
    auto *snapshotButton = sidebar->primaryBottomButton();
    auto *exportButton = sidebar->secondaryBottomButton();
    m_exportButton = exportButton;
    auto *actionStatusLabel = sidebar->actionStatusLabel();
    m_actionStatusLabel = actionStatusLabel;

    sidebar->populateChannelTree();
    treeStack->setCurrentWidget(playbackTree);
    setSidebarTabState(channelTab, playbackTab, false);

    auto *backButton = new QPushButton("← 멀티뷰로 돌아가기", sidebar);
    backButton->setObjectName("backLink");
    backButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    backButton->setFixedHeight(22);
    sidebar->controlsLayout()->addSpacing(12);
    sidebar->controlsLayout()->addWidget(backButton);
    sidebar->controlsLayout()->addSpacing(14);

    auto *content = new QFrame(this);
    content->setObjectName("centerPanel");
    content->setMinimumWidth(1200);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_playbackFileLabel = new QLabel("트리에서 재생할 항목을 선택하세요.", content);
    m_playbackFileLabel->setObjectName("playbackFileLabel");
    m_playbackFileLabel->hide();

    m_videoHost = new QFrame(content);
    m_videoHost->setObjectName("videoCell");
    m_videoHost->setMinimumHeight(420);
    m_videoHost->setAttribute(Qt::WA_NativeWindow);

    auto *timeline = new QFrame(content);
    timeline->setObjectName("timelinePanel");
    timeline->setFixedHeight(64);

    auto *timelineLayout = new QHBoxLayout(timeline);
    timelineLayout->setContentsMargins(10, 10, 10, 8);
    timelineLayout->setSpacing(8);

    m_playPauseButton = new QPushButton(QStringLiteral(">"), timeline);
    m_playPauseButton->setObjectName("playbackPlayButton");
    m_playPauseButton->setFixedSize(30, 24);

    m_speedButton = new QPushButton("1x", timeline);
    m_speedButton->setFixedWidth(54);
    m_speedButton->setEnabled(false);
    m_speedButton->hide();

    m_currentTimeLabel = new QLabel("00:00:00", timeline);
    m_currentTimeLabel->setObjectName("playbackTimeLabel");
    auto *timelineSlider = new PassiveTimelineSlider(Qt::Horizontal, timeline);
    timelineSlider->setUserSeekEnabled(false);
    m_timelineSlider = timelineSlider;
    m_timelineSlider->setObjectName("playbackTimelineSlider");
    m_timelineSlider->setRange(0, 1000);
    m_timelineSlider->setValue(0);
    m_timelineSlider->setEnabled(false);
    m_timelineSlider->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_timelineHandle = new QFrame(timeline);
    m_timelineHandle->setObjectName("playbackTimelineHandle");
    m_timelineHandle->setFixedSize(12, 12);
    m_timelineHandleCore = new QFrame(m_timelineHandle);
    m_timelineHandleCore->setObjectName("playbackTimelineHandleCore");
    m_timelineHandleCore->setFixedSize(4, 4);
    m_timelineHandleCore->move(
        (m_timelineHandle->width() - m_timelineHandleCore->width()) / 2,
        (m_timelineHandle->height() - m_timelineHandleCore->height()) / 2);
    m_timelineHandle->hide();
    m_totalTimeLabel = new QLabel("00:00:00", timeline);
    m_totalTimeLabel->setObjectName("playbackTimeLabel");

    timelineLayout->addWidget(m_playPauseButton);
    timelineLayout->addWidget(m_currentTimeLabel);
    timelineLayout->addWidget(m_timelineSlider, 1);
    timelineLayout->addWidget(m_totalTimeLabel);

    contentLayout->addWidget(m_videoHost, 1);
    contentLayout->addWidget(timeline);

    auto *sidebarSeparator = new QFrame(this);
    sidebarSeparator->setObjectName("sidebarSeparator");
    sidebarSeparator->setFixedWidth(1);

    body->addWidget(sidebar, 240);
    body->addWidget(sidebarSeparator);
    body->addWidget(content, 1680);

    root->addWidget(topbar);
    root->addLayout(body, 1);

    m_timelineTimer = new QTimer(this);
    m_timelineTimer->setInterval(200);
    connect(m_timelineTimer, &QTimer::timeout, this, &PlaybackScreen::refreshTimelineUi);
    m_timelineTimer->start();

    m_snackbarHideTimer = new QTimer(this);
    m_snackbarHideTimer->setSingleShot(true);
    m_snackbarHideTimer->setInterval(3000);
    connect(m_snackbarHideTimer, &QTimer::timeout, this, &PlaybackScreen::clearPlaybackSnackbar);

    m_exportPollTimer = new QTimer(this);
    m_exportPollTimer->setInterval(1000);
    connect(m_exportPollTimer, &QTimer::timeout, this, &PlaybackScreen::pollExportStatus);

    m_exportDownloadManager = new QNetworkAccessManager(this);

    m_playbackSnackbarFrame = new QFrame(this);
    m_playbackSnackbarFrame->setObjectName("ugvSnackbar");
    auto *snackbarLayout = new QHBoxLayout(m_playbackSnackbarFrame);
    snackbarLayout->setContentsMargins(10, 6, 8, 6);
    snackbarLayout->setSpacing(8);
    m_playbackSnackbarLabel = new QLabel("이전영상 이벤트", m_playbackSnackbarFrame);
    auto *snackbarCloseButton = new QPushButton("X", m_playbackSnackbarFrame);
    snackbarCloseButton->setObjectName("ugvSnackbarClose");
    snackbarCloseButton->setFixedSize(22, 22);
    snackbarLayout->addWidget(m_playbackSnackbarLabel, 1);
    snackbarLayout->addWidget(snackbarCloseButton);
    m_playbackSnackbarFrame->hide();

    connect(snackbarCloseButton, &QPushButton::clicked, this, &PlaybackScreen::clearPlaybackSnackbar);
    connect(topbar, &TopbarWidget::settingsClicked, this, &PlaybackScreen::settingsRequested, Qt::UniqueConnection);
    connect(topbar, &TopbarWidget::logoutClicked, this, &PlaybackScreen::logoutRequested, Qt::UniqueConnection);
    connect(topbar, &TopbarWidget::notificationCenterClicked, this, [this, topbar]() {
        if (auto *service = EventUiHelpers::eventService()) {
            service->markAllRead();
        }
        m_pendingSnackbarEvents = EventUiHelpers::currentUnreadCount();
        openNotificationCenterDialog(this, topbar);
    });
    connect(backButton, &QPushButton::clicked, this, &PlaybackScreen::backToMainRequested);

    connect(channelTab, &QPushButton::clicked, this, [channelTab, playbackTab, treeStack, channelPage]() {
        treeStack->setCurrentWidget(channelPage);
        setSidebarTabState(channelTab, playbackTab, true);
    });
    connect(playbackTab, &QPushButton::clicked, this, [channelTab, playbackTab, treeStack, playbackTree]() {
        treeStack->setCurrentWidget(playbackTree);
        setSidebarTabState(channelTab, playbackTab, false);
    });
    channelTab->installEventFilter(new DoubleClickFilter([this]() {
        emit backToMainRequested();
    }, channelTab));
    playbackTab->installEventFilter(new DoubleClickFilter([]() {}, playbackTab));

    connect(channelTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (!item || item->childCount() > 0) {
            return;
        }
        const QString type = item->data(0, Qt::UserRole + 3).toString().trimmed().toUpper();
        const QString selected = item->data(0, Qt::UserRole + 2).toString().trimmed().isEmpty()
            ? item->text(0).trimmed()
            : item->data(0, Qt::UserRole + 2).toString().trimmed();
        if (selected.isEmpty() || (!type.isEmpty() && type != QStringLiteral("CCTV"))) {
            return;
        }
        int cctvChannelId = item->data(0, Qt::UserRole).toInt();
        if (cctvChannelId <= 0) {
            cctvChannelId = selectedChannelIdForDisplayNameExact(selected, QStringLiteral("CCTV"));
        }
        if (cctvChannelId <= 0) {
            return;
        }
        auto &state = AppState::instance();
        state.activeChannel = selected;
        state.activeCctvChannelId = cctvChannelId;
        state.activeUgvChannelId = -1;
        state.activeUgvGatewayId = -1;
        emit openCctvRequested();
    });
    if (ugvTree) {
        connect(ugvTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
            if (!item || item->childCount() > 0) {
                return;
            }
            const QString type = item->data(0, Qt::UserRole + 3).toString().trimmed().toUpper();
            if (type == QStringLiteral("UGV")) {
                PopupManager::showInfo(this, "UGV", "UGV 진입은 이벤트 상세의 '출동 시작'으로만 가능합니다.");
            }
        });
    }
    connect(playbackTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (!item || item->childCount() > 0) {
            return;
        }
        const int channelId = item->data(0, Qt::UserRole).toInt();
        const QString date = item->data(0, Qt::UserRole + 1).toString().trimmed();
        const QString channelName = item->data(0, Qt::UserRole + 2).toString().trimmed();
        if (channelId < 0 || date.isEmpty() || channelName.isEmpty()) {
            return;
        }
        startPlaybackForChannel(channelId, channelName, date);
    });

    connect(snapshotButton, &QPushButton::clicked, this, [this, snapshotButton]() {
        QString savedPath;
        QString errorMessage;
        if (!saveSnapshotPngFromPlayer(m_player, "vms_playback_snapshot", &savedPath, &errorMessage)) {
            if (errorMessage.contains("설정에서 경로")) {
                const bool openSettings = PopupManager::confirmWithLabels(
                    this,
                    "스냅샷",
                    errorMessage,
                    "설정으로 이동",
                    "닫기");
                if (openSettings) {
                    emit settingsRequested();
                }
                return;
            }
            PopupManager::showInfo(this, "스냅샷", errorMessage);
            return;
        }
        Q_UNUSED(savedPath);
        snapshotButton->setProperty("feedbackState", QStringLiteral("done"));
        snapshotButton->style()->unpolish(snapshotButton);
        snapshotButton->style()->polish(snapshotButton);
        snapshotButton->setText(QStringLiteral("저장완료"));
        QTimer::singleShot(1800, snapshotButton, [snapshotButton]() {
            if (snapshotButton) {
                snapshotButton->setProperty("feedbackState", QVariant());
                snapshotButton->style()->unpolish(snapshotButton);
                snapshotButton->style()->polish(snapshotButton);
                snapshotButton->setText(QStringLiteral("스냅샷"));
            }
        });
    });
    connect(exportButton, &QPushButton::clicked, this, &PlaybackScreen::openExportDialog);

    connect(m_playPauseButton, &QPushButton::clicked, this, [this]() {
        if (m_currentPlaybackSource.isEmpty()) {
            PopupManager::showInfo(this, "이전영상", "먼저 재생할 항목을 선택하세요.");
            return;
        }
        const StreamStatus st = m_player->status();
        if (st == StreamStatus::Idle || st == StreamStatus::Error) {
            if (m_currentTimelinePositionMs >= 0) {
                m_playbackStartTimelinePositionMs = m_currentTimelinePositionMs;
            } else if (m_selectedTimelinePositionMs >= 0) {
                m_playbackStartTimelinePositionMs = m_selectedTimelinePositionMs;
            } else {
                m_playbackStartTimelinePositionMs = m_timelineSpanStartMs;
            }
            m_playbackStartWallclockMs = -1;
            m_player->setSource(m_currentPlaybackSource);
            m_player->start();
            m_playPauseButton->setText("||");
            return;
        }
        m_player->setPaused(!m_player->isPaused());
        if (m_player->isPaused()) {
            if (m_currentTimelinePositionMs >= 0) {
                m_playbackStartTimelinePositionMs = m_currentTimelinePositionMs;
            }
            m_playbackStartWallclockMs = -1;
        } else {
            if (m_currentTimelinePositionMs >= 0) {
                m_playbackStartTimelinePositionMs = m_currentTimelinePositionMs;
            } else if (m_selectedTimelinePositionMs >= 0) {
                m_playbackStartTimelinePositionMs = m_selectedTimelinePositionMs;
            } else {
                m_playbackStartTimelinePositionMs = m_timelineSpanStartMs;
            }
            m_playbackStartWallclockMs = QDateTime::currentMSecsSinceEpoch();
        }
        m_playPauseButton->setText(m_player->isPaused() ? ">" : "||");
    });

    // PB-2 policy: timeline slider is display-only, and seeking is done via event markers.

    connect(m_player, &StreamPlayer::statusChanged, this, [this](StreamStatus status) {
        applyPlaybackCapabilities();
        if (status == StreamStatus::Playing) {
            if (!m_player->isPaused() && m_playbackStartWallclockMs < 0) {
                m_playbackStartWallclockMs = QDateTime::currentMSecsSinceEpoch();
            }
            m_playPauseButton->setText(m_player->isPaused() ? ">" : "||");
            return;
        }
        m_playbackStartWallclockMs = -1;
        m_playPauseButton->setText(">");
    });

    connect(m_player, &StreamPlayer::eosReached, this, [this]() {
        applyPlaybackCapabilities();
        m_playbackStartWallclockMs = -1;
        m_playPauseButton->setText(">");
    });
    connect(m_player, &StreamPlayer::errorOccurred, this, [this](const QString &message) {
        m_playbackStartWallclockMs = -1;
        const QString text = message.trimmed().isEmpty()
            ? QStringLiteral("재생 중 오류가 발생했습니다.")
            : QStringLiteral("재생 오류: %1").arg(message.trimmed());
        showActionStatus(m_actionStatusLabel, text, "error", 3000);
    });

    rebuildEventMarkers();
    updateExportUiState();
}

void PlaybackScreen::setPlaybackService(PlaybackService *service)
{
    m_playbackService = service;
    if (service) {
        m_timelineServiceDisconnectPopupShown = false;
    }
    if (auto *sidebar = findChild<SidebarWidget *>()) {
        sidebar->setPlaybackService(service);
    }
}

void PlaybackScreen::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (auto *sidebar = findChild<SidebarWidget *>()) {
        sidebar->reloadPlaybackTree();
    }
    if (m_playbackTree && m_currentPlaybackChannelId >= 0) {
        QTimer::singleShot(160, this, [this]() {
            highlightSidebarPlaybackItem(m_playbackTree, m_currentPlaybackChannelId, m_currentPlaybackChannelName);
        });
    }
    auto &state = AppState::instance();
    if (state.playbackAutoStartRequested) {
        const int channelId = state.playbackTargetChannelId;
        const QString channel = state.playbackTargetChannel.trimmed().isEmpty()
            ? displayNameForChannelId(channelId)
            : state.playbackTargetChannel;
        const QString date = state.playbackTargetDate.trimmed().isEmpty()
            ? QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))
            : state.playbackTargetDate.trimmed();
        if (channelId >= 0) {
            const QString displayName = channel.trimmed().isEmpty() ? displayNameForChannelId(channelId) : channel.trimmed();
            if (!displayName.isEmpty()) {
                startPlaybackForChannel(channelId, displayName, date);
            }
        } else if (!channel.trimmed().isEmpty()) {
            const int fallbackChannelId = selectedChannelIdForDisplayName(channel);
            if (fallbackChannelId >= 0) {
                startPlaybackForChannel(fallbackChannelId, channel, date);
            }
        }
        state.playbackAutoStartRequested = false;
        state.playbackTargetChannelId = -1;
        state.playbackTargetChannel.clear();
        state.playbackTargetDate.clear();
    }
    placePlaybackSnackbar();
}

void PlaybackScreen::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    placePlaybackSnackbar();
    rebuildEventMarkers();
    updateTimelineHandle();
}

void PlaybackScreen::showPlaybackSnackbar(const QString &message)
{
    if (!m_playbackSnackbarFrame || !m_playbackSnackbarLabel) {
        return;
    }
    m_playbackSnackbarLabel->setText(message);
    m_playbackSnackbarFrame->adjustSize();
    placePlaybackSnackbar();
    m_playbackSnackbarFrame->show();
    m_playbackSnackbarFrame->raise();
    if (m_snackbarHideTimer) {
        m_snackbarHideTimer->start();
    }
}

void PlaybackScreen::clearPlaybackSnackbar()
{
    if (m_snackbarHideTimer) {
        m_snackbarHideTimer->stop();
    }
    if (m_playbackSnackbarFrame) {
        m_playbackSnackbarFrame->hide();
    }
}

void PlaybackScreen::placePlaybackSnackbar()
{
    if (!m_playbackSnackbarFrame) {
        return;
    }
    m_playbackSnackbarFrame->adjustSize();
    const int margin = 12;
    const int x = std::max(0, width() - m_playbackSnackbarFrame->width() - margin);
    const int y = std::max(0, height() - m_playbackSnackbarFrame->height() - margin);
    m_playbackSnackbarFrame->move(x, y);
}
