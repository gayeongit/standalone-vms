#include "mainwindow.h"

#include "mainwindow_internal.h"

#include "clip_capture_manager.h"
#include "app_config_loader.h"
#include "channel_session_manager.h"
#include "common_ui.h"
#include "common_widgets.h"
#include "cctv_control_service.h"
#include "device_service.h"
#include "event_service.h"
#include "event_ui_helpers.h"
#include "auth_service.h"
#include "playback_service.h"
#include "popup_manager.h"
#include "rest_client.h"
#include "settings_dialog.h"
#include "login_screen.h"
#include "main_screen.h"
#include "cctv_screen.h"
#include "ugv_screen.h"
#include "playback_screen.h"
#include "theme_loader.h"
#include "ugv_service.h"
#include "video_render_widget.h"
#include "ws_client.h"

#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QSharedPointer>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSet>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace MainWindowInternal {

constexpr const char *kSettingsOrg = "TeamClue";
constexpr const char *kSettingsAppV2 = "VMS_v2";

int toStackIndex(ScreenId screenId)
{
    switch (screenId) {
    case ScreenId::Login:
        return 0;
    case ScreenId::Signup:
        return 1;
    case ScreenId::DeviceCheck:
        return 2;
    case ScreenId::Main:
        return 3;
    case ScreenId::Cctv:
        return 4;
    case ScreenId::Ugv:
        return 5;
    case ScreenId::Playback:
        return 6;
    }
    return 0;
}

bool isCompactScreen(ScreenId screenId)
{
    return (screenId == ScreenId::Login
            || screenId == ScreenId::Signup
            || screenId == ScreenId::DeviceCheck);
}

bool shouldTrackFirstFrameForScreen(ScreenId screenId)
{
    return (screenId == ScreenId::Main
            || screenId == ScreenId::Cctv
            || screenId == ScreenId::Ugv
            || screenId == ScreenId::Playback);
}

QStringList configuredDeviceNames()
{
    QStringList names;
    const auto &contexts = AppState::instance().selectedChannelContexts;
    names.reserve(contexts.size());
    for (const auto &ctx : contexts) {
        const QString name = ctx.displayName.trimmed();
        if (!name.isEmpty() && !names.contains(name)) {
            names.push_back(name);
        }
    }
    return names;
}

QVector<SelectedChannelContext> normalizeSelectedContextsForRuntime(const QVector<SelectedChannelContext> &contexts)
{
    QVector<SelectedChannelContext> out = contexts;
    QMap<QString, int> nameCounts;
    for (const auto &ctx : contexts) {
        const QString baseName = ctx.displayName.trimmed();
        if (!baseName.isEmpty()) {
            nameCounts[baseName] += 1;
        }
    }

    QMap<QString, int> seenPerName;
    for (SelectedChannelContext &ctx : out) {
        const QString baseName = ctx.displayName.trimmed();
        if (baseName.isEmpty()) {
            continue;
        }
        if (nameCounts.value(baseName) <= 1) {
            ctx.displayName = baseName;
            continue;
        }

        seenPerName[baseName] += 1;
        const int suffixIndex = seenPerName.value(baseName);
        if (ctx.deviceId >= 0) {
            ctx.displayName = QString("%1 [%2]").arg(baseName).arg(ctx.deviceId);
        } else {
            ctx.displayName = QString("%1 [%2]").arg(baseName).arg(suffixIndex);
        }
    }
    return out;
}

QString screenIdName(ScreenId screenId)
{
    switch (screenId) {
    case ScreenId::Login:
        return QStringLiteral("login");
    case ScreenId::Signup:
        return QStringLiteral("signup");
    case ScreenId::DeviceCheck:
        return QStringLiteral("device_check");
    case ScreenId::Main:
        return QStringLiteral("main");
    case ScreenId::Cctv:
        return QStringLiteral("cctv");
    case ScreenId::Ugv:
        return QStringLiteral("ugv");
    case ScreenId::Playback:
        return QStringLiteral("playback");
    }
    return QStringLiteral("unknown");
}

