#include "cctv_screen.h"
#include "app_state.h"
#include "cctv_control_service.h"
#include "channel_session_manager.h"
#include "clip_capture_manager.h"
#include "common_ui.h"
#include "common_widgets.h"
#include "event_service.h"
#include "event_ui_helpers.h"
#include "popup_manager.h"

#include <QDateTime>
#include <QDebug>
#include <QFutureWatcher>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <QtConcurrent>

#include <algorithm>

namespace {

QString fullscreenOsdLabel(const QString &displayName)
{
    const QString trimmed = displayName.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("빈 셀");
    }

    const auto &contexts = AppState::instance().selectedChannelContexts;
    for (const auto &ctx : contexts) {
        if (ctx.displayName.trimmed() != trimmed) {
            continue;
        }

        QString suffix;
        const QString ip = ctx.deviceIp.trimmed();
        if (!ip.isEmpty()) {
            const QStringList parts = ip.split('.');
            if (!parts.isEmpty() && !parts.constLast().trimmed().isEmpty()) {
                suffix = QStringLiteral("(.%1)").arg(parts.constLast().trimmed());
            }
        }

        if (ctx.channelNo >= 0) {
            return suffix.isEmpty()
                ? QStringLiteral("Channel %1").arg(ctx.channelNo)
                : QStringLiteral("Channel %1 %2").arg(ctx.channelNo).arg(suffix);
        }
        break;
    }

    return trimmed;
}

} // namespace

