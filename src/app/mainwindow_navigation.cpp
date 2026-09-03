#include "mainwindow.h"

#include "mainwindow_internal.h"

#include "channel_session_manager.h"
#include "clip_capture_manager.h"
#include "common_ui.h"
#include "common_widgets.h"
#include "playback_screen.h"
#include "popup_manager.h"
#include "ugv_screen.h"
#include "video_render_widget.h"

#include <QCloseEvent>
#include <QDebug>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QStackedWidget>
#include <QTimer>

using namespace MainWindowInternal;

void MainWindow::showScreen(ScreenId screenId)
{
    // 이 앱에서 화면 전환은 단순 stacked widget index 변경이 아니다.
    // 실제로는 geometry 적용, 현재 화면 기록, active channel 계산, 성능 계측까지 한 번에 수행한다.
    // geometry 정책, 현재 화면 상태 기록, active channel 재계산,
    // 그리고 first-frame 성능 계측까지 한 번에 묶여 있는 런타임 전환의 중심 함수다.
    QElapsedTimer transitionTimer;
    transitionTimer.start();
    const ScreenId previousScreen = AppState::instance().currentScreen;
    const QString transitionContext = QStringLiteral("%1->%2")
                                          .arg(screenIdName(previousScreen),
                                               screenIdName(screenId));
    const bool targetCompact = isCompactScreen(screenId);
    const bool previousCompact = isCompactScreen(previousScreen);
    const bool initialShow = m_firstScreenShow;
    const bool enteringMainFromDevice = (previousScreen == ScreenId::DeviceCheck && screenId == ScreenId::Main);
    const bool returningLoginFromRuntime = (screenId == ScreenId::Login && !previousCompact);

    const bool shouldApplyGeometry = targetCompact || initialShow || enteringMainFromDevice || returningLoginFromRuntime;
    const bool shouldCenter = targetCompact || initialShow || enteringMainFromDevice || returningLoginFromRuntime;
    const bool shouldShowNormal = shouldApplyGeometry;

    if (previousScreen == ScreenId::Main && screenId == ScreenId::Cctv) {
        int targetChannelId = -1;
        QString targetChannel;
        resolveAndNormalizeActiveCctvTarget(&targetChannelId, &targetChannel);
        const QSet<QString> mainActiveChannels = activeChannelsForScreen(ScreenId::Main);
        const bool warm = !targetChannel.isEmpty() && mainActiveChannels.contains(targetChannel);
        qInfo().noquote()
            << QString("perf metric=cctv_entry_warm context=main->cctv value=%1 unit=bool target_id=%2 target=%3")
                  .arg(warm ? QStringLiteral("1") : QStringLiteral("0"),
                       QString::number(targetChannelId),
                       targetChannel.isEmpty() ? QStringLiteral("unknown") : targetChannel);
    }

    if (shouldShowNormal) {
        showNormal();
    }
    if (shouldApplyGeometry) {
        applyWindowGeometryForScreen(screenId);
    }

    if (shouldCenter) {
        // showNormal() + resize() 후 OS가 창 geometry를 비동기로 처리하므로
        // 다음 이벤트 루프 틱에 센터링해야 정확한 화면 중앙에 배치됨
        if (enteringMainFromDevice) {
            centerWindow();
        } else {
            QTimer::singleShot(0, this, [this]() { centerWindow(); });
        }
    }
    m_stack->setCurrentIndex(toStackIndex(screenId));
    if (shouldCenter && !enteringMainFromDevice) {
        // 스택 전환 이후 레이아웃이 확정된 상태에서 한 번 더 중앙 정렬.
        QTimer::singleShot(0, this, [this]() { centerWindow(); });
        if (enteringMainFromDevice) {
            // DeviceCheck -> Main 전환 시 WM 반영 지연 보정.
            centerWindow();
        }
    }
    AppState::instance().currentScreen = screenId;
    ChannelSessionManager::instance().applyActiveChannels(activeChannelsForScreen(screenId));
    m_firstScreenShow = false;
    qInfo().noquote()
        << QString("perf metric=screen_transition_ms context=%1->%2 value=%3 unit=ms")
              .arg(screenIdName(previousScreen),
                   screenIdName(screenId),
                   QString::number(transitionTimer.elapsed()));
    if (shouldTrackFirstFrameForScreen(screenId)) {
        RenderPerfMetrics::beginFirstFrameAfterTransition(transitionContext);
        RenderPerfMetrics::beginFirstLiveFrameAfterTransition(transitionContext);
    } else {
        RenderPerfMetrics::clearFirstFrameAfterTransition();
        RenderPerfMetrics::clearFirstLiveFrameAfterTransition();
    }
}

