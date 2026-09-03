#include "ugv_screen.h"
#include "app_state.h"
#include "channel_session_manager.h"
#include "clip_capture_manager.h"
#include "common_ui.h"
#include "common_widgets.h"
#include "event_service.h"
#include "event_ui_helpers.h"
#include "popup_manager.h"
#include "ugv_service.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QDateTime>
#include <QFrame>
#include <QFutureWatcher>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QSet>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
constexpr int kUgvPanMin = 0;
constexpr int kUgvPanCenter = 90;
constexpr int kUgvPanMax = 180;
constexpr int kUgvTiltMin = 70;
constexpr int kUgvTiltCenter = 90;
constexpr int kUgvTiltMax = 180;
constexpr int kUgvPanStepPerTick = 2;
constexpr int kUgvTiltStepPerTick = 2;
constexpr int kUgvDriveDirectionUnit = 1;

class AspectVideoPane final : public QWidget
{
public:
    explicit AspectVideoPane(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("fullscreenVideoPane");
        m_viewport = new QFrame(this);
        m_viewport->setObjectName("fullscreenVideoHost");
    }

    QWidget *viewport() const
    {
        return m_viewport;
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        const int w = width();
        const int h = height();
        if (w <= 0 || h <= 0) {
            return;
        }

        int targetW = w;
        int targetH = (w * 9) / 16;
        if (targetH > h) {
            targetH = h;
            targetW = (h * 16) / 9;
        }
        const int x = (w - targetW) / 2;
        const int y = (h - targetH) / 2;
        m_viewport->setGeometry(x, y, targetW, targetH);
    }

private:
    QFrame *m_viewport = nullptr;
};

bool isDriveArrowKey(int key)
{
    return key == Qt::Key_Up
        || key == Qt::Key_Down
        || key == Qt::Key_Left
        || key == Qt::Key_Right;
}

bool isPtzWasdKey(int key)
{
    return key == Qt::Key_W
        || key == Qt::Key_A
        || key == Qt::Key_S
        || key == Qt::Key_D;
}

bool shouldBypassDriveKeyHandling(QObject *watched)
{
    if (!watched) {
        return false;
    }
    return qobject_cast<QAbstractItemView *>(watched)
        || qobject_cast<QAbstractSlider *>(watched)
        || qobject_cast<QAbstractSpinBox *>(watched)
        || qobject_cast<QAbstractButton *>(watched);
}

QString ugvSessionStateLabel(UgvService::SessionState state)
{
    switch (state) {
    case UgvService::SessionState::ConnectedUgv:
        return QStringLiteral("<span style=\"color:#18D98A;\">●</span> 연결됨");
    case UgvService::SessionState::SocketConnecting:
    case UgvService::SessionState::SocketConnected:
    case UgvService::SessionState::ConnectingUgv:
    case UgvService::SessionState::DisconnectingUgv:
        return QStringLiteral("<span style=\"color:#F5C542;\">●</span> 연결 중");
    case UgvService::SessionState::Error:
        return QStringLiteral("<span style=\"color:#FF5A52;\">●</span> 오류");
    case UgvService::SessionState::Disconnected:
    default:
        return QStringLiteral("<span style=\"color:#FF5A52;\">●</span> 연결 안 됨");
    }
}

} // namespace

