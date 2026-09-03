#include "main_screen.h"
#include "app_state.h"
#include "channel_session_manager.h"
#include "clip_capture_manager.h"
#include "common_ui.h"
#include "common_widgets.h"
#include "event_service.h"
#include "event_ui_helpers.h"
#include "popup_manager.h"

#include <QCursor>
#include <QChildEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFutureWatcher>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <QtConcurrent>

#include <algorithm>
#include <functional>
#include <memory>

namespace {
QString eventHeadKey(const QVector<EventInfo> &events)
{
    if (events.isEmpty()) {
        return QString();
    }
    const EventInfo &head = events.first();
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(head.eventId.trimmed(),
             head.timestamp.trimmed(),
             QString::number(head.channelId),
             head.channel.trimmed(),
             head.type.trimmed());
}

QString streamStatusDotText(StreamStatus status)
{
    switch (status) {
    case StreamStatus::Playing:
        return "● 연결됨";
    case StreamStatus::Connecting:
        return "● 연결 중";
    case StreamStatus::Error:
    case StreamStatus::Idle:
    default:
        return "● 연결 안 됨";
    }
}

const char *streamStatusProperty(StreamStatus status)
{
    switch (status) {
    case StreamStatus::Playing:
        return "playing";
    case StreamStatus::Connecting:
        return "connecting";
    case StreamStatus::Error:
    case StreamStatus::Idle:
    default:
        return "error";
    }
}

QString multiviewOsdLabel(const QString &displayName)
{
    const QString trimmedDisplayName = displayName.trimmed();
    if (trimmedDisplayName.isEmpty()) {
        return trimmedDisplayName;
    }

    const auto &contexts = AppState::instance().selectedChannelContexts;
    for (const auto &ctx : contexts) {
        if (ctx.displayName.trimmed() != trimmedDisplayName) {
            continue;
        }
        const QString trimmedIp = ctx.deviceIp.trimmed();
        if (trimmedIp.isEmpty()) {
            return trimmedDisplayName;
        }
        const QStringList parts = trimmedIp.split('.');
        const QString lastOctet = parts.isEmpty() ? QString() : parts.constLast().trimmed();
        if (lastOctet.isEmpty()) {
            break;
        }
        if (ctx.channelNo >= 0) {
            return QStringLiteral("Channel %1 (.%2)").arg(ctx.channelNo).arg(lastOctet);
        }
        return QStringLiteral("%1 (.%2)").arg(trimmedDisplayName, lastOctet);
    }

    return trimmedDisplayName;
}

class VideoCellWidget final : public QFrame
{
public:
    explicit VideoCellWidget(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setObjectName("videoCell");
        setProperty("multiViewCell", true);
        setMinimumHeight(150);
        setAcceptDrops(true);
        setMouseTracking(true);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_overlayBar = new QFrame(this);
        m_overlayBar->setObjectName("videoCellTopBar");
        m_overlayBar->setFixedHeight(20);
        m_overlayBar->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_overlayBarLayout = new QHBoxLayout(m_overlayBar);
        m_overlayBarLayout->setContentsMargins(8, 0, 8, 0);
        m_overlayBarLayout->setSpacing(8);

        m_channelLabel = new QLabel(m_overlayBar);
        m_statusLabel = new QLabel("● 연결 안 됨", m_overlayBar);
        m_channelLabel->setObjectName("videoCellChannelLabel");
        m_statusLabel->setObjectName("videoCellStatusLabel");
        m_channelLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_channelLabel->setAttribute(Qt::WA_TranslucentBackground);
        m_statusLabel->setAttribute(Qt::WA_TranslucentBackground);
        m_channelLabel->setAttribute(Qt::WA_NoSystemBackground);
        m_statusLabel->setAttribute(Qt::WA_NoSystemBackground);
        m_channelLabel->setAutoFillBackground(false);
        m_statusLabel->setAutoFillBackground(false);
        m_channelLabel->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        m_statusLabel->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        m_overlayBarLayout->addWidget(m_channelLabel, 1);
        m_overlayBarLayout->addWidget(m_statusLabel, 0, Qt::AlignRight | Qt::AlignVCenter);

        m_videoHost = new QWidget(this);
        m_videoHost->setObjectName("videoHost");
        m_videoHost->setMinimumHeight(110);
        m_videoHost->setAttribute(Qt::WA_NativeWindow);
        m_videoHost->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_videoHost->setMouseTracking(true);
        m_videoHost->installEventFilter(this);
        m_overlayBar->installEventFilter(this);
        m_channelLabel->installEventFilter(this);
        m_statusLabel->installEventFilter(this);
        // X 버튼 — 기본 숨김, 호버 시 표시
        m_closeButton = new QPushButton("✕", m_videoHost);
        m_closeButton->setObjectName("videoCellCloseButton");
        m_closeButton->setFixedSize(16, 16);
        m_closeButton->setMouseTracking(true);
        m_closeButton->hide();
        QObject::connect(m_closeButton, &QPushButton::clicked, this, [this]() {
            if (onClearRequested) {
                onClearRequested();
            }
        });

        auto makeSelectionBar = [this]() {
            auto *bar = new QFrame(m_videoHost);
            bar->setAttribute(Qt::WA_TransparentForMouseEvents);
            bar->setStyleSheet(QStringLiteral("background: #F37321; border: none;"));
            bar->hide();
            return bar;
        };
        m_selectionTop = makeSelectionBar();
        m_selectionBottom = makeSelectionBar();
        m_selectionLeft = makeSelectionBar();
        m_selectionRight = makeSelectionBar();

        layout->addWidget(m_overlayBar, 0);
        layout->addWidget(m_videoHost, 1);

        m_statusTimer = new QTimer(this);
        m_statusTimer->setInterval(250);
        QObject::connect(m_statusTimer, &QTimer::timeout, this, [this]() {
            const StreamStatus status = ChannelSessionManager::instance().statusForChannel(m_currentChannel);
            m_statusLabel->setText(streamStatusDotText(status));
            const QString nextState = QString::fromLatin1(streamStatusProperty(status));
            if (m_statusLabel->property("state").toString() != nextState) {
                m_statusLabel->setProperty("state", nextState);
                m_statusLabel->style()->unpolish(m_statusLabel);
                m_statusLabel->style()->polish(m_statusLabel);
            }
            m_statusLabel->adjustSize();
            updateOverlayLayout();
        });
        m_statusTimer->start();

        setSelected(false);
        QTimer::singleShot(0, this, [this]() {
            updateSelectionOutlineGeometry();
            updateOverlayLayout();
        });
    }

