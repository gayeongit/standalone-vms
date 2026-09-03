#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <functional>

#include "app_state.h"

class QStackedWidget;
class LoginScreen;
class SignupScreen;
class DeviceCheckScreen;
class MainScreen;
class CctvScreen;
class UgvScreen;
class PlaybackScreen;
class RestClient;
class AuthService;
class DeviceService;
class CctvControlService;
class PlaybackService;
class UgvService;
class WsClient;
class EventService;
class SettingsDialog;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void initializeState();
    void setupUi();
    void setupConnections();
    void openSettingsDialog();
    void rebuildRuntimeScreens();
    void destroyRuntimeScreens();
    void createRuntimeScreens(std::function<void()> openSettingsHandler);
    void handleLogoutRequest();
    void handleUnauthorized();
    void clearAuthenticationState();
    void connectRuntimeScreens(std::function<void()> openSettingsHandler);
    bool initializeAuthServices();
    void configureLoginScreenState();
    void showScreen(ScreenId screenId);
    void applyWindowGeometryForScreen(ScreenId screenId);
    void centerWindow();

    QStackedWidget *m_stack = nullptr;
    LoginScreen *m_loginScreen = nullptr;
    SignupScreen *m_signupScreen = nullptr;
    DeviceCheckScreen *m_deviceScreen = nullptr;
    MainScreen *m_mainScreen = nullptr;
    CctvScreen *m_cctvScreen = nullptr;
    UgvScreen *m_ugvScreen = nullptr;
    PlaybackScreen *m_playbackScreen = nullptr;
    RestClient *m_restClient = nullptr;
    AuthService *m_authService = nullptr;
    DeviceService *m_deviceService = nullptr;
    CctvControlService *m_cctvControlService = nullptr;
    PlaybackService *m_playbackService = nullptr;
    UgvService *m_ugvService = nullptr;
    WsClient *m_wsClient = nullptr;
    EventService *m_eventService = nullptr;
    QTimer *m_eventPingTimer = nullptr;
    QString m_eventSubscriptionId;
    bool m_authInfraReady = false;
    bool m_firstScreenShow = true;
    bool m_skipNextWsRecovery = false;
    double m_ugvMapMinLat = 33.0;
    double m_ugvMapMaxLat = 39.0;
    double m_ugvMapMinLon = 124.0;
    double m_ugvMapMaxLon = 132.0;
};
#endif // MAINWINDOW_H