CctvScreen::CctvScreen(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    TopbarWidget::Config topbarConfig;
    topbarConfig.showNotification = true;
    topbarConfig.showSettings = true;
    topbarConfig.showLogout = true;
    auto *topbar = new TopbarWidget(topbarConfig, this);

    auto *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    SidebarWidget::Config sidebarConfig;
    sidebarConfig.primaryBottomText = "스냅샷";
    sidebarConfig.secondaryBottomText = "클립";
    auto *sidebar = new SidebarWidget(sidebarConfig, this);
    auto *channelTab = sidebar->channelTab();
    auto *playbackTab = sidebar->playbackTab();
    auto *channelPage = sidebar->channelPage();
    m_channelTree = sidebar->cctvTree();
    auto *ugvTree = sidebar->ugvTree();
    auto *playbackTree = sidebar->playbackTree();
    auto *treeStack = sidebar->treeStack();
    auto *snapshotButton = sidebar->primaryBottomButton();
    auto *clipButton = sidebar->secondaryBottomButton();
    auto *actionStatusLabel = sidebar->actionStatusLabel();
    m_clipButton = clipButton;
    m_actionStatusLabel = actionStatusLabel;

    auto *backButton = new QPushButton("← 멀티뷰로 돌아가기", sidebar);
    backButton->setObjectName("backLink");
    backButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    backButton->setFixedHeight(22);
    sidebar->populateChannelTree();
    treeStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    sidebar->controlsLayout()->addSpacing(12);
    sidebar->controlsLayout()->addWidget(backButton);
    sidebar->controlsLayout()->addSpacing(14);
    auto *ptzLabel = new QLabel("Zoom / Focus", sidebar);
    ptzLabel->setObjectName("cctvControlTitle");
    ptzLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    sidebar->controlsLayout()->addWidget(ptzLabel);
    sidebar->controlsLayout()->addSpacing(8);

    const QList<int> controlSteps = {-100, -10, -1, 1, 10, 100};
    auto createControlRow = [this, sidebar, controlSteps](
                                const QString &title,
                                const QString &leftEdge,
                                const QString &rightEdge,
                                const std::function<void(int)> &submit) {
        auto *section = new QFrame(sidebar);
        section->setObjectName("cctvControlSection");
        auto *sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(6);

        auto *titleLabel = new QLabel(title, sidebar);
        titleLabel->setObjectName("cctvControlLabel");
        sectionLayout->addWidget(titleLabel);

        auto *edgeRow = new QFrame(section);
        edgeRow->setObjectName("cctvControlRow");
        auto *edgeLayout = new QHBoxLayout(edgeRow);
        edgeLayout->setContentsMargins(0, 0, 0, 0);
        edgeLayout->setSpacing(0);

        auto *leftLabel = new QLabel(leftEdge, edgeRow);
        leftLabel->setObjectName("cctvControlEdgeLabel");
        leftLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        edgeLayout->addWidget(leftLabel);
        edgeLayout->addStretch(1);

        auto *rightLabel = new QLabel(rightEdge, edgeRow);
        rightLabel->setObjectName("cctvControlEdgeLabel");
        rightLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        edgeLayout->addWidget(rightLabel);
        sectionLayout->addWidget(edgeRow);

        auto *buttonGrid = new QFrame(section);
        buttonGrid->setObjectName("cctvControlStepGrid");
        auto *grid = new QGridLayout(buttonGrid);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(0);
        grid->setVerticalSpacing(0);

        for (int index = 0; index < controlSteps.size(); ++index) {
            const int step = controlSteps.at(index);
            auto *button = new QPushButton(QString::number(step), buttonGrid);
            button->setObjectName("cctvControlStepButton");
            button->setFixedHeight(28);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            connect(button, &QPushButton::clicked, this, [submit, step]() {
                submit(step);
            });
            grid->addWidget(button, 0, index);
        }
        sectionLayout->addWidget(buttonGrid);

        sidebar->controlsLayout()->addWidget(section);
        sidebar->controlsLayout()->addSpacing(8);
    };

    createControlRow(
        "Zoom",
        "W",
        "T",
        [this](int step) { submitZoomStep(step); });
    createControlRow(
        "Focus",
        "−",
        "+",
        [this](int step) { submitFocusStep(step); });

    auto *videoPanel = new QFrame(this);
    videoPanel->setObjectName("centerPanel");
    videoPanel->setMinimumWidth(900);
    videoPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *videoPanelLayout = new QVBoxLayout(videoPanel);
    videoPanelLayout->setContentsMargins(0, 0, 0, 0);
    videoPanelLayout->setSpacing(0);
    m_videoHost = new QFrame(videoPanel);
    m_videoHost->setObjectName("fullscreenVideoHost");
    m_videoHost->setMinimumHeight(420);
    m_videoHost->setAttribute(Qt::WA_NativeWindow);
    videoPanelLayout->addWidget(m_videoHost, 1);

    applyNotificationUnreadState(topbar, EventUiHelpers::currentUnreadCount() > 0, EventUiHelpers::currentUnreadCount());
    if (auto *service = EventUiHelpers::eventService()) {
        connect(service, &EventService::unreadChanged, this, [topbar](int count) {
            applyNotificationUnreadState(topbar, count > 0, count);
        });
    }

    auto *sidebarSeparator = new QFrame(this);
    sidebarSeparator->setObjectName("sidebarSeparator");
    sidebarSeparator->setFixedWidth(1);

    body->addWidget(sidebar, 240);
    body->addWidget(sidebarSeparator);
    body->addWidget(videoPanel, 1680);
    root->addWidget(topbar);
    root->addLayout(body, 1);

    connect(backButton, &QPushButton::clicked, this, &CctvScreen::backToMainRequested);
    connect(topbar, &TopbarWidget::settingsClicked, this, &CctvScreen::settingsRequested, Qt::UniqueConnection);
    connect(topbar, &TopbarWidget::logoutClicked, this, &CctvScreen::logoutRequested, Qt::UniqueConnection);
    connect(topbar, &TopbarWidget::notificationCenterClicked, this, [this, topbar]() {
        openNotificationCenterDialog(this, topbar);
    });
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
    playbackTab->installEventFilter(new DoubleClickFilter([this]() {
        auto &appState = AppState::instance();
        appState.playbackAutoStartRequested = false;
        appState.playbackTargetChannelId = -1;
        appState.playbackTargetChannel.clear();
        appState.playbackTargetDate.clear();
        emit openPlaybackRequested();
    }, playbackTab));
    connect(playbackTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (!item || item->childCount() > 0) {
            return;
        }
        auto &appState = AppState::instance();
        const QString channel = item->data(0, Qt::UserRole + 2).toString().trimmed().isEmpty()
            ? item->text(0).trimmed()
            : item->data(0, Qt::UserRole + 2).toString().trimmed();
        const int channelId = item->data(0, Qt::UserRole).toInt();
        const QString date = item->data(0, Qt::UserRole + 1).toString().trimmed();
        if (!channel.isEmpty()) {
            appState.playbackAutoStartRequested = true;
            appState.playbackTargetChannelId = channelId;
            appState.playbackTargetChannel = channel;
            appState.playbackTargetDate = date;
            appState.activeChannel = channel;
        } else {
            appState.playbackAutoStartRequested = false;
            appState.playbackTargetChannelId = -1;
            appState.playbackTargetChannel.clear();
            appState.playbackTargetDate.clear();
        }
        emit openPlaybackRequested();
    });
    setSidebarTabState(channelTab, playbackTab, true);
    connect(m_channelTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
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
        refreshStream();
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
connect(snapshotButton, &QPushButton::clicked, this, [this, snapshotButton, actionStatusLabel]() {
        QString savedPath;
        QString errorMessage;
        const QString snapshotChannel = m_boundChannel.isEmpty() ? AppState::instance().activeChannel : m_boundChannel;
        if (!saveSnapshotPngFromChannel(snapshotChannel, "vms_snapshot", &savedPath, &errorMessage)) {
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
            showActionStatus(actionStatusLabel, errorMessage, "error", 2500);
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
    auto refreshClipButton = [this]() {
        if (!m_clipButton) {
            return;
        }
        if (m_clipButton->property("feedbackActive").toBool()) {
            return;
        }
        m_clipButton->setText(clipButtonText());
    };
    refreshClipButton();
    auto *clipUiTimer = new QTimer(this);
    clipUiTimer->setInterval(1000);
    connect(clipUiTimer, &QTimer::timeout, this, refreshClipButton);
    clipUiTimer->start();
		connect(clipButton, &QPushButton::clicked, this, [this, refreshClipButton, clipButton, actionStatusLabel]() {
        auto &clipMgr = ClipCaptureManager::instance();
        if (clipMgr.isEncoding()) {
            showActionStatus(actionStatusLabel, "클립 인코딩이 진행 중입니다.", "info", 2200);
            return;
        }
        if (!clipMgr.isRecording()) {
            const QString clipChannel = m_boundChannel.isEmpty() ? AppState::instance().activeChannel : m_boundChannel;
            if (!clipMgr.startChannel(clipChannel)) {
                showActionStatus(actionStatusLabel, "클립 시작 실패.", "error", 2200);
                return;
            }
            refreshOsd();
        } else {
            clipButton->setEnabled(false);
            ClipCaptureManager::EncodeSnapshot snapshot;
            const auto prepared = clipMgr.prepareEncoding(&snapshot);
            if (!prepared.ok) {
                clipButton->setEnabled(true);
                handleClipEncodeFailure(this, actionStatusLabel, "클립", prepared, [this]() { emit settingsRequested(); });
                refreshClipButton();
                refreshOsd();
                return;
            }
            const auto cancelToken = snapshot.cancelRequested;
            auto *watcher = new QFutureWatcher<ClipCaptureManager::EncodeResult>(this);
            connect(watcher, &QFutureWatcher<ClipCaptureManager::EncodeResult>::finished, this, [this, watcher, clipButton, actionStatusLabel, refreshClipButton, cancelToken]() {
                clipButton->setEnabled(true);
                const auto encode = watcher->result();
                watcher->deleteLater();
                ClipCaptureManager::instance().finishEncoding(cancelToken);
                if (!encode.ok) {
                    handleClipEncodeFailure(this, actionStatusLabel, "클립", encode, [this]() { emit settingsRequested(); });
                    refreshClipButton();
                    refreshOsd();
                    return;
                }
                clipButton->setProperty("feedbackActive", true);
                clipButton->setProperty("feedbackState", QStringLiteral("done"));
                clipButton->style()->unpolish(clipButton);
                clipButton->style()->polish(clipButton);
                clipButton->setText(QStringLiteral("저장완료"));
                QTimer::singleShot(2000, clipButton, [clipButton, refreshClipButton]() {
                    if (!clipButton) {
                        return;
                    }
                    clipButton->setProperty("feedbackActive", false);
                    clipButton->setProperty("feedbackState", QVariant());
                    clipButton->style()->unpolish(clipButton);
                    clipButton->style()->polish(clipButton);
                    refreshClipButton();
                });
                refreshClipButton();
                refreshOsd();
            });
            watcher->setFuture(QtConcurrent::run([snapshot = std::move(snapshot)]() mutable {
                return ClipCaptureManager::encodeSnapshot(snapshot);
            }));
            refreshClipButton();
            refreshOsd();
            return;
        }
        refreshClipButton();
    });

    m_osdTimestampLabel = new QLabel(m_videoHost);
    m_osdTimestampLabel->setObjectName("videoOsdTimestamp");
    m_osdTimestampLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_osdTimestampLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_osdTimestampLabel->setAttribute(Qt::WA_NoSystemBackground);
    m_osdTimestampLabel->setAutoFillBackground(false);
    m_osdTimestampLabel->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    m_osdTimestampLabel->setFixedSize(208, 24);
    m_osdTimestampLabel->setAlignment(Qt::AlignCenter);
    m_osdConnectionLabel = new QLabel(m_videoHost);
    m_osdConnectionLabel->setObjectName("videoOsdConnection");
    m_osdConnectionLabel->setProperty("state", "error");
    m_osdConnectionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_osdConnectionLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_osdConnectionLabel->setAttribute(Qt::WA_NoSystemBackground);
    m_osdConnectionLabel->setAutoFillBackground(false);
    m_osdConnectionLabel->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    m_osdConnectionLabel->setFixedSize(18, 24);
    m_osdConnectionLabel->setAlignment(Qt::AlignCenter);
    m_osdChannelLabel = new QLabel(m_videoHost);
    m_osdChannelLabel->setObjectName("videoOsdChannel");
    m_osdChannelLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_osdChannelLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_osdChannelLabel->setAttribute(Qt::WA_NoSystemBackground);
    m_osdChannelLabel->setAutoFillBackground(false);
    m_osdChannelLabel->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    m_osdChannelLabel->setFixedHeight(24);
    m_osdChannelLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_osdTimestampLabel->raise();
    m_osdConnectionLabel->raise();
    m_osdChannelLabel->raise();

    m_osdTimer = new QTimer(this);
    m_osdTimer->setInterval(1000);
    connect(m_osdTimer, &QTimer::timeout, this, &CctvScreen::refreshOsd);
    m_osdTimer->start();
}

void CctvScreen::setCctvControlService(CctvControlService *service)
{
    m_cctvControlService = service;
}

void CctvScreen::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_clipButton) {
        m_clipButton->setText(clipButtonText());
    }
    QTimer::singleShot(0, this, [this]() {
        if (!isVisible()) {
            return;
        }
        refreshStream();
        if (ClipCaptureManager::instance().isRecording()) {
            const QString clipChannel = m_boundChannel.isEmpty() ? AppState::instance().activeChannel : m_boundChannel;
            ClipCaptureManager::instance().setSourceChannel(clipChannel);
        }
    });
}

void CctvScreen::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (!m_boundChannel.isEmpty()) {
        ChannelSessionManager::instance().unbindChannelFromWidget(m_boundChannel, m_videoHost);
    }
}

