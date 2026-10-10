#include "mainwindow.h"

#include "mainwindow_internal.h"

#include "common_widgets.h"
#include "cctv_screen.h"
#include "main_screen.h"
#include "playback_screen.h"
#include "settings_dialog.h"
#include "ugv_screen.h"

#include <QStackedWidget>

using namespace MainWindowInternal;

void MainWindow::openSettingsDialog()
{
    // Phase 6: "장치 관리" 탭 제거 이후로는 이 다이얼로그에서 런타임 채널 선택에 영향을 주는
    // 변경이 발생하지 않는다(저장 경로/정책 탭만 남음) — devicesChanged() 이후 처리 로직도 같이 제거.
    SettingsDialog extractedDialog(this);
    extractedDialog.exec();
}
void MainWindow::destroyRuntimeScreens()
{
    if (m_mainScreen) {
        m_stack->removeWidget(m_mainScreen);
        m_mainScreen->deleteLater();
        m_mainScreen = nullptr;
    }
    if (m_cctvScreen) {
        m_stack->removeWidget(m_cctvScreen);
        m_cctvScreen->deleteLater();
        m_cctvScreen = nullptr;
    }
    if (m_ugvScreen) {
        m_stack->removeWidget(m_ugvScreen);
        m_ugvScreen->deleteLater();
        m_ugvScreen = nullptr;
    }
    if (m_playbackScreen) {
        m_stack->removeWidget(m_playbackScreen);
        m_playbackScreen->deleteLater();
        m_playbackScreen = nullptr;
    }
}

void MainWindow::createRuntimeScreens(std::function<void()> openSettingsHandler)
{
    // Main/CCTV/UGV/Playback는 인증 이후의 상태(AppState, 서비스 주입)에 의존한다.
    // 이 함수는 네 개 화면 인스턴스를 만들고 필요한 서비스를 주입한 뒤 stacked widget에 등록한다.
    // 그래서 이 함수는 런타임 화면을 한 번에 생성하고,
    // 필요한 서비스(Playback/Ugv/CCTV control)를 주입한 뒤
    // stacked widget에 다시 꽂는 단일 조립 지점 역할을 한다.
    const int mainIndex = toStackIndex(ScreenId::Main);
    const int cctvIndex = toStackIndex(ScreenId::Cctv);
    const int ugvIndex = toStackIndex(ScreenId::Ugv);
    const int playbackIndex = toStackIndex(ScreenId::Playback);

    destroyRuntimeScreens();

    m_mainScreen = new MainScreen(this);
    m_cctvScreen = new CctvScreen(this);
    m_cctvScreen->setCctvControlService(m_cctvControlService);
    m_ugvScreen = new UgvScreen(this);
    m_ugvScreen->setUgvService(m_ugvService);
    m_ugvScreen->setMapBounds(m_ugvMapMinLat, m_ugvMapMaxLat, m_ugvMapMinLon, m_ugvMapMaxLon);
    m_playbackScreen = new PlaybackScreen(this);
    m_playbackScreen->setPlaybackService(m_playbackService);
    if (auto *sidebar = m_mainScreen->findChild<SidebarWidget *>()) {
        sidebar->setPlaybackService(m_playbackService);
    }
    if (auto *sidebar = m_cctvScreen->findChild<SidebarWidget *>()) {
        sidebar->setPlaybackService(m_playbackService);
    }
    if (auto *sidebar = m_ugvScreen->findChild<SidebarWidget *>()) {
        sidebar->setPlaybackService(m_playbackService);
    }
    if (auto *sidebar = m_playbackScreen->findChild<SidebarWidget *>()) {
        sidebar->setPlaybackService(m_playbackService);
    }
    m_stack->insertWidget(mainIndex, m_mainScreen);
    m_stack->insertWidget(cctvIndex, m_cctvScreen);
    m_stack->insertWidget(ugvIndex, m_ugvScreen);
    m_stack->insertWidget(playbackIndex, m_playbackScreen);
    connectRuntimeScreens(std::move(openSettingsHandler));
}