UgvScreen::UgvScreen(QWidget *parent)
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
    auto unreadCount = std::make_shared<int>(EventUiHelpers::currentUnreadCount());
    applyNotificationUnreadState(topbar, *unreadCount > 0, *unreadCount);
    if (auto *service = EventUiHelpers::eventService()) {
        connect(service, &EventService::unreadChanged, this, [topbar, unreadCount](int count) {
            *unreadCount = count;
            applyNotificationUnreadState(topbar, count > 0, count);
        });
    }

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
    m_ugvTree = ugvTree;
    auto *playbackTree = sidebar->playbackTree();
    auto *treeStack = sidebar->treeStack();
    auto *snapshotButton = sidebar->primaryBottomButton();
    auto *clipButton = sidebar->secondaryBottomButton();
    auto *actionStatusLabel = sidebar->actionStatusLabel();
    m_clipButton = clipButton;
    m_actionStatusLabel = actionStatusLabel;

    sidebar->populateChannelTree();
    treeStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto *backButton = new QPushButton("← 멀티뷰로 돌아가기", sidebar);
    backButton->setObjectName("backLink");
    backButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    backButton->setFixedHeight(22);
    sidebar->controlsLayout()->addSpacing(12);
    sidebar->controlsLayout()->addWidget(backButton);
    sidebar->controlsLayout()->addSpacing(14);

    auto *ptzLabel = new QLabel("Pan / Tilt", sidebar);
    ptzLabel->setObjectName("cctvControlTitle");
    ptzLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    sidebar->controlsLayout()->addWidget(ptzLabel);
    sidebar->controlsLayout()->addSpacing(8);

    const QString iconUp(QChar(0x25B2));    // ▲
    const QString iconLeft(QChar(0x25C0));  // ◀
    const QString iconCenter(QChar(0x25CF)); // ●
    const QString iconRight(QChar(0x25B6)); // ▶
    const QString iconDown(QChar(0x25BC));  // ▼
    const QString iconStop(QChar(0x25A0));  // ■

    auto *dpadGrid = new QGridLayout();
    auto *dpadUp = new QToolButton(sidebar);
    auto *dpadLeft = new QToolButton(sidebar);
    auto *dpadCenter = new QToolButton(sidebar);
    auto *dpadRight = new QToolButton(sidebar);
    auto *dpadDown = new QToolButton(sidebar);
    m_dpadUpButton = dpadUp;
    m_dpadLeftButton = dpadLeft;
    m_dpadCenterButton = dpadCenter;
    m_dpadRightButton = dpadRight;
    m_dpadDownButton = dpadDown;
    dpadUp->setText(iconUp);
    dpadLeft->setText(iconLeft);
    dpadCenter->setText(iconCenter);
    dpadRight->setText(iconRight);
    dpadDown->setText(iconDown);
    dpadUp->setToolButtonStyle(Qt::ToolButtonTextOnly);
    dpadLeft->setToolButtonStyle(Qt::ToolButtonTextOnly);
    dpadCenter->setToolButtonStyle(Qt::ToolButtonTextOnly);
    dpadRight->setToolButtonStyle(Qt::ToolButtonTextOnly);
    dpadDown->setToolButtonStyle(Qt::ToolButtonTextOnly);
    dpadUp->setFixedSize(46, 30);
    dpadLeft->setFixedSize(46, 30);
    dpadCenter->setFixedSize(46, 30);
    dpadRight->setFixedSize(46, 30);
    dpadDown->setFixedSize(46, 30);
    dpadUp->setObjectName("ugvPadButton");
    dpadLeft->setObjectName("ugvPadButton");
    dpadCenter->setObjectName("ugvPadButton");
    dpadRight->setObjectName("ugvPadButton");
    dpadDown->setObjectName("ugvPadButton");
    dpadGrid->setContentsMargins(0, 0, 0, 0);
    dpadGrid->setHorizontalSpacing(4);
    dpadGrid->setVerticalSpacing(4);
    dpadGrid->addWidget(dpadUp, 0, 1);
    dpadGrid->addWidget(dpadLeft, 1, 0);
    dpadGrid->addWidget(dpadCenter, 1, 1);
    dpadGrid->addWidget(dpadRight, 1, 2);
    dpadGrid->addWidget(dpadDown, 2, 1);
    sidebar->controlsLayout()->addLayout(dpadGrid);

    auto *panRow = new QHBoxLayout();
    panRow->addWidget(new QLabel("Pan", sidebar));
    panRow->addSpacing(12);
    auto *panCurrentValue = new QLabel(QString::number(kUgvPanCenter), sidebar);
    panCurrentValue->setObjectName("ugvStatusValue");
    m_panCurrentValueLabel = panCurrentValue;
    panRow->addWidget(panCurrentValue);
    panRow->addStretch(1);
    auto *panSpin = new QSpinBox(sidebar);
    panSpin->setRange(kUgvPanMin, kUgvPanMax);
    panSpin->setValue(kUgvPanCenter);
    panSpin->setObjectName("ugvPtzSpin");
    m_panSpin = panSpin;
    panRow->addWidget(panSpin);
    sidebar->controlsLayout()->addLayout(panRow);

    auto *tiltRow = new QHBoxLayout();
    tiltRow->addWidget(new QLabel("Tilt", sidebar));
    tiltRow->addSpacing(12);
    auto *tiltCurrentValue = new QLabel(QString::number(kUgvTiltCenter), sidebar);
    tiltCurrentValue->setObjectName("ugvStatusValue");
    m_tiltCurrentValueLabel = tiltCurrentValue;
    tiltRow->addWidget(tiltCurrentValue);
    tiltRow->addStretch(1);
    auto *tiltSpin = new QSpinBox(sidebar);
    tiltSpin->setRange(kUgvTiltMin, kUgvTiltMax);
    tiltSpin->setValue(kUgvTiltCenter);
    tiltSpin->setObjectName("ugvPtzSpin");
    m_tiltSpin = tiltSpin;
    tiltRow->addWidget(tiltSpin);
    sidebar->controlsLayout()->addLayout(tiltRow);

    auto *content = new QFrame(this);
    content->setObjectName("centerPanel");
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto *splitTopWrap = new QWidget(content);
    splitTopWrap->setAutoFillBackground(true);
    auto *topWrapLayout = new QGridLayout(splitTopWrap);
    topWrapLayout->setContentsMargins(0, 0, 0, 0);
    topWrapLayout->setSpacing(0);
    auto *splitTop = new AspectVideoPane(splitTopWrap);
    m_ugvVideoViewport = splitTop->viewport();
    topWrapLayout->addWidget(splitTop, 0, 0);

    m_ugvInfoPanel = new QFrame(splitTopWrap);
    m_ugvInfoPanel->setObjectName("ugvInfoPanel");
    m_ugvInfoPanel->setMinimumWidth(238);
    m_ugvInfoPanel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto *ugvInfoLayout = new QGridLayout(m_ugvInfoPanel);
    ugvInfoLayout->setContentsMargins(12, 12, 12, 12);
    ugvInfoLayout->setHorizontalSpacing(10);
    ugvInfoLayout->setVerticalSpacing(5);

    auto addStatusRow = [this, ugvInfoLayout](int row, const QString &title, QLabel **outValue) {
        auto *titleLabel = new QLabel(title, m_ugvInfoPanel);
        titleLabel->setObjectName("ugvStatusKey");
        auto *valueLabel = new QLabel(QStringLiteral("--"), m_ugvInfoPanel);
        valueLabel->setObjectName("ugvStatusValue");
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueLabel->setWordWrap(false);
        ugvInfoLayout->addWidget(titleLabel, row, 0);
        ugvInfoLayout->addWidget(valueLabel, row, 1);
        if (outValue) {
            *outValue = valueLabel;
        }
    };
    addStatusRow(0, QStringLiteral("시간"), &m_ugvStatusTimestampValue);
    addStatusRow(1, QStringLiteral("연결"), &m_ugvStatusConnectionValue);
    addStatusRow(2, QStringLiteral("RSSI"), &m_ugvStatusRssiValue);
    addStatusRow(3, QStringLiteral("최근 수신 시각"), &m_ugvStatusFeedbackValue);
    auto *divider = new QFrame(m_ugvInfoPanel);
    divider->setObjectName("ugvInfoDivider");
    divider->setFixedHeight(1);
    ugvInfoLayout->addWidget(divider, 4, 0, 1, 2);
    addStatusRow(5, QStringLiteral("대상"), &m_ugvStatusTargetValue);
    addStatusRow(6, QStringLiteral("ID"), &m_ugvStatusIdsValue);
    ugvInfoLayout->setColumnStretch(1, 1);
    if (m_ugvStatusConnectionValue) {
        m_ugvStatusConnectionValue->setTextFormat(Qt::RichText);
    }
    m_ugvInfoPanel->adjustSize();
    m_ugvInfoPanel->raise();

    auto *splitBottomWrap = new QWidget(content);
    splitBottomWrap->setAutoFillBackground(true);
    auto *bottomWrapLayout = new QGridLayout(splitBottomWrap);
    bottomWrapLayout->setContentsMargins(18, 18, 18, 18);
    bottomWrapLayout->setSpacing(0);

    m_mapView = new QGraphicsView(splitBottomWrap);
    m_mapView->setObjectName("ugvMapView");
    m_mapView->setFrameShape(QFrame::NoFrame);
    m_mapView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_mapView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_mapView->setRenderHint(QPainter::Antialiasing, true);
    m_mapView->setRenderHint(QPainter::TextAntialiasing, true);
    m_mapScene = new QGraphicsScene(m_mapView);
    m_mapScene->setSceneRect(0, 0, 1600, 900);
    m_mapView->setScene(m_mapScene);
    m_mapScene->addRect(m_mapScene->sceneRect(), QPen(Qt::NoPen), QBrush(QColor("#202226")));
    for (int x = 0; x <= 1600; x += 100) {
        m_mapScene->addLine(x, 0, x, 900, QPen(QColor(255, 255, 255, 18), 1));
    }
    for (int y = 0; y <= 900; y += 100) {
        m_mapScene->addLine(0, y, 1600, y, QPen(QColor(255, 255, 255, 18), 1));
    }
    m_mapRoute = m_mapScene->addPath(
        QPainterPath(),
        QPen(QColor(95, 208, 165, 190), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    m_mapPulse = m_mapScene->addEllipse(
        -56, -56, 112, 112,
        QPen(Qt::NoPen),
        QBrush(QColor(126, 226, 205, 48)));
    m_mapMarker = m_mapScene->addEllipse(
        -16, -16, 32, 32,
        QPen(QColor("#F5F7FA"), 4),
        QBrush(QColor("#FF4D4F")));
    m_mapHeadingLine = m_mapScene->addLine(
        QLineF(),
        QPen(QColor(255, 77, 79, 0), 1, Qt::SolidLine, Qt::RoundCap));
    m_mapHeadingChevron = m_mapScene->addPolygon(
        QPolygonF({
            QPointF(0.0, -38.0),
            QPointF(11.0, -20.0),
            QPointF(-11.0, -20.0),
        }),
        QPen(QColor(245, 247, 250, 200), 2),
        QBrush(QColor("#FF4D4F")));
    m_mapHeadingChevron->setTransformOriginPoint(QPointF(0.0, 0.0));
    m_mapPulse->setZValue(1.0);
    m_mapRoute->setZValue(2.0);
    m_mapHeadingLine->setZValue(3.0);
    m_mapMarker->setZValue(4.0);
    m_mapHeadingChevron->setZValue(5.0);
    m_mapPulse->setVisible(false);
    m_mapMarker->setVisible(false);
    m_mapHeadingLine->setVisible(false);
    m_mapHeadingChevron->setVisible(false);
    // auto *mapLabel = m_mapScene->addText("UGV Route Map");
    // mapLabel->setDefaultTextColor(QColor("#EAEAEA"));
    // mapLabel->setPos(14, 10);
    bottomWrapLayout->addWidget(m_mapView, 0, 0);

    auto *missionEndButton = new QPushButton("출동 시작", splitBottomWrap);
    missionEndButton->setObjectName("primaryButton");
    missionEndButton->setMinimumSize(156, 44);
    m_sessionButton = missionEndButton;
    bottomWrapLayout->addWidget(missionEndButton, 0, 0, Qt::AlignLeft | Qt::AlignBottom);

    auto *driveBox = new QFrame(splitBottomWrap);
    driveBox->setObjectName("ugvDriveBox");
    auto *driveGrid = new QGridLayout(driveBox);
    driveGrid->setContentsMargins(14, 14, 14, 14);
    driveGrid->setSpacing(12);
    auto *driveUp = new QToolButton(driveBox);
    auto *driveLeft = new QToolButton(driveBox);
    auto *driveStop = new QToolButton(driveBox);
    auto *driveRight = new QToolButton(driveBox);
    auto *driveDown = new QToolButton(driveBox);
    m_driveUpButton = driveUp;
    m_driveLeftButton = driveLeft;
    m_driveStopButton = driveStop;
    m_driveRightButton = driveRight;
    m_driveDownButton = driveDown;
    driveUp->setText(iconUp);
    driveLeft->setText(iconLeft);
    driveStop->setText(iconStop);
    driveRight->setText(iconRight);
    driveDown->setText(iconDown);
    driveUp->setToolButtonStyle(Qt::ToolButtonTextOnly);
    driveLeft->setToolButtonStyle(Qt::ToolButtonTextOnly);
    driveStop->setToolButtonStyle(Qt::ToolButtonTextOnly);
    driveRight->setToolButtonStyle(Qt::ToolButtonTextOnly);
    driveDown->setToolButtonStyle(Qt::ToolButtonTextOnly);
    driveUp->setMinimumSize(56, 44);
    driveLeft->setMinimumSize(56, 44);
    driveStop->setMinimumSize(56, 44);
    driveRight->setMinimumSize(56, 44);
    driveDown->setMinimumSize(56, 44);
    driveUp->setObjectName("ugvDriveButton");
    driveLeft->setObjectName("ugvDriveButton");
    driveStop->setObjectName("ugvDriveButton");
    driveRight->setObjectName("ugvDriveButton");
    driveDown->setObjectName("ugvDriveButton");
    auto *speedLabel = new QLabel("SPD", driveBox);
    speedLabel->setObjectName("ugvStatusKey");
    auto *speedSlider = new QSlider(Qt::Vertical, driveBox);
    speedSlider->setObjectName("ugvDriveSpeedSlider");
    speedSlider->setRange(0, 3);
    speedSlider->setSingleStep(1);
    speedSlider->setPageStep(1);
    speedSlider->setTickPosition(QSlider::NoTicks);
    speedSlider->setTracking(true);
    speedSlider->setValue(2);
    speedSlider->setFixedHeight(156);
    m_driveSpeedSlider = speedSlider;
    m_driveSpeedLevel = 2;
    connect(speedSlider, &QSlider::valueChanged, this, [this](int value) {
        if (!m_driveSpeedSlider) {
            return;
        }
        m_driveSpeedLevel = qBound(0, value, 3);
    });

    driveGrid->addWidget(speedLabel, 0, 0, 1, 1, Qt::AlignHCenter | Qt::AlignBottom);
    driveGrid->addWidget(speedSlider, 1, 0, 3, 1, Qt::AlignHCenter | Qt::AlignVCenter);
    driveGrid->addWidget(driveUp, 1, 2);
    driveGrid->addWidget(driveLeft, 2, 1);
    driveGrid->addWidget(driveStop, 2, 2);
    driveGrid->addWidget(driveRight, 2, 3);
    driveGrid->addWidget(driveDown, 3, 2);
    driveGrid->setColumnStretch(0, 0);
    driveGrid->setColumnStretch(1, 0);
    driveGrid->setColumnStretch(2, 0);
    driveGrid->setColumnStretch(3, 0);

    m_ugvSnackbarFrame = new QFrame(this);
    m_ugvSnackbarFrame->setObjectName("ugvSnackbar");
    auto *snackbarLayout = new QHBoxLayout(m_ugvSnackbarFrame);
    snackbarLayout->setContentsMargins(10, 6, 8, 6);
    snackbarLayout->setSpacing(8);
    m_ugvSnackbarLabel = new QLabel("UGV event received", m_ugvSnackbarFrame);
    m_ugvSnackbarLabel->setObjectName("ugvSnackbarLabel");
    auto *snackbarCloseButton = new QPushButton("X", m_ugvSnackbarFrame);
    snackbarCloseButton->setObjectName("ugvSnackbarClose");
    snackbarCloseButton->setFixedSize(22, 22);
    snackbarLayout->addWidget(m_ugvSnackbarLabel, 1);
    snackbarLayout->addWidget(snackbarCloseButton);
    m_ugvSnackbarFrame->hide();
    auto *driveAnchor = new QWidget(splitBottomWrap);
    driveAnchor->setAttribute(Qt::WA_TranslucentBackground, true);
    auto *driveAnchorLayout = new QVBoxLayout(driveAnchor);
    driveAnchorLayout->setContentsMargins(0, 0, 8, 6);
    driveAnchorLayout->setSpacing(0);
    driveAnchorLayout->addWidget(driveBox, 0, Qt::AlignRight | Qt::AlignBottom);
    bottomWrapLayout->addWidget(driveAnchor, 0, 0, Qt::AlignRight | Qt::AlignBottom);

    auto *missionAnchor = new QWidget(splitBottomWrap);
    missionAnchor->setAttribute(Qt::WA_TranslucentBackground, true);
    auto *missionAnchorLayout = new QVBoxLayout(missionAnchor);
    missionAnchorLayout->setContentsMargins(8, 0, 0, 8);
    missionAnchorLayout->setSpacing(0);
    missionAnchorLayout->addWidget(missionEndButton, 0, Qt::AlignLeft | Qt::AlignBottom);
    bottomWrapLayout->addWidget(missionAnchor, 0, 0, Qt::AlignLeft | Qt::AlignBottom);

    placeSnackbar();

    contentLayout->addWidget(splitTopWrap, 1);
    contentLayout->addWidget(splitBottomWrap, 1);

    auto *sidebarSeparator = new QFrame(this);
    sidebarSeparator->setObjectName("sidebarSeparator");
    sidebarSeparator->setFixedWidth(1);

    body->addWidget(sidebar, 240);
    body->addWidget(sidebarSeparator);
    body->addWidget(content, 1680);
    root->addWidget(topbar);
    root->addLayout(body, 1);

    connect(channelTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (!item || item->childCount() > 0) {
            return;
        }
        const QString type = item->data(0, Qt::UserRole + 3).toString().trimmed().toUpper();
        if (type != QStringLiteral("CCTV")) {
            return;
        }
        const QString selected = item->data(0, Qt::UserRole + 2).toString().trimmed().isEmpty()
            ? item->text(0).trimmed()
            : item->data(0, Qt::UserRole + 2).toString().trimmed();
        auto &state = AppState::instance();
        state.activeChannel = selected;
        int cctvChannelId = item->data(0, Qt::UserRole).toInt();
        if (cctvChannelId <= 0) {
            cctvChannelId = selectedChannelIdForDisplayNameExact(selected, QStringLiteral("CCTV"));
        }
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
            if (type != QStringLiteral("UGV")) {
                return;
            }
            const QString selected = item->data(0, Qt::UserRole + 2).toString().trimmed().isEmpty()
                ? item->text(0).trimmed()
                : item->data(0, Qt::UserRole + 2).toString().trimmed();
            auto &state = AppState::instance();
            state.activeChannel = selected;
            state.activeCctvChannelId = -1;
            state.activeUgvChannelId = item->data(0, Qt::UserRole).toInt();
            refreshStream();
        });
    }
    connect(channelTab, &QPushButton::clicked, this, [channelTab, playbackTab, treeStack, channelPage]() {
        setSidebarTabState(channelTab, playbackTab, true);
        treeStack->setCurrentWidget(channelPage);
    });
    connect(playbackTab, &QPushButton::clicked, this, [channelTab, playbackTab, treeStack, playbackTree]() {
        setSidebarTabState(channelTab, playbackTab, false);
        treeStack->setCurrentWidget(playbackTree);
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
    auto applyAbsolutePtzFromInputs = [this]() {
        sendPtzCommand(m_panSpin ? m_panSpin->value() : kUgvPanCenter,
                       m_tiltSpin ? m_tiltSpin->value() : kUgvTiltCenter);
    };
    connect(panSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_panCurrentValueLabel) {
            m_panCurrentValueLabel->setText(QString::number(value));
        }
    });
    connect(tiltSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_tiltCurrentValueLabel) {
            m_tiltCurrentValueLabel->setText(QString::number(value));
        }
    });
    connect(panSpin, &QSpinBox::editingFinished, this, applyAbsolutePtzFromInputs);
    connect(tiltSpin, &QSpinBox::editingFinished, this, applyAbsolutePtzFromInputs);
    m_driveRepeatTimer = new QTimer(this);
    m_driveRepeatTimer->setInterval(50);
    connect(m_driveRepeatTimer, &QTimer::timeout, this, [this]() {
        if (m_activeDriveForward == 0 && m_activeDriveBack == 0
            && m_activeDriveLeft == 0 && m_activeDriveRight == 0) {
            return;
        }
        sendDriveCommand(m_activeDriveForward, m_activeDriveBack, m_activeDriveLeft, m_activeDriveRight, 260);
    });
    m_ptzRepeatTimer = new QTimer(this);
    m_ptzRepeatTimer->setInterval(50);
    connect(m_ptzRepeatTimer, &QTimer::timeout, this, [this]() {
        if (qFuzzyIsNull(m_activePanCommand) && qFuzzyIsNull(m_activeTiltCommand)) {
            return;
        }
        bool changed = false;
        if (m_panSpin && !qFuzzyIsNull(m_activePanCommand)) {
            const int nextPan = qBound(m_panSpin->minimum(),
                                       m_panSpin->value() + static_cast<int>(m_activePanCommand),
                                       m_panSpin->maximum());
            if (nextPan != m_panSpin->value()) {
                m_panSpin->setValue(nextPan);
                changed = true;
            }
        }
        if (m_tiltSpin && !qFuzzyIsNull(m_activeTiltCommand)) {
            const int nextTilt = qBound(m_tiltSpin->minimum(),
                                        m_tiltSpin->value() + static_cast<int>(m_activeTiltCommand),
                                        m_tiltSpin->maximum());
            if (nextTilt != m_tiltSpin->value()) {
                m_tiltSpin->setValue(nextTilt);
                changed = true;
            }
        }
        if (changed) {
            sendPtzCommand(m_panSpin ? m_panSpin->value() : kUgvPanCenter,
                           m_tiltSpin ? m_tiltSpin->value() : kUgvTiltCenter);
        }
    });
    connect(dpadUp, &QToolButton::pressed, this, [this]() { startPtzHold(0.0, static_cast<double>(kUgvTiltStepPerTick)); });
    connect(dpadDown, &QToolButton::pressed, this, [this]() { startPtzHold(0.0, -static_cast<double>(kUgvTiltStepPerTick)); });
    connect(dpadLeft, &QToolButton::pressed, this, [this]() { startPtzHold(static_cast<double>(kUgvPanStepPerTick), 0.0); });
    connect(dpadRight, &QToolButton::pressed, this, [this]() { startPtzHold(-static_cast<double>(kUgvPanStepPerTick), 0.0); });
    connect(dpadUp, &QToolButton::released, this, [this]() { stopPtzHold(); });
    connect(dpadDown, &QToolButton::released, this, [this]() { stopPtzHold(); });
    connect(dpadLeft, &QToolButton::released, this, [this]() { stopPtzHold(); });
    connect(dpadRight, &QToolButton::released, this, [this]() { stopPtzHold(); });
    connect(dpadCenter, &QToolButton::clicked, this, [this]() {
        stopPtzHold();
        if (m_panSpin) {
            m_panSpin->setValue(kUgvPanCenter);
        }
        if (m_tiltSpin) {
            m_tiltSpin->setValue(kUgvTiltCenter);
        }
        sendPtzCommand(kUgvPanCenter, kUgvTiltCenter);
    });
    connect(driveUp, &QToolButton::pressed, this, [this]() {
        startDriveHold(kUgvDriveDirectionUnit, 0, 0, 0);
    });
    connect(driveDown, &QToolButton::pressed, this, [this]() {
        startDriveHold(0, kUgvDriveDirectionUnit, 0, 0);
    });
    connect(driveLeft, &QToolButton::pressed, this, [this]() {
        startDriveHold(0, 0, kUgvDriveDirectionUnit, 0);
    });
    connect(driveRight, &QToolButton::pressed, this, [this]() {
        startDriveHold(0, 0, 0, kUgvDriveDirectionUnit);
    });
    connect(driveStop, &QToolButton::clicked, this, [this]() {
        stopDriveHold();
        sendDriveCommand(0, 0, 0, 0, 120);
    });
    connect(driveUp, &QToolButton::released, this, [this]() { stopDriveHold(); });
    connect(driveDown, &QToolButton::released, this, [this]() { stopDriveHold(); });
    connect(driveLeft, &QToolButton::released, this, [this]() { stopDriveHold(); });
    connect(driveRight, &QToolButton::released, this, [this]() { stopDriveHold(); });
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
    connect(missionEndButton, &QPushButton::clicked, this, [this]() {
        if (!m_ugvService) {
            showActionStatus(m_actionStatusLabel, "UGV 서비스가 아직 준비되지 않았습니다.", "error", 2200);
            return;
        }
        UgvTarget target;
        if (!resolveCurrentUgvTarget(&target)) {
            showActionStatus(m_actionStatusLabel, "선택된 UGV 채널을 찾을 수 없습니다.", "error", 2200);
            return;
        }
        const auto state = m_ugvService->sessionState();
        if (state == UgvService::SessionState::ConnectedUgv
            || state == UgvService::SessionState::ConnectingUgv
            || state == UgvService::SessionState::SocketConnecting
            || state == UgvService::SessionState::SocketConnected) {
            if (!PopupManager::confirm(this, "임무 종료", "임무를 종료하고 멀티뷰로 돌아가시겠습니까?")) {
                return;
            }
            m_skipDisconnectOnHide = true;
            m_ugvService->disconnectUgv(target.gatewayId, target.ugvId);
            emit missionEndRequested();
            return;
        }
        m_ugvService->connectUgv(target.gatewayId, target.ugvId);
    });
    connect(backButton, &QPushButton::clicked, this, &UgvScreen::backToMainRequested);
    connect(topbar, &TopbarWidget::settingsClicked, this, &UgvScreen::settingsRequested, Qt::UniqueConnection);
    connect(topbar, &TopbarWidget::logoutClicked, this, &UgvScreen::logoutRequested, Qt::UniqueConnection);
    connect(topbar, &TopbarWidget::notificationCenterClicked, this, [this, topbar, unreadCount]() {
        if (auto *service = EventUiHelpers::eventService()) {
            service->markAllRead();
        }
        *unreadCount = EventUiHelpers::currentUnreadCount();
        openNotificationCenterDialog(this, topbar);
    });
    connect(snackbarCloseButton, &QPushButton::clicked, this, &UgvScreen::clearSnackbar);

    setSidebarTabState(channelTab, playbackTab, true);
    driveUp->setEnabled(false);
    driveLeft->setEnabled(false);
    driveStop->setEnabled(false);
    driveRight->setEnabled(false);
    driveDown->setEnabled(false);
    dpadUp->setEnabled(false);
    dpadLeft->setEnabled(false);
    dpadCenter->setEnabled(false);
    dpadRight->setEnabled(false);
    dpadDown->setEnabled(false);
    panSpin->setEnabled(false);
    tiltSpin->setEnabled(false);
    if (speedSlider) {
        speedSlider->setEnabled(false);
    }
    missionEndButton->setProperty("danger", true);
    missionEndButton->style()->unpolish(missionEndButton);
    missionEndButton->style()->polish(missionEndButton);

    m_osdTimer = new QTimer(this);
    m_osdTimer->setInterval(1000);
    connect(m_osdTimer, &QTimer::timeout, this, &UgvScreen::refreshOsd);
    m_snackbarHideTimer = new QTimer(this);
    m_snackbarHideTimer->setSingleShot(true);
    m_snackbarHideTimer->setInterval(3000);
    connect(m_snackbarHideTimer, &QTimer::timeout, this, &UgvScreen::clearSnackbar);
    m_telemetryRefreshTimer = new QTimer(this);
    m_telemetryRefreshTimer->setSingleShot(true);
    m_telemetryRefreshTimer->setInterval(120);
    connect(m_telemetryRefreshTimer, &QTimer::timeout, this, &UgvScreen::applyTelemetryUiRefresh);
    if (m_ugvService) {
        setUgvService(m_ugvService);
    }
    setFocusPolicy(Qt::StrongFocus);
    setFocus(Qt::OtherFocusReason);
    installEventFilter(this);
    const auto childWidgets = findChildren<QWidget *>();
    for (QWidget *child : childWidgets) {
        if (child) {
            child->installEventFilter(this);
        }
    }
    updateSessionUi();
    refreshOsd();
}