void CctvScreen::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    refreshOsd();
}

void CctvScreen::refreshStream()
{
    // CCTV fullscreen은 AppState.activeChannel만 믿지 않고,
    // 현재 선택된 CCTV 채널을 다시 해석해 video host에 bind/unbind를 수행한다.
    // active Cctv target을 다시 정규화한 뒤 세션 매니저에 반영한다.
    // 즉 이 함수는 "fullscreen 진입 시 실제로 어떤 채널을 bound할지"를 결정하고,
    // clip source와 OSD까지 함께 갱신하는 화면-미디어 접점이다.
    constexpr int kRebindDelayMs = 120;

    const int refreshGeneration = ++m_streamRefreshGeneration;
    QString activeChannel;
    int resolvedCctvChannelId = -1;
    resolveAndNormalizeActiveCctvTarget(&resolvedCctvChannelId, &activeChannel);
    highlightSidebarChannelItem(m_channelTree, activeChannel);

    QSet<QString> activeChannels;
    if (!activeChannel.isEmpty()) {
        activeChannels.insert(activeChannel);
    }
    ChannelSessionManager::instance().applyActiveChannels(activeChannels);
    const QString url = rtspUrlForDisplayName(activeChannel, QStringLiteral("CCTV"));
    if (url.isEmpty()) {
        if (!m_boundChannel.isEmpty()) {
            ChannelSessionManager::instance().unbindChannelFromWidget(m_boundChannel, m_videoHost);
            m_boundChannel.clear();
        }
        refreshOsd();
        return;
    }
    if (!m_boundChannel.isEmpty() && m_boundChannel != activeChannel) {
        ChannelSessionManager::instance().unbindChannelFromWidget(m_boundChannel, m_videoHost);
    }
    m_videoHost->setAttribute(Qt::WA_NativeWindow);
    m_videoHost->winId();
    ChannelSessionManager::instance().bindChannelToWidget(activeChannel, m_videoHost, StreamQualityProfile::Normal);
    m_boundChannel = activeChannel;
    if (ClipCaptureManager::instance().isRecording()) {
        ClipCaptureManager::instance().setSourceChannel(m_boundChannel);
    }
    QTimer::singleShot(kRebindDelayMs, this, [this, activeChannel, refreshGeneration]() {
        if (!isVisible()
            || m_boundChannel != activeChannel
            || refreshGeneration != m_streamRefreshGeneration) {
            return;
        }
        if (m_videoHost) {
            m_videoHost->setAttribute(Qt::WA_NativeWindow);
            m_videoHost->winId();
        }
        ChannelSessionManager::instance().bindChannelToWidget(activeChannel, m_videoHost, StreamQualityProfile::Normal);
        if (ClipCaptureManager::instance().isRecording()) {
            ClipCaptureManager::instance().setSourceChannel(activeChannel);
        }
    });
    refreshOsd();
}