void applyDeviceChangesToRuntimeState(AppState &state)
{
    const QStringList valid = configuredDeviceNames();

    // Settings(디바이스 관리)에서 변경한 목록을 화면 선택 목록에 즉시 반영한다.

    // 기존 셀 매핑 중 유효하지 않은 채널은 제거한다.
    for (std::size_t i = 0; i < state.gridCells.size(); ++i) {
        const QString cellName = state.gridCells[i].displayName;
        if (!cellName.isEmpty() && !valid.contains(cellName)) {
            state.clearGridCell(i);
        } else if (cellName.isEmpty()) {
            state.clearGridCell(i);
        }
    }

    // 멀티뷰 반영: 비어있는 셀을 디바이스 목록 순서로 채운다.
    int fillIndex = 0;
    for (std::size_t i = 0; i < state.gridCells.size(); ++i) {
        if (!state.gridCells[i].isEmpty()) {
            continue;
        }
        while (fillIndex < valid.size()
               && std::any_of(state.gridCells.begin(),
                              state.gridCells.end(),
                              [&valid, fillIndex](const MainGridCellState &cell) {
                                  return cell.displayName == valid[fillIndex];
                              })) {
            ++fillIndex;
        }
        if (fillIndex >= valid.size()) {
            break;
        }
        const QString cellName = valid[fillIndex];
        const int channelId = selectedChannelIdForDisplayName(cellName);
        state.setGridCell(i, cellName, channelId, deviceIdForChannelId(channelId));
        ++fillIndex;
    }

    if (!state.activeChannel.isEmpty() && !valid.contains(state.activeChannel)) {
        state.activeChannel.clear();
    }
    if (state.activeChannel.isEmpty() && !valid.isEmpty()) {
        state.activeChannel = valid.first();
    }
    if (state.activeCctvChannelId >= 0) {
        SelectedChannelContext ctx;
        if (!findSelectedChannelContextByChannelId(state.activeCctvChannelId, &ctx)
            || ctx.deviceType.trimmed().compare(QStringLiteral("CCTV"), Qt::CaseInsensitive) != 0) {
            state.activeCctvChannelId = -1;
        }
    }
    if (state.activeCctvChannelId < 0) {
        state.activeCctvChannelId = selectedChannelIdForDisplayNameExact(state.activeChannel, QStringLiteral("CCTV"));
    }
    if (state.activeUgvChannelId >= 0) {
        SelectedChannelContext ctx;
        if (!findSelectedChannelContextByChannelId(state.activeUgvChannelId, &ctx)
            || ctx.deviceType.trimmed().compare(QStringLiteral("UGV"), Qt::CaseInsensitive) != 0) {
            state.activeUgvChannelId = -1;
            state.activeUgvGatewayId = -1;
        } else {
            state.activeUgvGatewayId = ctx.deviceId;
        }
    } else {
        state.activeUgvGatewayId = -1;
    }
}

void pruneStateDeviceSelection(AppState &state)
{
    const QStringList valid = configuredDeviceNames();

    for (std::size_t i = 0; i < state.gridCells.size(); ++i) {
        const QString cellName = state.gridCells[i].displayName;
        if (!cellName.isEmpty() && !valid.contains(cellName)) {
            state.clearGridCell(i);
        } else if (cellName.isEmpty()) {
            state.clearGridCell(i);
        }
    }
    if (!state.activeChannel.isEmpty() && !valid.contains(state.activeChannel)) {
        state.activeChannel.clear();
    }
    if (state.activeChannel.isEmpty() && !valid.isEmpty()) {
        state.activeChannel = valid.first();
    }
    if (state.activeCctvChannelId >= 0) {
        SelectedChannelContext ctx;
        if (!findSelectedChannelContextByChannelId(state.activeCctvChannelId, &ctx)
            || ctx.deviceType.trimmed().compare(QStringLiteral("CCTV"), Qt::CaseInsensitive) != 0) {
            state.activeCctvChannelId = -1;
        }
    }
    if (state.activeCctvChannelId < 0) {
        state.activeCctvChannelId = selectedChannelIdForDisplayNameExact(state.activeChannel, QStringLiteral("CCTV"));
    }
    if (state.activeUgvChannelId >= 0) {
        SelectedChannelContext ctx;
        if (!findSelectedChannelContextByChannelId(state.activeUgvChannelId, &ctx)
            || ctx.deviceType.trimmed().compare(QStringLiteral("UGV"), Qt::CaseInsensitive) != 0) {
            state.activeUgvChannelId = -1;
            state.activeUgvGatewayId = -1;
        } else {
            state.activeUgvGatewayId = ctx.deviceId;
        }
    } else {
        state.activeUgvGatewayId = -1;
    }
}