    ~VideoCellWidget() override
    {
        if (!m_currentChannel.isEmpty()) {
            ChannelSessionManager::instance().unbindChannelFromWidget(m_currentChannel, m_videoHost);
        }
    }

    void setSelected(bool selected)
    {
        for (auto *bar : {m_selectionTop, m_selectionBottom, m_selectionLeft, m_selectionRight}) {
            if (bar) {
                bar->setVisible(selected);
                bar->raise();
            }
        }
        update();
    }

    void setChannelName(const QString &channel, StreamQualityProfile profile = StreamQualityProfile::Normal)
    {
        const QString previousChannel = m_currentChannel;
        m_hasChannel = !channel.isEmpty();
        if (channel.isEmpty()) {
            m_currentChannel.clear();
            m_currentUrl.clear();
            m_channelFullText = QStringLiteral("빈 셀");
            updateChannelLabelText();
            m_statusLabel->setText("● 연결 안 됨");
            m_statusLabel->setProperty("state", "error");
            m_statusLabel->style()->unpolish(m_statusLabel);
            m_statusLabel->style()->polish(m_statusLabel);
            if (!previousChannel.isEmpty()) {
                ChannelSessionManager::instance().unbindChannelFromWidget(previousChannel, m_videoHost);
            }
            updateOverlayLayout();
            return;
        }
        m_channelFullText = multiviewOsdLabel(channel);
        updateChannelLabelText();
        const QString url = rtspUrlForDisplayName(channel);
        if (url.isEmpty()) {
            m_currentChannel = channel;
            m_currentUrl.clear();
            m_statusLabel->setText("● 연결 안 됨");
            m_statusLabel->setProperty("state", "error");
            m_statusLabel->style()->unpolish(m_statusLabel);
            m_statusLabel->style()->polish(m_statusLabel);
            if (!previousChannel.isEmpty()) {
                ChannelSessionManager::instance().unbindChannelFromWidget(previousChannel, m_videoHost);
            }
            updateOverlayLayout();
            return;
        }

        if (channel == m_currentChannel
            && url == m_currentUrl
            && (ChannelSessionManager::instance().statusForChannel(channel) == StreamStatus::Playing
                || ChannelSessionManager::instance().statusForChannel(channel) == StreamStatus::Connecting)) {
            // Even on the same channel/url, propagate layout-driven quality profile changes.
            ChannelSessionManager::instance().bindChannelToWidget(channel, m_videoHost, profile);
            return;
        }

        if (!previousChannel.isEmpty() && previousChannel != channel) {
            ChannelSessionManager::instance().unbindChannelFromWidget(previousChannel, m_videoHost);
        }
        m_currentChannel = channel;
        m_currentUrl = url;
        ChannelSessionManager::instance().bindChannelToWidget(channel, m_videoHost, profile);
        updateOverlayLayout();
    }

    std::function<void()> onClicked;
    std::function<void()> onDoubleClicked;
    std::function<void(const DroppedChannelInfo &)> onChannelDropped;
    std::function<void()> onClearRequested;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!event) {
            return QFrame::eventFilter(watched, event);
        }