int CctvScreen::resolveActiveCctvChannelId() const
{
    int resolvedChannelId = -1;
    resolveAndNormalizeActiveCctvTarget(&resolvedChannelId, nullptr);
    return resolvedChannelId;
}

void CctvScreen::submitZoomStep(int step)
{
    // 줌/포커스는 서버가 허용하는 discrete step만 받는다.
    // 이 함수는 현재 활성 CCTV 채널에 대해 zoom step 요청을 보내고 결과를 상태라벨로 돌려준다.
    // 그래서 이 함수는 UI 버튼 클릭을 바로 전송하지 않고,
    // 채널 식별자/서비스 연결/중복 요청 여부를 먼저 검증한 뒤
    // 결과를 action status로 피드백한다.
    if (m_zoomRequestInFlight) {
        return;
    }

    const int channelId = resolveActiveCctvChannelId();
    if (channelId < 0) {
        showActionStatus(m_actionStatusLabel, "CCTV 채널 정보 없음", "error", 2000);
        return;
    }
    if (!m_cctvControlService) {
        showActionStatus(m_actionStatusLabel, "제어 서비스 미연결", "error", 2000);
        return;
    }
    if (!CctvControlService::isSupportedStepValue(step)) {
        return;
    }

    m_zoomRequestInFlight = true;
    showActionStatus(m_actionStatusLabel, "줌 제어 요청 중...", "progress");
    m_cctvControlService->zoomStep(channelId, step, this, [this](const CctvControlResult &result) {
        m_zoomRequestInFlight = false;
        if (!result.ok) {
            const QString detail = result.errorMessage.trimmed();
            const QString message = detail.isEmpty()
                ? QStringLiteral("줌 제어 실패")
                : QStringLiteral("줌 제어 실패: %1").arg(detail);
            showActionStatus(m_actionStatusLabel, message, "error", 2500);
            return;
        }
        showActionStatus(m_actionStatusLabel, "줌 제어 완료 ✓", "success", 1500);
    });
}

