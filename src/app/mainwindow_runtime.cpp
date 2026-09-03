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
    SettingsDialog extractedDialog(this);
    extractedDialog.exec();
    if (extractedDialog.devicesChanged()) {
        auto &state = AppState::instance();
        pruneStateDeviceSelection(state);
        applyDeviceChangesToRuntimeState(state);
        rebuildRuntimeScreens();
    }
}
void MainWindow::rebuildRuntimeScreens()
{
    // 설정 변경 후 런타임 화면을 재조립하는 진입점이다.
    // 기존 런타임 화면을 버리고 새로 만들되, 가능하면 사용자가 있던 화면으로 다시 복귀시킨다.
    // 현재 화면이 런타임 화면이면 같은 ScreenId로 복귀시켜 컨텍스트를 보존하고,
    // 아니면 Main으로 안전하게 되돌린다.
    auto showSettingsDialog = [this]() {
        openSettingsDialog();
    };

    const ScreenId current = AppState::instance().currentScreen;
    createRuntimeScreens(showSettingsDialog);

    if (current == ScreenId::Main || current == ScreenId::Cctv || current == ScreenId::Ugv || current == ScreenId::Playback) {
        showScreen(current);
        return;
    }
    showScreen(ScreenId::Main);
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