        if (watched == m_videoHost) {
            if (event->type() == QEvent::ChildAdded) {
                auto *childEvent = static_cast<QChildEvent *>(event);
                if (childEvent->added() && childEvent->child()) {
                    childEvent->child()->installEventFilter(this);
                }
            } else if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
                updateSelectionOutlineGeometry();
            }
            return QFrame::eventFilter(watched, event);
        }

        bool watchedSelfChild = (watched == m_videoHost || watched == m_channelLabel || watched == m_statusLabel);
        if (!watchedSelfChild && watched) {
            QObject *p = watched->parent();
            while (p) {
                if (p == m_videoHost) {
                    watchedSelfChild = true;
                    break;
                }
                p = p->parent();
            }
        }
        if (!watchedSelfChild) {
            return QFrame::eventFilter(watched, event);
        }
        if (watched == m_closeButton || (watched && watched->parent() == m_closeButton)) {
            return QFrame::eventFilter(watched, event);
        }

        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && onClicked) {
                onClicked();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && onDoubleClicked) {
                onDoubleClicked();
                return true;
            }
        }
        return QFrame::eventFilter(watched, event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        QFrame::mousePressEvent(event);
        if (event && event->button() == Qt::LeftButton && onClicked) {
            onClicked();
        }
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        QFrame::mouseDoubleClickEvent(event);
        if (event && event->button() == Qt::LeftButton && onDoubleClicked) {
            onDoubleClicked();
        }
    }

    void enterEvent(QEnterEvent *event) override
    {
        QFrame::enterEvent(event);
        updateCloseButtonVisibility();
    }

    void leaveEvent(QEvent *event) override
    {
        QFrame::leaveEvent(event);
        QTimer::singleShot(0, this, [this]() {
            updateCloseButtonVisibility();
        });
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        QFrame::mouseMoveEvent(event);
        updateCloseButtonVisibility();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QFrame::resizeEvent(event);
        updateSelectionOutlineGeometry();
        updateOverlayLayout();
        updateCloseButtonVisibility();
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        const DroppedChannelInfo dropped = extractDroppedChannelInfo(event->mimeData());
        if (dropped.isValid()
            && dropped.deviceType.trimmed().compare(QStringLiteral("CCTV"), Qt::CaseInsensitive) == 0) {
            event->acceptProposedAction();
            return;
        }
        QFrame::dragEnterEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        const DroppedChannelInfo dropped = extractDroppedChannelInfo(event->mimeData());
        if (!dropped.isValid()
            || dropped.deviceType.trimmed().compare(QStringLiteral("CCTV"), Qt::CaseInsensitive) != 0) {
            QFrame::dropEvent(event);
            return;
        }
        if (onChannelDropped) {
            onChannelDropped(dropped);
            event->acceptProposedAction();
            return;
        }
        QFrame::dropEvent(event);
    }

private:
    void updateCloseButtonVisibility()
    {
        if (!m_hasChannel) {
            m_closeButton->hide();
            updateOverlayLayout();
            return;
        }
        if (m_videoHost) {
            m_closeButton->move(std::max(4, m_videoHost->width() - m_closeButton->width() - 6), 6);
            m_closeButton->raise();
        }
        const QPoint localPos = mapFromGlobal(QCursor::pos());
        const bool insideSelf = rect().contains(localPos);
        const bool hoveringCloseButton = m_closeButton->underMouse();
        m_closeButton->setVisible(insideSelf || hoveringCloseButton);
        updateOverlayLayout();
    }

    void updateChannelLabelText()
    {
        if (!m_channelLabel) {
            return;
        }
        if (!m_overlayBar) {
            m_channelLabel->setText(m_channelFullText);
            m_channelLabel->setToolTip(m_channelFullText);
            return;
        }
        const int leftInset = 8;
        const int rightInset = (m_closeButton && m_closeButton->isVisible()) ? 36 : 12;
        const int statusWidth = m_statusLabel ? m_statusLabel->sizeHint().width() : 0;
        const int spacing = 8;
        const int availableWidth = std::max(40, m_overlayBar->width() - leftInset - rightInset - statusWidth - spacing);
        const QString elided = m_channelLabel->fontMetrics().elidedText(
            m_channelFullText,
            Qt::ElideRight,
            availableWidth);
        m_channelLabel->setText(elided);
        m_channelLabel->setToolTip(m_channelFullText);
    }

    void updateOverlayLayout()
    {
        if (!m_overlayBar || !m_overlayBarLayout || !m_channelLabel || !m_statusLabel) {
            return;
        }
        updateChannelLabelText();
        m_overlayBarLayout->setContentsMargins(8, 0, 8, 0);
        m_overlayBar->raise();
    }

    void updateSelectionOutlineGeometry()
    {
        if (!m_videoHost) {
            return;
        }
        const QRect hostRect = m_videoHost->rect();
        const int line = 1;
        if (m_selectionTop) {
            m_selectionTop->setGeometry(0, 0, hostRect.width(), line);
            m_selectionTop->raise();
        }
        if (m_selectionBottom) {
            m_selectionBottom->setGeometry(0, std::max(0, hostRect.height() - line), hostRect.width(), line);
            m_selectionBottom->raise();
        }
        if (m_selectionLeft) {
            m_selectionLeft->setGeometry(0, 0, line, hostRect.height());
            m_selectionLeft->raise();
        }
        if (m_selectionRight) {
            m_selectionRight->setGeometry(std::max(0, hostRect.width() - line), 0, line, hostRect.height());
            m_selectionRight->raise();
        }
    }

    QFrame *m_overlayBar = nullptr;
    QHBoxLayout *m_overlayBarLayout = nullptr;
    QLabel *m_channelLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QWidget *m_videoHost = nullptr;
    QPushButton *m_closeButton = nullptr;
    QFrame *m_selectionTop = nullptr;
    QFrame *m_selectionBottom = nullptr;
    QFrame *m_selectionLeft = nullptr;
    QFrame *m_selectionRight = nullptr;
    QTimer *m_statusTimer = nullptr;
    bool m_hasChannel = false;
    QString m_channelFullText;
    QString m_currentChannel;
    QString m_currentUrl;
};