void CctvScreen::submitFocusStep(int step)
{
    // 포커스 제어도 줌과 동일한 guard를 사용한다.
    // 현재 활성 CCTV 채널에 focus step 요청을 보내고 성공/실패를 상태라벨로 노출한다.
    // fullscreen 제어 패널에서 반복 클릭이 쉬운 만큼,
    // "현재 요청이 끝나기 전 중복 전송 금지"와 "사용자 피드백 일원화"가 핵심이다.
    if (m_focusRequestInFlight) {
        return;
    }

    const int channelId = resolveActiveCctvChannelId();
    if (channelId < 0) {
        showActionStatus(m_actionStatusLabel, "CCTV 채널 정보 없음", "error", 2000);
        return;
    }
    if (!m_cctvControlService) {
        showActionStatus(m_actionStatusLabel, "제어 서비스 미연결", "error", 2000);
        return;
    }
    if (!CctvControlService::isSupportedStepValue(step)) {
        return;
    }

    m_focusRequestInFlight = true;
    showActionStatus(m_actionStatusLabel, "포커스 제어 요청 중...", "progress");
    m_cctvControlService->focusStep(channelId, step, this, [this](const CctvControlResult &result) {
        m_focusRequestInFlight = false;
        if (!result.ok) {
            const QString detail = result.errorMessage.trimmed();
            const QString message = detail.isEmpty()
                ? QStringLiteral("포커스 제어 실패")
                : QStringLiteral("포커스 제어 실패: %1").arg(detail);
            showActionStatus(m_actionStatusLabel, message, "error", 2500);
            return;
        }
        showActionStatus(m_actionStatusLabel, "포커스 제어 완료 ✓", "success", 1500);
    });
}