bool UgvScreen::eventFilter(QObject *watched, QEvent *event)
{
    if (!event || !isVisible()) {
        return QWidget::eventFilter(watched, event);
    }
    if (shouldBypassDriveKeyHandling(watched)) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (handlePtzKeyPress(keyEvent->key(), keyEvent->isAutoRepeat())) {
            return true;
        }
        if (handleDriveKeyPress(keyEvent->key(), keyEvent->isAutoRepeat())) {
            return true;
        }
    } else if (event->type() == QEvent::KeyRelease) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (handlePtzKeyRelease(keyEvent->key(), keyEvent->isAutoRepeat())) {
            return true;
        }
        if (handleDriveKeyRelease(keyEvent->key(), keyEvent->isAutoRepeat())) {
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void UgvScreen::keyPressEvent(QKeyEvent *event)
{
    if (event && handlePtzKeyPress(event->key(), event->isAutoRepeat())) {
        event->accept();
        return;
    }
    if (event && handleDriveKeyPress(event->key(), event->isAutoRepeat())) {
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void UgvScreen::keyReleaseEvent(QKeyEvent *event)
{
    if (event && handlePtzKeyRelease(event->key(), event->isAutoRepeat())) {
        event->accept();
        return;
    }
    if (event && handleDriveKeyRelease(event->key(), event->isAutoRepeat())) {
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

bool UgvScreen::handleDriveKeyPress(int key, bool isAutoRepeat)
{
    if (key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
        if (isAutoRepeat) {
            return true;
        }
        const int delta = (key == Qt::Key_PageUp) ? 1 : -1;
        m_driveSpeedLevel = qBound(0, m_driveSpeedLevel + delta, 3);
        if (m_driveSpeedSlider) {
            m_driveSpeedSlider->setValue(m_driveSpeedLevel);
        }
        return true;
    }

    if (!isDriveArrowKey(key)) {
        return false;
    }
    if (!m_ugvService || m_ugvService->sessionState() != UgvService::SessionState::ConnectedUgv) {
        return false;
    }
    if (isAutoRepeat) {
        return true;
    }

    m_pressedDriveKeys.removeAll(key);
    m_pressedDriveKeys.append(key);
    dispatchDriveForArrowKey(key);
    return true;
}

bool UgvScreen::handlePtzKeyPress(int key, bool isAutoRepeat)
{
    if (!isPtzWasdKey(key)) {
        return false;
    }
    if (!m_ugvService || m_ugvService->sessionState() != UgvService::SessionState::ConnectedUgv) {
        return false;
    }
    if (isAutoRepeat) {
        return true;
    }

    m_pressedPtzKeys.removeAll(key);
    m_pressedPtzKeys.append(key);
    dispatchPtzForWasdKey(key);
    return true;
}

bool UgvScreen::handleDriveKeyRelease(int key, bool isAutoRepeat)
{
    if (!isDriveArrowKey(key)) {
        return false;
    }
    if (isAutoRepeat) {
        return true;
    }

    m_pressedDriveKeys.removeAll(key);
    if (!m_ugvService || m_ugvService->sessionState() != UgvService::SessionState::ConnectedUgv) {
        // Session changed while a drive key was held. Drop stale key state.
        m_pressedDriveKeys.clear();
        return false;
    }
    if (m_pressedDriveKeys.isEmpty()) {
        stopDriveHold();
    } else {
        dispatchDriveForArrowKey(m_pressedDriveKeys.constLast());
    }
    return true;
}

bool UgvScreen::handlePtzKeyRelease(int key, bool isAutoRepeat)
{
    if (!isPtzWasdKey(key)) {
        return false;
    }
    if (isAutoRepeat) {
        return true;
    }

    m_pressedPtzKeys.removeAll(key);
    if (!m_ugvService || m_ugvService->sessionState() != UgvService::SessionState::ConnectedUgv) {
        m_pressedPtzKeys.clear();
        return false;
    }
    if (m_pressedPtzKeys.isEmpty()) {
        stopPtzHold();
    } else {
        dispatchPtzForWasdKey(m_pressedPtzKeys.constLast());
    }
    return true;
}

void UgvScreen::dispatchDriveForArrowKey(int key)
{
    if (m_driveUpButton) {
        m_driveUpButton->setDown(key == Qt::Key_Up);
    }
    if (m_driveDownButton) {
        m_driveDownButton->setDown(key == Qt::Key_Down);
    }
    if (m_driveLeftButton) {
        m_driveLeftButton->setDown(key == Qt::Key_Left);
    }
    if (m_driveRightButton) {
        m_driveRightButton->setDown(key == Qt::Key_Right);
    }
    switch (key) {
    case Qt::Key_Up:
        startDriveHold(kUgvDriveDirectionUnit, 0, 0, 0);
        break;
    case Qt::Key_Down:
        startDriveHold(0, kUgvDriveDirectionUnit, 0, 0);
        break;
    case Qt::Key_Left:
        startDriveHold(0, 0, kUgvDriveDirectionUnit, 0);
        break;
    case Qt::Key_Right:
        startDriveHold(0, 0, 0, kUgvDriveDirectionUnit);
        break;
    default:
        stopDriveHold();
        break;
    }
}

void UgvScreen::dispatchPtzForWasdKey(int key)
{
    if (m_dpadUpButton) {
        m_dpadUpButton->setDown(key == Qt::Key_W);
    }
    if (m_dpadDownButton) {
        m_dpadDownButton->setDown(key == Qt::Key_S);
    }
    if (m_dpadLeftButton) {
        m_dpadLeftButton->setDown(key == Qt::Key_A);
    }
    if (m_dpadRightButton) {
        m_dpadRightButton->setDown(key == Qt::Key_D);
    }
    switch (key) {
    case Qt::Key_W:
        startPtzHold(0.0, static_cast<double>(kUgvTiltStepPerTick));
        break;
    case Qt::Key_S:
        startPtzHold(0.0, -static_cast<double>(kUgvTiltStepPerTick));
        break;
    case Qt::Key_A:
        startPtzHold(static_cast<double>(kUgvPanStepPerTick), 0.0);
        break;
    case Qt::Key_D:
        startPtzHold(-static_cast<double>(kUgvPanStepPerTick), 0.0);
        break;
    default:
        stopPtzHold();
        break;
    }
}

void UgvScreen::refreshSidebarStatus()
{
    // 정보 패널/사이드바에 보여주는 UGV 상태는 여러 출처를 섞어 만든다.
    // 현재 target, session state, 마지막 telemetry를 읽어 연결/최근 수신 시각을 표시한다.
    // 세션 상태는 UgvService, 대상/ID는 현재 선택 컨텍스트,
    // 최근 수신 시각은 마지막 telemetry 캐시를 기준으로 그려
    // "지금 조작 가능한가 + 지금 데이터가 들어오는가"를 한 번에 보여준다.
    UgvTarget target;
    const bool hasTarget = resolveCurrentUgvTarget(&target);
    const auto state = m_ugvService ? m_ugvService->sessionState() : UgvService::SessionState::Disconnected;

    highlightSidebarChannelItem(m_ugvTree, hasTarget ? target.displayName : QString());

    if (m_ugvStatusConnectionValue) {
        m_ugvStatusConnectionValue->setText(ugvSessionStateLabel(state));
    }
    if (m_ugvStatusTargetValue) {
        m_ugvStatusTargetValue->setText(hasTarget ? target.displayName : QStringLiteral("--"));
    }
    if (m_ugvStatusRssiValue) {
        QString rssiText = QStringLiteral("--");
        if (m_hasRssiTelemetry) {
            rssiText = QString::number(m_lastRssiTelemetry.rssiDbm);
            if (!m_lastRssiTelemetry.linkType.trimmed().isEmpty()) {
                rssiText = QStringLiteral("%1 (%2)").arg(rssiText, m_lastRssiTelemetry.linkType.trimmed());
            }
        }
        m_ugvStatusRssiValue->setText(rssiText);
    }
    if (m_ugvStatusIdsValue) {
        m_ugvStatusIdsValue->setText(
            hasTarget ? QStringLiteral("GW %1 / UGV %2").arg(target.gatewayId).arg(target.ugvId) : QStringLiteral("--"));
    }
    if (m_ugvStatusFeedbackValue) {
        QString lastFeedback;
        if (m_hasGpsTelemetry && !m_lastGpsTelemetry.ts.trimmed().isEmpty()) {
            lastFeedback = m_lastGpsTelemetry.ts.trimmed();
        } else if (m_hasRssiTelemetry && !m_lastRssiTelemetry.ts.trimmed().isEmpty()) {
            lastFeedback = m_lastRssiTelemetry.ts.trimmed();
        }
        m_ugvStatusFeedbackValue->setText(lastFeedback.isEmpty() ? QStringLiteral("--") : lastFeedback);
    }
}

void UgvScreen::setUgvService(UgvService *service)
{
    // UgvScreen은 서비스를 소유하지 않고 주입받는다.
    // 새 service의 세션/telemetry/error signal을 화면 갱신 로직에 다시 연결한다.
    // 따라서 이 함수는 단순 포인터 교체가 아니라:
    // - 이전 service signal disconnect
    // - 새 service signal 재연결
    // - telemetry/UI 갱신 파이프 재구성
    // 을 담당하는 화면-서비스 결합 지점이다.
    if (m_ugvService == service) {
        return;
    }
    if (m_ugvService) {
        disconnect(m_ugvService, nullptr, this, nullptr);
    }
    m_ugvService = service;
    if (!m_ugvService) {
        updateSessionUi();
        return;
    }

    connect(m_ugvService, &UgvService::sessionStateChanged, this, [this](UgvService::SessionState) {
        updateSessionUi();
    });
    connect(m_ugvService, &UgvService::gpsUpdated, this, [this](const UgvGpsTelemetry &telemetry) {
        m_hasGpsTelemetry = true;
        m_lastGpsTelemetry = telemetry;
        scheduleTelemetryUiRefresh();
    });
    connect(m_ugvService, &UgvService::rssiUpdated, this, [this](const UgvRssiTelemetry &telemetry) {
        m_hasRssiTelemetry = true;
        m_lastRssiTelemetry = telemetry;
        scheduleTelemetryUiRefresh();
    });
    connect(m_ugvService, &UgvService::serviceError, this, [this](const QString &message) {
        updateSessionUi();
        presentServiceError(message);
    });
    updateSessionUi();
}

void UgvScreen::scheduleTelemetryUiRefresh()
{
    if (!isVisible()) {
        return;
    }
    if (m_telemetryRefreshTimer && !m_telemetryRefreshTimer->isActive()) {
        m_telemetryRefreshTimer->start();
    }
}

void UgvScreen::applyTelemetryUiRefresh()
{
    if (!isVisible()) {
        return;
    }
    if (m_hasGpsTelemetry) {
        updateMapTelemetry(m_lastGpsTelemetry);
    }
    refreshOsd();
}

void UgvScreen::setMapBounds(double minLat, double maxLat, double minLon, double maxLon)
{
    if (minLat < maxLat) {
        m_mapMinLat = minLat;
        m_mapMaxLat = maxLat;
    }
    if (minLon < maxLon) {
        m_mapMinLon = minLon;
        m_mapMaxLon = maxLon;
    }
}

void UgvScreen::prepareForShutdown()
{
    m_skipDisconnectOnHide = true;
    clearSnackbar();
}

void UgvScreen::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_skipDisconnectOnHide = false;
    setFocus(Qt::OtherFocusReason);
    if (m_clipButton) {
        auto &clipMgr = ClipCaptureManager::instance();
        if (clipMgr.isRecording()) {
            m_clipButton->setText(QString("⏹ %1").arg(formatClipDuration(clipMgr.elapsedSeconds())));
        } else if (clipMgr.isEncoding()) {
            m_clipButton->setText("⏳ 인코딩");
        } else {
            m_clipButton->setText("⏺ Clip");
        }
    }
    if (m_clipButton) {
        m_clipButton->setText(clipButtonText());
    }
    refreshStream();
    updateSessionUi();
    if (ClipCaptureManager::instance().isRecording()) {
        const QString clipChannel = m_boundChannel.isEmpty() ? AppState::instance().activeChannel : m_boundChannel;
        ClipCaptureManager::instance().setSourceChannel(clipChannel);
    }
    refreshOsd();
    if (m_mapView && m_mapScene) {
        m_mapView->fitInView(m_mapScene->sceneRect(), Qt::KeepAspectRatio);
    }
    if (m_osdTimer) {
        m_osdTimer->start();
    }
}

void UgvScreen::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_pressedDriveKeys.clear();
    sendDriveCommand(0, 0, 0, 0);
    stopPtzHold();
    resetTelemetryAndMapState(true);
    if (m_ugvService) {
        if (m_skipDisconnectOnHide) {
            m_skipDisconnectOnHide = false;
        } else {
            UgvTarget target;
            if (resolveCurrentUgvTarget(&target)) {
                m_ugvService->disconnectUgv(target.gatewayId, target.ugvId);
            } else {
                m_ugvService->shutdown();
            }
        }
    }
    if (!m_boundChannel.isEmpty()) {
        ChannelSessionManager::instance().unbindChannelFromWidget(m_boundChannel, m_ugvVideoViewport);
        m_boundChannel.clear();
    }
    if (m_osdTimer) {
        m_osdTimer->stop();
    }
    clearSnackbar();
}

void UgvScreen::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_mapView && m_mapScene) {
        m_mapView->fitInView(m_mapScene->sceneRect(), Qt::KeepAspectRatio);
    }
    placeInfoPanel();
    refreshOsd();
    placeSnackbar();
}

void UgvScreen::refreshOsd()
{
    if (m_ugvStatusTimestampValue) {
        const QString ts = m_hasGpsTelemetry && !m_lastGpsTelemetry.ts.trimmed().isEmpty()
            ? m_lastGpsTelemetry.ts.trimmed()
            : QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        m_ugvStatusTimestampValue->setText(ts);
    }
    refreshSidebarStatus();
    placeInfoPanel();
}

void UgvScreen::placeInfoPanel()
{
    if (!m_ugvInfoPanel || !m_ugvVideoViewport) {
        return;
    }

    QWidget *container = m_ugvInfoPanel->parentWidget();
    if (!container) {
        return;
    }

    const QPoint origin = m_ugvVideoViewport->mapTo(container, QPoint(0, 0));
    const int maxWidth = std::max(220, m_ugvVideoViewport->width() - 24);
    m_ugvInfoPanel->setMaximumWidth(maxWidth);
    m_ugvInfoPanel->adjustSize();
    int x = origin.x() - m_ugvInfoPanel->width() - 4;
    if (x < 12) {
        x = 12;
    }
    int y = origin.y() + 12;
    const int maxY = std::max(12, container->height() - m_ugvInfoPanel->height() - 12);
    if (y > maxY) {
        y = maxY;
    }
    m_ugvInfoPanel->move(x, y);
    m_ugvInfoPanel->raise();
}

void UgvScreen::resetTelemetryAndMapState(bool resetPtzToCenter)
{
    m_hasGpsTelemetry = false;
    m_lastGpsTelemetry = UgvGpsTelemetry{};
    m_hasRssiTelemetry = false;
    m_lastRssiTelemetry = UgvRssiTelemetry{};
    m_routePoints.clear();
    m_hasLastMapPoint = false;
    m_lastMapPoint = QPointF();
    m_hasEstimatedHeading = false;
    m_estimatedHeadingDeg = 0.0;

    if (m_mapRoute) {
        m_mapRoute->setPath(QPainterPath());
    }
    if (m_mapPulse) {
        m_mapPulse->setVisible(false);
    }
    if (m_mapMarker) {
        m_mapMarker->setVisible(false);
    }
    if (m_mapHeadingLine) {
        m_mapHeadingLine->setVisible(false);
    }
    if (m_mapHeadingChevron) {
        m_mapHeadingChevron->setVisible(false);
    }

    if (resetPtzToCenter) {
        if (m_panSpin) {
            m_panSpin->setValue(kUgvPanCenter);
        }
        if (m_tiltSpin) {
            m_tiltSpin->setValue(kUgvTiltCenter);
        }
        if (m_panCurrentValueLabel) {
            m_panCurrentValueLabel->setText(QString::number(kUgvPanCenter));
        }
        if (m_tiltCurrentValueLabel) {
            m_tiltCurrentValueLabel->setText(QString::number(kUgvTiltCenter));
        }
    }

    refreshOsd();
}

void UgvScreen::refreshStream()
{
    // UGV 화면도 결국 ChannelSessionManager를 통해 영상을 본다.
    // 현재 UGV target을 해석해 AppState를 맞추고, 대응하는 channel을 video viewport에 bind한다.
    // 이 함수는 현재 active UGV target을 찾아 AppState를 동기화하고,
    // 그 target의 channel을 viewport에 bind한다.
    // 대상이 바뀌면 map/telemetry 캐시도 함께 리셋해 이전 UGV 상태가 남지 않게 한다.
    if (!m_ugvVideoViewport) {
        return;
    }

    UgvTarget target;
    if (!resolveCurrentUgvTarget(&target)) {
        ChannelSessionManager::instance().applyActiveChannels({});
        if (!m_boundChannel.isEmpty()) {
            ChannelSessionManager::instance().unbindChannelFromWidget(m_boundChannel, m_ugvVideoViewport);
            m_boundChannel.clear();
        }
        refreshSidebarStatus();
        return;
    }

    const QString channel = target.displayName;
    auto &state = AppState::instance();
    state.activeChannel = channel;
    state.activeUgvChannelId = target.ugvId;
    state.activeUgvGatewayId = target.gatewayId;
    ChannelSessionManager::instance().applyActiveChannels(QSet<QString>{channel});
    const QString url = rtspUrlForChannelId(target.ugvId);
    if (url.isEmpty()) {
        if (!m_boundChannel.isEmpty()) {
            ChannelSessionManager::instance().unbindChannelFromWidget(m_boundChannel, m_ugvVideoViewport);
            m_boundChannel.clear();
        }
        return;
    }

    if (!m_boundChannel.isEmpty() && m_boundChannel != channel) {
        ChannelSessionManager::instance().unbindChannelFromWidget(m_boundChannel, m_ugvVideoViewport);
        resetTelemetryAndMapState(false);
    }

    m_ugvVideoViewport->setAttribute(Qt::WA_NativeWindow);
    m_ugvVideoViewport->winId();
    ChannelSessionManager::instance().bindChannelToWidget(channel, m_ugvVideoViewport);
    m_boundChannel = channel;
    if (ClipCaptureManager::instance().isRecording()) {
        ClipCaptureManager::instance().setSourceChannel(m_boundChannel);
    }
    refreshSidebarStatus();
}

bool UgvScreen::resolveCurrentUgvTarget(UgvTarget *target) const
{
    // UGV 대상은 activeUgvChannelId가 있으면 그 값을 우선하고,
    // 없으면 선택된 채널 목록에서 첫 번째 UGV 채널을 찾아 gatewayId/ugvId/displayName을 채운다.
    // 없으면 선택된 채널 목록에서 첫 UGV를 fallback으로 찾는다.
    // 화면 진입 경로가 메인 dispatch / direct re-entry / logout recovery로 갈릴 수 있어서
    // 한 군데에서 target 해석 규칙을 통일하는 것이 중요하다.
    if (target) {
        *target = UgvTarget{};
    }

    const auto &state = AppState::instance();
    int channelId = state.activeUgvChannelId;
    SelectedChannelContext ctx;
    if (channelId >= 0 && findSelectedChannelContextByChannelId(channelId, &ctx)
        && ctx.deviceType.trimmed().compare(QStringLiteral("UGV"), Qt::CaseInsensitive) == 0
        && ctx.deviceId > 0) {
        if (target) {
            target->gatewayId = ctx.deviceId;
            target->ugvId = ctx.channelId;
            target->displayName = ctx.displayName.trimmed();
        }
        return true;
    }

    if (channelId < 0) {
        channelId = firstSelectedChannelIdByType(QStringLiteral("UGV"));
    }
    if (channelId < 0 || !findSelectedChannelContextByChannelId(channelId, &ctx)) {
        return false;
    }
    if (ctx.deviceType.trimmed().compare(QStringLiteral("UGV"), Qt::CaseInsensitive) != 0
        || ctx.deviceId <= 0 || ctx.channelId <= 0) {
        return false;
    }

    if (target) {
        target->gatewayId = ctx.deviceId;
        target->ugvId = ctx.channelId;
        target->displayName = ctx.displayName.trimmed();
    }
    return true;
}

bool UgvScreen::confirmNavigationAwayFromActiveMission()
{
    // UGV는 다른 런타임 화면과 달리 "화면을 숨기면 연결이 끊기는" 정책이 있다.
    // 현재 세션이 살아 있을 때 다른 화면으로 이동하려 하면 먼저 사용자 확인을 받아 disconnect 여부를 결정한다.
    // 그래서 미션/세션이 살아 있는 동안 다른 화면으로 이동하려 할 때는
    // 사용자가 disconnect를 인지하고 넘길 수 있도록 여기서 확인을 맡는다.
    if (!m_ugvService) {
        return true;
    }

    const auto state = m_ugvService->sessionState();
    const bool hasActiveMission = (state == UgvService::SessionState::ConnectedUgv
                                   || state == UgvService::SessionState::ConnectingUgv
                                   || state == UgvService::SessionState::SocketConnecting
                                   || state == UgvService::SessionState::SocketConnected
                                   || state == UgvService::SessionState::DisconnectingUgv);
    if (!hasActiveMission) {
        return true;
    }

    if (!PopupManager::confirm(this, "화면 이동", "UGV 연결을 종료하고 화면을 이동하시겠습니까?")) {
        return false;
    }

    m_skipDisconnectOnHide = true;
    UgvTarget target;
    if (resolveCurrentUgvTarget(&target)) {
        m_ugvService->disconnectUgv(target.gatewayId, target.ugvId);
    } else {
        m_ugvService->shutdown();
    }
    return true;
}