struct MainScreenState {
    int selectedCell = 0;
    int gridCount = 4;
    QVector<VideoCellWidget *> cells;
};


} // namespace

MainScreen::MainScreen(QWidget *parent)
    : QWidget(parent)
{
    auto *state = new MainScreenState();
    auto &appState = AppState::instance();

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
    auto *channelTree = sidebar->cctvTree();
    auto *ugvTree = sidebar->ugvTree();
    auto *playbackTree = sidebar->playbackTree();
    auto *treeStack = sidebar->treeStack();
    auto *snapshot = sidebar->primaryBottomButton();
    auto *clip = sidebar->secondaryBottomButton();
    auto *actionStatusLabel = sidebar->actionStatusLabel();

    auto *layoutRow = new QWidget(sidebar);
    auto *layoutRowLayout = new QHBoxLayout(layoutRow);
    layoutRowLayout->setContentsMargins(8, 0, 0, 0);
    layoutRowLayout->setSpacing(4);

    auto *layoutLabel = new QLabel(QStringLiteral("레이아웃"), layoutRow);
    layoutLabel->setObjectName("sidebarSectionLabel");
    auto *grid4Button = new QPushButton(QStringLiteral("4"), layoutRow);
    auto *grid6Button = new QPushButton(QStringLiteral("6"), layoutRow);
    auto *grid9Button = new QPushButton(QStringLiteral("9"), layoutRow);
    for (auto *button : {grid4Button, grid6Button, grid9Button}) {
        button->setObjectName("layoutToggleButton");
        button->setFixedSize(28, 28);
        button->setCursor(Qt::PointingHandCursor);
    }

    layoutRowLayout->addWidget(layoutLabel);
    layoutRowLayout->addSpacing(10);
    layoutRowLayout->addWidget(grid4Button);
    layoutRowLayout->addSpacing(2);
    layoutRowLayout->addWidget(grid6Button);
    layoutRowLayout->addSpacing(2);
    layoutRowLayout->addWidget(grid9Button);
    layoutRowLayout->addStretch(1);

    sidebar->controlsLayout()->addSpacing(10);
    sidebar->controlsLayout()->addWidget(layoutRow);

    sidebar->populateChannelTree();
    channelTree->setDragEnabled(true);
    channelTree->setDragDropMode(QAbstractItemView::DragOnly);
    channelTree->setSelectionMode(QAbstractItemView::SingleSelection);
    if (ugvTree) {
        ugvTree->setDragEnabled(true);
        ugvTree->setDragDropMode(QAbstractItemView::DragOnly);
        ugvTree->setSelectionMode(QAbstractItemView::SingleSelection);
    }
    std::function<void(QTreeWidgetItem *)> configureChannelTreeItem = [&](QTreeWidgetItem *item) {
        if (!item) {
            return;
        }
        if (item->childCount() > 0) {
            item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsDragEnabled));
            for (int c = 0; c < item->childCount(); ++c) {
                configureChannelTreeItem(item->child(c));
            }
            return;
        }
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    };
    for (int i = 0; i < channelTree->topLevelItemCount(); ++i) {
        configureChannelTreeItem(channelTree->topLevelItem(i));
    }
    if (ugvTree) {
        for (int i = 0; i < ugvTree->topLevelItemCount(); ++i) {
            configureChannelTreeItem(ugvTree->topLevelItem(i));
        }
    }

    auto *center = new QFrame(this);
    center->setObjectName("panel");
    // 1280x720 기본 창에서도 멀티뷰가 우측 패널과 겹치지 않도록
    // 중앙 영역 최소 폭 고정값을 제거한다.
    center->setMinimumWidth(0);
    center->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);

    auto *gridHost = new QWidget(center);
    gridHost->setObjectName("multiviewGridHost");
    auto *gridLayout = new QGridLayout(gridHost);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(1);
    centerLayout->addWidget(gridHost, 1);

    auto *eventViewContainer = new QFrame(this);
    eventViewContainer->setObjectName("eventViewContainer");
    eventViewContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    eventViewContainer->setMinimumWidth(340);
    eventViewContainer->setMaximumWidth(340);
    auto *eventViewContainerLayout = new QVBoxLayout(eventViewContainer);
    eventViewContainerLayout->setContentsMargins(0, 0, 0, 0);
    eventViewContainerLayout->setSpacing(0);
    m_eventView = new EventViewWidget(eventViewContainer);
    m_eventView->setEvents(EventUiHelpers::currentEvents(), 30, true);
    eventViewContainerLayout->addWidget(m_eventView);
    m_eventViewRefreshTimer = new QTimer(this);
    m_eventViewRefreshTimer->setSingleShot(true);
    m_eventViewRefreshTimer->setInterval(5000);
    connect(m_eventViewRefreshTimer, &QTimer::timeout, this, &MainScreen::applyPendingEventViewRefresh);
    auto unreadCount = std::make_shared<int>(EventUiHelpers::currentUnreadCount());

    auto *sidebarSeparator = new QFrame(this);
    sidebarSeparator->setObjectName("sidebarSeparator");
    sidebarSeparator->setFixedWidth(1);

    body->addWidget(sidebar, 240);
    body->addWidget(sidebarSeparator);
    body->addWidget(center, 1300);
    body->addWidget(eventViewContainer, 340);

    root->addWidget(topbar);
    root->addLayout(body, 1);

    auto updateLayoutButtonStyle = [grid4Button, grid6Button, grid9Button](int count) {
        grid4Button->setProperty("active", count == 4);
        grid6Button->setProperty("active", count == 6);
        grid9Button->setProperty("active", count == 9);
        for (auto *button : {grid4Button, grid6Button, grid9Button}) {
            button->style()->unpolish(button);
            button->style()->polish(button);
            button->update();
        }
    };

    auto refreshCells = [state, &appState]() {
        StreamQualityProfile profile = StreamQualityProfile::Normal;
        if (state->gridCount == 4) {
            profile = StreamQualityProfile::QuadGrid;
        } else if (state->gridCount >= 6) {
            profile = StreamQualityProfile::DenseGrid;
        }
        for (int i = 0; i < state->cells.size(); ++i) {
            const QString channel = appState.gridCells[static_cast<std::size_t>(i)].displayName;
            state->cells[i]->setChannelName(channel, profile);
            state->cells[i]->setSelected(i == state->selectedCell);
        }
    };

    auto rebuildGrid = [this, state, &appState, gridLayout, refreshCells](int count) {
        while (QLayoutItem *item = gridLayout->takeAt(0)) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        state->cells.clear();
        state->gridCount = count;
        if (state->selectedCell >= count) {
            state->selectedCell = 0;
        }

        const int columns = (count == 6) ? 2 : ((count == 4) ? 2 : 3);
        for (std::size_t i = static_cast<std::size_t>(count); i < appState.gridCells.size(); ++i) {
            appState.clearGridCell(i);
        }
        for (int i = 0; i < count; ++i) {
            auto *cell = new VideoCellWidget(this);
            state->cells.push_back(cell);

            cell->onClicked = [state, refreshCells, i]() {
                state->selectedCell = i;
                refreshCells();
            };
            cell->onDoubleClicked = [this, &appState, i]() {
                const MainGridCellState &selectedCell = appState.gridCells[static_cast<std::size_t>(i)];
                const QString selected = selectedCell.displayName;
                if (selected.isEmpty()) {
                    return;
                }
                appState.activeChannel = selected;
                const int channelId = selectedCell.channelId;
                SelectedChannelContext ctx;
                const bool isUgvCell = (channelId > 0
                                        && findSelectedChannelContextByChannelId(channelId, &ctx)
                                        && ctx.deviceType.trimmed().compare(QStringLiteral("UGV"), Qt::CaseInsensitive) == 0);
                if (isUgvCell) {
                    appState.activeUgvChannelId = -1;
                    appState.activeUgvGatewayId = -1;
                    appState.activeCctvChannelId = -1;
                    return;
                }
                appState.activeUgvChannelId = -1;
                appState.activeUgvGatewayId = -1;
                int cctvChannelId = channelId;
                if (cctvChannelId <= 0) {
                    cctvChannelId = selectedChannelIdForDisplayNameExact(selected, QStringLiteral("CCTV"));
                }
                appState.activeCctvChannelId = cctvChannelId;
                emit openCctvRequested();
            };
            cell->onChannelDropped = [this, &appState, refreshCells, i](const DroppedChannelInfo &dropped) {
                if (dropped.deviceType.trimmed().compare(QStringLiteral("CCTV"), Qt::CaseInsensitive) != 0) {
                    return;
                }
                appState.setGridCell(static_cast<std::size_t>(i), dropped.displayName, dropped.channelId, dropped.deviceId);
                if (i == 0) {
                    appState.activeChannel = dropped.displayName;
                    int cctvChannelId = dropped.channelId;
                    if (cctvChannelId <= 0) {
                        cctvChannelId = selectedChannelIdForDisplayNameExact(dropped.displayName, QStringLiteral("CCTV"));
                    }
                    appState.activeCctvChannelId = cctvChannelId;
                }
                refreshCells();
            };
            cell->onClearRequested = [&appState, refreshCells, i]() {
                appState.clearGridCell(static_cast<std::size_t>(i));
                refreshCells();
            };

            gridLayout->addWidget(cell, i / columns, i % columns);
        }
        refreshCells();
    };

    int selectedCount = 0;
    for (const auto &cell : appState.gridCells) {
        if (!cell.isEmpty()) {
            ++selectedCount;
        }
    }
    int initialGrid = 4;
    if (selectedCount > 6) {
        initialGrid = 9;
    } else if (selectedCount > 4) {
        initialGrid = 6;
    }
    rebuildGrid(initialGrid);
    updateLayoutButtonStyle(initialGrid);

    connect(grid4Button, &QPushButton::clicked, this, [rebuildGrid, updateLayoutButtonStyle]() {
        rebuildGrid(4);
        updateLayoutButtonStyle(4);
    });
    connect(grid6Button, &QPushButton::clicked, this, [rebuildGrid, updateLayoutButtonStyle]() {
        rebuildGrid(6);
        updateLayoutButtonStyle(6);
    });
    connect(grid9Button, &QPushButton::clicked, this, [rebuildGrid, updateLayoutButtonStyle]() {
        rebuildGrid(9);
        updateLayoutButtonStyle(9);
    });

    connect(channelTree, &QTreeWidget::itemDoubleClicked, this, [this, &appState](QTreeWidgetItem *item) {
        if (!item || item->childCount() > 0) {
            return;
        }
        const int channelId = item->data(0, Qt::UserRole).toInt();
        const QString type = item->data(0, Qt::UserRole + 3).toString().trimmed().toUpper();
        const QString channel = item->data(0, Qt::UserRole + 2).toString().trimmed().isEmpty()
            ? item->text(0).trimmed()
            : item->data(0, Qt::UserRole + 2).toString().trimmed();
        if (channel.isEmpty() || (!type.isEmpty() && type != QStringLiteral("CCTV"))) {
            return;
        }

        int cctvChannelId = channelId;
        if (cctvChannelId <= 0) {
            cctvChannelId = selectedChannelIdForDisplayNameExact(channel, QStringLiteral("CCTV"));
        }
        if (cctvChannelId <= 0) {
            return;
        }
        appState.activeChannel = channel;
        appState.activeUgvChannelId = -1;
        appState.activeUgvGatewayId = -1;
        appState.activeCctvChannelId = cctvChannelId;
        emit openCctvRequested();
    });
    if (ugvTree) {
        connect(ugvTree, &QTreeWidget::itemDoubleClicked, this, [this, &appState](QTreeWidgetItem *item) {
            Q_UNUSED(item);
            Q_UNUSED(appState);
        });
    }
    connect(playbackTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (!item || item->childCount() > 0) {
            return;
        }
        auto &appState = AppState::instance();
        const QString channel = item->data(0, Qt::UserRole + 2).toString().trimmed().isEmpty()
            ? item->text(0).trimmed()
            : item->data(0, Qt::UserRole + 2).toString().trimmed();
        const QString date = item->data(0, Qt::UserRole + 1).toString().trimmed();
        if (!channel.isEmpty()) {
            const int channelId = item->data(0, Qt::UserRole).toInt();
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
    connect(channelTab, &QPushButton::clicked, this, [channelTab, playbackTab, treeStack, channelPage]() {
        treeStack->setCurrentWidget(channelPage);
        setSidebarTabState(channelTab, playbackTab, true);
    });
    connect(playbackTab, &QPushButton::clicked, this, [channelTab, playbackTab, treeStack, playbackTree]() {
        treeStack->setCurrentWidget(playbackTree);
        setSidebarTabState(channelTab, playbackTab, false);
    });
    channelTab->installEventFilter(new DoubleClickFilter([]() {}, channelTab));
    playbackTab->installEventFilter(new DoubleClickFilter([this]() {
        auto &appState = AppState::instance();
        appState.playbackAutoStartRequested = false;
        appState.playbackTargetChannelId = -1;
        appState.playbackTargetChannel.clear();
        appState.playbackTargetDate.clear();
        emit openPlaybackRequested();
    }, playbackTab));
    setSidebarTabState(channelTab, playbackTab, true);
    if (auto *service = EventUiHelpers::eventService()) {
        connect(service, &EventService::eventsUpdated, this, [this](const QVector<EventInfo> &events) {
            scheduleEventViewRefresh(events);
        });
        connect(service, &EventService::unreadChanged, this, [topbar, unreadCount, service](int count) {
            *unreadCount = count;
            if (count > 0) {
                service->markAllRead();
            } else {
                applyNotificationUnreadState(topbar, false, 0);
            }
        });
        if (*unreadCount > 0) {
            service->markAllRead();
        } else {
            applyNotificationUnreadState(topbar, false, 0);
        }
    } else {
        applyNotificationUnreadState(topbar, false, 0);
    }

    connect(topbar, &TopbarWidget::logoutClicked, this, &MainScreen::logoutRequested, Qt::UniqueConnection);
    connect(topbar, &TopbarWidget::settingsClicked, this, &MainScreen::settingsRequested, Qt::UniqueConnection);
    connect(topbar, &TopbarWidget::notificationCenterClicked, this, [this, topbar, unreadCount]() {
        if (auto *service = EventUiHelpers::eventService()) {
            service->markAllRead();
        }
        *unreadCount = EventUiHelpers::currentUnreadCount();
        applyNotificationUnreadState(topbar, false, *unreadCount);
        openNotificationCenterDialog(this, topbar);
    });
    connect(m_eventView, &EventViewWidget::searchRequested, this, [this]() {
        openEventSearchDialog(this);
    });
    connect(m_eventView, &EventViewWidget::ugvDispatchRequested, this, [this, actionStatusLabel, state]() {
        auto &clipMgr = ClipCaptureManager::instance();
        auto proceedToUgv = [this, state]() {
            auto &appState = AppState::instance();
            int ugvChannelId = -1;
            if (appState.activeUgvChannelId > 0) {
                SelectedChannelContext activeCtx;
                if (findSelectedChannelContextByChannelId(appState.activeUgvChannelId, &activeCtx)
                    && activeCtx.deviceType.trimmed().compare(QStringLiteral("UGV"), Qt::CaseInsensitive) == 0) {
                    ugvChannelId = appState.activeUgvChannelId;
                }
            }
            if (ugvChannelId < 0 && state
                && state->selectedCell >= 0
                && state->selectedCell < static_cast<int>(appState.gridCells.size())) {
                const int selectedCellChannelId = appState.gridCells[static_cast<std::size_t>(state->selectedCell)].channelId;
                SelectedChannelContext selectedCtx;
                if (selectedCellChannelId > 0
                    && findSelectedChannelContextByChannelId(selectedCellChannelId, &selectedCtx)
                    && selectedCtx.deviceType.trimmed().compare(QStringLiteral("UGV"), Qt::CaseInsensitive) == 0) {
                    ugvChannelId = selectedCellChannelId;
                }
            }
            if (ugvChannelId < 0) {
                ugvChannelId = firstSelectedChannelIdByType(QStringLiteral("UGV"));
            }
            const int gatewayId = deviceIdForChannelId(ugvChannelId);
            if (ugvChannelId <= 0 || gatewayId <= 0) {
                PopupManager::showInfo(this, "UGV 출동", "연결 가능한 UGV 식별자를 찾을 수 없습니다.");
                return;
            }
            appState.activeUgvChannelId = ugvChannelId;
            appState.activeUgvGatewayId = gatewayId;
            const QString ugvChannel = displayNameForChannelId(ugvChannelId);
            if (!ugvChannel.isEmpty()) {
                appState.activeChannel = ugvChannel;
            }
            emit openUgvRequested();
        };
        const bool clipOn = (clipMgr.state() != ClipCaptureManager::State::Idle);
        const QString confirmText = clipOn
            ? "클립 저장 중입니다. UGV 출동 시 자동 저장 후 전환됩니다. 계속하시겠습니까?"
            : "UGV를 출동하시겠습니까?";
        if (!PopupManager::confirm(this, "UGV 출동", confirmText)) {
            return;
        }
        if (clipOn) {
            showActionStatus(actionStatusLabel, "클립 저장 중...", "progress");
            ClipCaptureManager::EncodeSnapshot snapshot;
            const auto prepared = clipMgr.prepareEncoding(&snapshot);
            if (!prepared.ok) {
                clearActionStatus(actionStatusLabel);
                showActionStatus(actionStatusLabel, QString("자동 저장 실패: %1").arg(prepared.message), "error", 2500);
                proceedToUgv();
                return;
            }
            const auto cancelToken = snapshot.cancelRequested;
            auto *watcher = new QFutureWatcher<ClipCaptureManager::EncodeResult>(this);
            connect(watcher, &QFutureWatcher<ClipCaptureManager::EncodeResult>::finished, this, [this, watcher, actionStatusLabel, cancelToken, proceedToUgv]() {
                const auto encode = watcher->result();
                watcher->deleteLater();
                ClipCaptureManager::instance().finishEncoding(cancelToken);
                if (!encode.ok) {
                    clearActionStatus(actionStatusLabel);
                    showActionStatus(actionStatusLabel, QString("자동 저장 실패: %1").arg(encode.message), "error", 2500);
                } else {
                    showActionStatus(actionStatusLabel, "클립 자동 저장됨 ✓", "success", 2500);
                }
                proceedToUgv();
            });
            watcher->setFuture(QtConcurrent::run([snapshot = std::move(snapshot)]() mutable {
                return ClipCaptureManager::encodeSnapshot(snapshot);
            }));
            return;
        }
        proceedToUgv();
    });
    connect(snapshot, &QPushButton::clicked, this, [this, gridHost, snapshot, actionStatusLabel]() {
        if (!gridHost) {
            showActionStatus(actionStatusLabel, "캡처 소스가 없습니다.", "error", 2200);
            return;
        }
        QString savedPath;
        QString errorMessage;
        if (!saveSnapshotPng(gridHost, "vms_snapshot", &savedPath, &errorMessage)) {
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
        snapshot->setProperty("feedbackState", QStringLiteral("done"));
        snapshot->style()->unpolish(snapshot);
        snapshot->style()->polish(snapshot);
        snapshot->setText(QStringLiteral("저장완료"));
        QTimer::singleShot(1800, snapshot, [snapshot]() {
            if (snapshot) {
                snapshot->setProperty("feedbackState", QVariant());
                snapshot->style()->unpolish(snapshot);
                snapshot->style()->polish(snapshot);
                snapshot->setText(QStringLiteral("스냅샷"));
            }
        });
    });
    auto refreshClipButton = [clip]() {
        if (clip->property("feedbackActive").toBool()) {
            return;
        }
        clip->setText(clipButtonText());
    };
    refreshClipButton();
    auto *clipUiTimer = new QTimer(this);
    clipUiTimer->setInterval(1000);
    connect(clipUiTimer, &QTimer::timeout, this, refreshClipButton);
    clipUiTimer->start();
    connect(clip, &QPushButton::clicked, this, [this, gridHost, refreshClipButton, clip, actionStatusLabel]() {
        auto &clipMgr = ClipCaptureManager::instance();
        if (clipMgr.isEncoding()) {
            showActionStatus(actionStatusLabel, "클립 인코딩이 진행 중입니다.", "info", 2200);
            return;
        }
        if (!clipMgr.isRecording()) {
            if (!gridHost) {
                showActionStatus(actionStatusLabel, "클립 캡처 소스가 없습니다.", "error", 2200);
                return;
            }
            if (!clipMgr.start(gridHost)) {
                showActionStatus(actionStatusLabel, "클립 시작 실패.", "error", 2200);
                return;
            }
        } else {
            clip->setEnabled(false);
            ClipCaptureManager::EncodeSnapshot snapshot;
            const auto prepared = clipMgr.prepareEncoding(&snapshot);
            if (!prepared.ok) {
                clip->setEnabled(true);
                handleClipEncodeFailure(this, actionStatusLabel, "클립", prepared, [this]() { emit settingsRequested(); });
                refreshClipButton();
                return;
            }
            const auto cancelToken = snapshot.cancelRequested;
            auto *watcher = new QFutureWatcher<ClipCaptureManager::EncodeResult>(this);
            connect(watcher, &QFutureWatcher<ClipCaptureManager::EncodeResult>::finished, this, [this, watcher, clip, actionStatusLabel, refreshClipButton, cancelToken]() {
                clip->setEnabled(true);
                const auto encode = watcher->result();
                watcher->deleteLater();
                ClipCaptureManager::instance().finishEncoding(cancelToken);
                if (!encode.ok) {
                    handleClipEncodeFailure(this, actionStatusLabel, "클립", encode, [this]() { emit settingsRequested(); });
                    refreshClipButton();
                    return;
                }
                clip->setProperty("feedbackActive", true);
                clip->setProperty("feedbackState", QStringLiteral("done"));
                clip->style()->unpolish(clip);
                clip->style()->polish(clip);
                clip->setText(QStringLiteral("저장완료"));
                QTimer::singleShot(2000, clip, [clip, refreshClipButton]() {
                    if (!clip) {
                        return;
                    }
                    clip->setProperty("feedbackActive", false);
                    clip->setProperty("feedbackState", QVariant());
                    clip->style()->unpolish(clip);
                    clip->style()->polish(clip);
                    refreshClipButton();
                });
                refreshClipButton();
            });
            watcher->setFuture(QtConcurrent::run([snapshot = std::move(snapshot)]() mutable {
                return ClipCaptureManager::encodeSnapshot(snapshot);
            }));
            refreshClipButton();
            return;
        }
        refreshClipButton();
    });

    auto *clipSourceTimer = new QTimer(this);
    clipSourceTimer->setInterval(500);
    connect(clipSourceTimer, &QTimer::timeout, this, [this, gridHost]() {
        if (!isVisible() || !ClipCaptureManager::instance().isRecording() || !gridHost) {
            return;
        }
        ClipCaptureManager::instance().setSourceWidget(gridHost);
    });
    clipSourceTimer->start();
}

void MainScreen::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    applyPendingEventViewRefresh();
}

void MainScreen::scheduleEventViewRefresh(const QVector<EventInfo> &events)
{
    const QString incomingHeadKey = eventHeadKey(events);
    const QString referenceHeadKey = m_eventViewRefreshPending
        ? eventHeadKey(m_pendingEventViewEvents)
        : m_lastFlushedEventViewHeadKey;

    if (!incomingHeadKey.isEmpty() && incomingHeadKey != referenceHeadKey) {
        ++m_eventViewPendingBurstCount;
    }

    m_pendingEventViewEvents = events;
    m_eventViewRefreshPending = true;

    if (!isVisible()) {
        return;
    }
    if (m_eventViewPendingBurstCount >= 5) {
        if (m_eventViewRefreshTimer) {
            m_eventViewRefreshTimer->stop();
        }
        applyPendingEventViewRefresh();
        return;
    }
    if (m_eventViewRefreshTimer) {
        m_eventViewRefreshTimer->start();
    } else {
        applyPendingEventViewRefresh();
    }
}

void MainScreen::applyPendingEventViewRefresh()
{
    if (!m_eventViewRefreshPending || !m_eventView) {
        return;
    }
    if (!isVisible()) {
        return;
    }

    m_eventView->setEvents(m_pendingEventViewEvents, 30, true);
    m_lastFlushedEventViewHeadKey = eventHeadKey(m_pendingEventViewEvents);
    m_eventViewPendingBurstCount = 0;
    m_eventViewRefreshPending = false;
}