void CctvScreen::refreshOsd()
{
    // CCTV fullscreen OSD는 채널명/시간/연결 상태를 별도 타이머로 갱신한다.
    // 현재 바운드 채널과 stream status를 읽어 상단 OSD 라벨들의 텍스트/위치를 다시 계산한다.
    // 연결 상태는 ChannelSessionManager의 실제 stream status를 기준으로 하고,
    // 채널명은 가운데 타이머/오른쪽 상태 점과 겹치지 않도록 매 refresh마다 elide 폭을 다시 계산한다.
    if (!m_osdTimestampLabel || !m_osdConnectionLabel || !m_osdChannelLabel || !m_videoHost) {
        return;
    }
    m_osdTimestampLabel->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    const StreamStatus s = ChannelSessionManager::instance().statusForChannel(AppState::instance().activeChannel);
    if (s == StreamStatus::Playing) {
        m_osdConnectionLabel->setText("⬤");
        m_osdConnectionLabel->setProperty("state", "playing");
    } else if (s == StreamStatus::Connecting) {
        m_osdConnectionLabel->setText("⬤");
        m_osdConnectionLabel->setProperty("state", "connecting");
    } else {
        m_osdConnectionLabel->setText("⬤");
        m_osdConnectionLabel->setProperty("state", "error");
    }
    m_osdConnectionLabel->style()->unpolish(m_osdConnectionLabel);
    m_osdConnectionLabel->style()->polish(m_osdConnectionLabel);
    const QString currentChannel = m_boundChannel.isEmpty()
        ? AppState::instance().activeChannel
        : m_boundChannel;
    const QString fullChannelText = fullscreenOsdLabel(currentChannel);
    const int tsW = m_osdTimestampLabel->width();
    const int topY = 0;
    const int leftMargin = 12;
    const int rightMargin = 12;
    const int connectionWidth = m_osdConnectionLabel->width();
    const int channelAvailableWidth = std::max(
        80,
        ((m_videoHost->width() - tsW) / 2) - leftMargin - 12);
    const QString channelText = m_osdChannelLabel->fontMetrics().elidedText(
        fullChannelText,
        Qt::ElideRight,
        channelAvailableWidth);
    m_osdChannelLabel->setText(channelText);
    m_osdChannelLabel->setToolTip(fullChannelText);
    m_osdTimestampLabel->move(std::max(12, (m_videoHost->width() - tsW) / 2), topY);
    m_osdConnectionLabel->move(std::max(12, m_videoHost->width() - connectionWidth - rightMargin), topY);
    m_osdChannelLabel->setFixedWidth(channelAvailableWidth);
    m_osdChannelLabel->move(leftMargin, topY);
    m_osdTimestampLabel->raise();
    m_osdConnectionLabel->raise();
    m_osdChannelLabel->raise();
}