void MainWindow::applyWindowGeometryForScreen(ScreenId screenId)
{
    // 로그인/회원가입/장치확인은 고정 크기 compact 화면,
    // Main/CCTV/UGV/Playback는 큰 런타임 화면으로 분기해 창 크기 정책을 다시 적용한다.
    // Main/CCTV/UGV/Playback는 resizable 16:9 baseline 화면으로 취급한다.
    // setFixedSize() 부작용으로 size policy가 굳는 문제를 피하려고
    // 먼저 min/max와 size policy를 리셋한 뒤 화면별 규칙을 다시 적용한다.
    const bool compactScreen = (screenId == ScreenId::Login
                                || screenId == ScreenId::Signup
                                || screenId == ScreenId::DeviceCheck);

    // Always reset size constraints first so Windows treats the window as resizable
    // before applying the screen-specific geometry.
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    // Restore size policy ??setFixedSize() silently sets QSizePolicy::Fixed which
    // persists across size-constraint changes and causes Windows to disable the
    // maximize button even after min/max are no longer equal.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    if (compactScreen) {
        if (screenId == ScreenId::Login || screenId == ScreenId::Signup) {
            const QSize targetSize = (screenId == ScreenId::Login)
                ? QSize(400, 380)
                : QSize(400, 460);
            setMinimumSize(targetSize);
            setMaximumSize(targetSize);
            resize(targetSize);
            return;
        }
        if (screenId == ScreenId::DeviceCheck) {
            setMinimumSize(560, 380);
            setMaximumSize(560, 380);
            resize(560, 380);
            return;
        }
        return;
    }

    // Main/CCTV/UGV/Playback: 1600x900 우선, 작은 화면에서는 가용 해상도 비율로 축소.
    QScreen *scr = this->screen();
    if (!scr) {
        scr = QGuiApplication::primaryScreen();
    }
    const QRect available = scr ? scr->availableGeometry() : QRect(0, 0, 1920, 1080);

    int targetW = 1600;
    int targetH = 900;
    if (available.width() < targetW || available.height() < targetH) {
        targetW = qMin(available.width(), qMax(1280, static_cast<int>(available.width() * 0.92)));
        targetH = qMin(available.height(), qMax(720, static_cast<int>(available.height() * 0.92)));
    }

    setMinimumSize(1280, 720);
    if (width() != targetW || height() != targetH) {
        resize(targetW, targetH);
    }
}

void MainWindow::centerWindow()
{
    QScreen *screen = this->screen();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }
    const QRect available = screen->availableGeometry();
    const QPoint topLeft = available.center() - QPoint(width() / 2, height() / 2);
    move(topLeft);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 종료 시에는 단순히 창을 닫지 않는다.
    // 진행 중인 클립/내보내기 작업을 확인하고, UGV/미디어 세션을 내린 뒤에야 실제 종료를 허용한다.
    // 클립 저장/내보내기 중단 확인, UGV 세션 정리,
    // 마지막으로 ChannelSessionManager shutdown까지 수행해
    // ffmpeg/GStreamer 파이프라인이 프로세스 종료 직전까지 남지 않게 한다.
    auto &clipMgr = ClipCaptureManager::instance();
    const bool exportOn = (m_playbackScreen && m_playbackScreen->isExportBusy());
    if (clipMgr.state() != ClipCaptureManager::State::Idle) {
        const bool confirmed = PopupManager::confirm(
            this,
            "Exit",
            exportOn
                ? "클립 저장과 내보내기 작업이 진행 중입니다. 종료하면 현재 작업은 저장되지 않고 중단됩니다. 종료하시겠습니까?"
                : "클립 저장이 진행 중입니다. 종료하면 현재 클립은 저장되지 않고 중단됩니다. 종료하시겠습니까?");
        if (!confirmed) {
            event->ignore();
            return;
        }

        clipMgr.discard();
    } else if (exportOn) {
        const bool confirmed = PopupManager::confirm(
            this,
            "Exit",
            "내보내기 작업이 진행 중입니다. 종료하면 현재 작업은 중단됩니다. 종료하시겠습니까?");
        if (!confirmed) {
            event->ignore();
            return;
        }
    } else {
        clipMgr.stop();
    }
    if (exportOn && m_playbackScreen) {
        m_playbackScreen->cancelExportOperations(false);
    }
    if (m_ugvScreen) {
        m_ugvScreen->prepareForShutdown();
    }
    if (m_ugvService) {
        m_ugvService->shutdown();
    }

    // Ensure all GStreamer channel sessions are stopped before process teardown.
    ChannelSessionManager::instance().shutdown();
    QMainWindow::closeEvent(event);
}