QSet<QString> activeChannelsForScreen(ScreenId screenId)
{
    const AppState &state = AppState::instance();
    QSet<QString> activeChannels;

    switch (screenId) {
    case ScreenId::Main:
        for (std::size_t i = 0; i < state.gridCells.size(); ++i) {
            const MainGridCellState &cell = state.gridCells[i];
            const QString &channel = cell.displayName;
            if (!channel.isEmpty()) {
                const int channelId = cell.channelId;
                if (channelId > 0) {
                    const QString canonicalDisplayName = displayNameForChannelId(channelId);
                    if (!canonicalDisplayName.isEmpty()) {
                        activeChannels.insert(canonicalDisplayName);
                    } else {
                        activeChannels.insert(channel);
                    }
                } else {
                    activeChannels.insert(channel);
                }
            }
        }
        break;
    case ScreenId::Cctv: {
        int channelId = state.activeCctvChannelId;
        if (channelId < 0) {
            channelId = selectedChannelIdForDisplayNameExact(state.activeChannel, QStringLiteral("CCTV"));
        }
        if (channelId < 0) {
            channelId = firstSelectedChannelIdByType(QStringLiteral("CCTV"));
        }
        const QString channel = displayNameForChannelId(channelId);
        if (!channel.isEmpty()) {
            activeChannels.insert(channel);
        }
        break;
    }
    case ScreenId::Ugv: {
        int channelId = state.activeUgvChannelId;
        if (channelId < 0) {
            channelId = firstSelectedChannelIdByType(QStringLiteral("UGV"));
        }
        const QString channel = displayNameForChannelId(channelId);
        if (!channel.isEmpty()) {
            activeChannels.insert(channel);
        }
        break;
    }
    case ScreenId::Playback:
    case ScreenId::Login:
    case ScreenId::Signup:
    case ScreenId::DeviceCheck:
        break;
    }

    return activeChannels;
}

} // namespace MainWindowInternal

using namespace MainWindowInternal;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    initializeState();  // 상태 초기화
    setupUi();  // UI 구성
    m_authInfraReady = initializeAuthServices();    // 인증/서비스 객체 생성
    if (m_deviceScreen) {
        m_deviceScreen->setDeviceService(m_deviceService);
    }
    configureLoginScreenState();
    setupConnections();
    showScreen(ScreenId::Login);
}

MainWindow::~MainWindow() {}

void MainWindow::initializeState()
{
    auto &state = AppState::instance();
    state.currentScreen = ScreenId::Login;  // 앱 첫 시작 화면=로그인
    state.isAuthenticated = false;  // 로그인
    state.authConfigReady = false; 
    state.activeChannel.clear();
    state.activeCctvChannelId = -1;
    state.activeUgvGatewayId = -1;
    state.activeUgvChannelId = -1;
    state.accessToken.clear();  // 토큰 클리어
    state.apiBaseUrl.clear();
    state.currentUserId.clear();
    state.authConfigError.clear();
    state.playbackAutoStartRequested = false;
    state.playbackTargetChannelId = -1;
    state.playbackTargetChannel.clear();
    state.playbackTargetDate.clear();
    state.selectedChannelContexts.clear();
    state.channelRtspByName.clear();
    state.channelRtspById.clear();
    state.clearAllGridCells();  // 메인화면 멀티뷰 셀 초기화
    pruneStateDeviceSelection(state);
}

void MainWindow::setupUi()
{
    setWindowTitle("VMS v2");
    setWindowIcon(QIcon(":/styles/clue_logomark.svg"));
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);
    resize(1920, 1080); // 앱 처음 켰을 때 기본 크기

    loadMergedThemeFromRelativePaths();

    m_stack = new QStackedWidget(this);

    m_loginScreen = new LoginScreen(this);
    m_signupScreen = new SignupScreen(this);
    m_deviceScreen = new DeviceCheckScreen(this);

    m_stack->addWidget(m_loginScreen);
    m_stack->addWidget(m_signupScreen);
    m_stack->addWidget(m_deviceScreen);
    createRuntimeScreens([this]() {
        openSettingsDialog();
    });

    setCentralWidget(m_stack);
    applyNativeDarkTitleBar(this);  // 윈도우창 제목 표시줄 어두운색으로
}