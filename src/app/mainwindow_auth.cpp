#include "mainwindow.h"

#include "mainwindow_internal.h"

#include "app_config_loader.h"
#include "auth_service.h"
#include "channel_session_manager.h"
#include "clip_capture_manager.h"
#include "common_ui.h"
#include "cctv_control_service.h"
#include "credential_store.h"
#include "device_credential_dialog.h"
#include "device_service.h"
#include "onvif_lite_client.h"
#include "event_service.h"
#include "event_ui_helpers.h"
#include "login_screen.h"
#include "main_screen.h"
#include "cctv_screen.h"
#include "ugv_screen.h"
#include "playback_screen.h"
#include "playback_service.h"
#include "popup_manager.h"
#include "rest_client.h"
#include "ugv_service.h"
#include "ws_client.h"

#include <QDateTime>
#include <QJsonObject>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QSharedPointer>
#include <QTimer>
#include <QUrl>

using namespace MainWindowInternal;

namespace {
// Phase 3b: GetStreamUri가 돌려주는 순수 RTSP 주소에 캐시된 자격증명을 끼워 넣는다.
// StreamPlayer는 rtspsrc location="..."으로 URL을 그대로 받아쓰기 때문에, 여기서 한 번만
// 처리해두면 미디어 계층은 한 줄도 안 건드려도 된다 (설계 원칙 1/2).
QString embedRtspCredentials(const QString &rtsp, const QString &username, const QString &password)
{
    if (rtsp.trimmed().isEmpty() || (username.isEmpty() && password.isEmpty())) {
        return rtsp;
    }
    QUrl url(rtsp);
    if (!url.isValid()) {
        return rtsp;
    }
    url.setUserName(username);
    url.setPassword(password);
    return url.toString(QUrl::FullyEncoded);
}
} // namespace

void MainWindow::setupConnections()
{
    auto showSettingsDialog = [this]() {
        openSettingsDialog();
    };

    connect(m_loginScreen, &LoginScreen::loginRequested, this, [this](const QString &username, const QString &password) {
        if (!m_authInfraReady || !m_authService) {
            if (m_loginScreen) {
                m_loginScreen->showConfigError(
                    "설정 오류로 로그인 요청을 보낼 수 없습니다. app_config.json을 확인해 주세요.");
            }
            return;
        }
        m_loginScreen->setLoginInProgress(true);
        m_loginScreen->clearLoginStatus();
        m_authService->login(username, password, this, [this, username](const AuthResult &result) {
            if (!m_loginScreen) {
                return;
            }
            m_loginScreen->setLoginInProgress(false);
            if (!result.ok) {
                m_loginScreen->showLoginError(result.errorMessage);
                return;
            }
            auto &state = AppState::instance();
            state.isAuthenticated = true;
            state.currentUserId = result.userId.trimmed().isEmpty() ? username : result.userId.trimmed();
            state.accessToken = result.accessToken;
            if (m_restClient) {
                m_restClient->setAccessToken(result.accessToken);
            }
            if (m_ugvService) {
                m_ugvService->setAccessToken(result.accessToken);
            }
            if (m_eventService) {
                m_eventService->reset();
                m_eventService->fetchRecentEvents(this);
            }
            m_skipNextWsRecovery = true;
            if (m_wsClient) {
                m_wsClient->connectAuthenticated(result.accessToken);
            }
            showScreen(ScreenId::DeviceCheck);
            if (m_deviceScreen) {
                QTimer::singleShot(300, m_deviceScreen, [this]() {
                    if (m_deviceScreen) {
                        m_deviceScreen->refreshDevices();
                    }
                });
            }
        });
    });
    connect(m_loginScreen, &LoginScreen::guestRequested, this, [this]() {
        // 게스트 진입: AuthService::login(...)을 호출하지 않으므로 AppState.isAuthenticated는
        // false로 유지된다. 카메라 데이터는 로그인 여부와 무관하게 항상 직접 받는다는
        // 설계 원칙(로드맵 2.3)에 따라, 로그인 성공 경로와 동일하게 DeviceCheck로 전환하고
        // 채널 조회를 시도한다.
        showScreen(ScreenId::DeviceCheck);
        if (m_deviceScreen) {
            QTimer::singleShot(300, m_deviceScreen, [this]() {
                if (m_deviceScreen) {
                    m_deviceScreen->refreshDevices();
                }
            });
        }
    });
    connect(m_loginScreen, &LoginScreen::signupRequested, this, [this]() {
        if (m_loginScreen) {
            m_loginScreen->resetLoginInputs();
        }
        if (m_signupScreen) {
            m_signupScreen->clearSignupStatus();
            m_signupScreen->resetSignupInputs();
        }
        showScreen(ScreenId::Signup);
    });

    connect(m_signupScreen, &SignupScreen::signupRequested, this, [this](const QString &name, const QString &username, const QString &password) {
        if (!m_authInfraReady || !m_authService) {
            if (m_signupScreen) {
                m_signupScreen->showSignupError(
                    "설정 오류로 회원가입 요청을 보낼 수 없습니다. app_config.json을 확인해 주세요.");
            }
            return;
        }
        m_signupScreen->setSignupInProgress(true);
        m_signupScreen->clearSignupStatus();
        m_authService->signup(name, username, password, this, [this](const AuthResult &result) {
            if (!m_signupScreen) {
                return;
            }
            m_signupScreen->setSignupInProgress(false);
            if (!result.ok) {
                m_signupScreen->showSignupError(result.errorMessage);
                return;
            }
            PopupManager::showInfo(this, "계정", "회원가입이 완료되었습니다. 로그인해 주세요.");
            m_signupScreen->clearSignupStatus();
            m_signupScreen->resetSignupInputs();
            if (m_loginScreen) {
                m_loginScreen->resetLoginInputs();
            }
            showScreen(ScreenId::Login);
        });
    });
    connect(m_signupScreen, &SignupScreen::backToLoginRequested, this, [this]() {
        if (m_signupScreen) {
            m_signupScreen->clearSignupStatus();
            m_signupScreen->resetSignupInputs();
        }
        if (m_loginScreen) {
            m_loginScreen->resetLoginInputs();
        }
        showScreen(ScreenId::Login);
    });

    connect(m_deviceScreen, &DeviceCheckScreen::startRequested, this, [this, showSettingsDialog](const QVector<SelectedChannelContext> &selectedContexts) {
        if (!m_deviceService) {
            PopupManager::showInfo(this, "장치 탐색", "DeviceService 미연결 상태입니다.");
            return;
        }
        // 채널 0개(카메라 미연결/게스트)도 정상 입력이다 — Phase 3a.
        // normalized가 비어 있으면 아래 pending 루프가 그냥 0회 돌고 finalize()가 즉시 호출된다.
        auto normalized = normalizeSelectedContextsForRuntime(selectedContexts);

        // Phase 3b: 선택된 채널들의 디바이스(deviceIp 기준)마다 자격증명이 세션 캐시에 있는지 확인.
        // discovery/트리 조회 자체는 무인증이라 여기서 처음 걸린다 — RTSP 재생/PTZ 제어에만 필요.
        // 로그인 상태면 QtKeychain을 먼저 보고, 없으면 모달. 모달을 취소하면 그 디바이스의
        // 채널은 이번 선택에서 제외한다.
        //
        // 모달에 새로 입력한 값(캐시/키체인에서 가져온 게 아닌 것)은 대표 채널 하나로 PTZ에
        // delta=0 "ping"을 날려 맞는지 바로 검증한다. 틀리면 캐시에서 지우고, 그 디바이스는
        // 기존 "RTSP 조회 실패" 채널과 똑같이 그리드에서 제외한다 — 새 재시도 UI는 만들지 않고,
        // "연결 안 됨" 상태로 그리드에 남는 대신 아예 안 보이게(선택 안 한 것처럼) 처리한다.
        m_deviceScreen->setEnabled(false);

        QSet<QString> rejectedIps;
        QSet<QString> freshlyEnteredIps;
        {
            auto &credentialState = AppState::instance();
            QSet<QString> processedIps;
            for (const auto &ctx : normalized) {
                const QString deviceIp = ctx.deviceIp.trimmed();
                if (deviceIp.isEmpty() || processedIps.contains(deviceIp)) {
                    continue;
                }
                processedIps.insert(deviceIp);
                if (credentialState.deviceCredentialUsernameByIp.contains(deviceIp)) {
                    continue;
                }
                QString loadedUsername;
                QString loadedPassword;
                if (credentialState.isAuthenticated
                    && CredentialStore::load(deviceIp, &loadedUsername, &loadedPassword)) {
                    credentialState.deviceCredentialUsernameByIp.insert(deviceIp, loadedUsername);
                    credentialState.deviceCredentialPasswordByIp.insert(deviceIp, loadedPassword);
                    continue;
                }
                const QString label = ctx.displayName.trimmed().isEmpty() ? deviceIp : ctx.displayName.trimmed();
                DeviceCredentialDialog dialog(label, this);
                if (dialog.exec() != QDialog::Accepted) {
                    rejectedIps.insert(deviceIp);
                    continue;
                }
                credentialState.deviceCredentialUsernameByIp.insert(deviceIp, dialog.username());
                credentialState.deviceCredentialPasswordByIp.insert(deviceIp, dialog.password());
                if (credentialState.isAuthenticated) {
                    CredentialStore::save(deviceIp, dialog.username(), dialog.password());
                }
                freshlyEnteredIps.insert(deviceIp);
            }
        }

        // deviceIp별 대표 채널(ping에 쓸 PTZ xaddr/token을 얻기 위함 — DeviceService 캐시라 네트워크 요청 없음).
        QHash<QString, int> representativeChannelIdByIp;
        for (const auto &ctx : normalized) {
            const QString deviceIp = ctx.deviceIp.trimmed();
            if (ctx.channelId < 0 || !freshlyEnteredIps.contains(deviceIp)
                || representativeChannelIdByIp.contains(deviceIp)) {
                continue;
            }
            representativeChannelIdByIp.insert(deviceIp, ctx.channelId);
        }

        auto proceedWithCredentials = [this, showSettingsDialog, normalized, rejectedIps](const QSet<QString> &pingFailedIps) {
            QVector<SelectedChannelContext> credentialFiltered;
            credentialFiltered.reserve(normalized.size());
            auto &credentialState = AppState::instance();
            for (const auto &ctx : normalized) {
                const QString deviceIp = ctx.deviceIp.trimmed();
                if (!deviceIp.isEmpty() && (rejectedIps.contains(deviceIp) || pingFailedIps.contains(deviceIp))) {
                    continue;
                }
                credentialFiltered.push_back(ctx);
            }
            runDeviceSelectionPipeline(showSettingsDialog, credentialFiltered);
        };

        if (freshlyEnteredIps.isEmpty()) {
            proceedWithCredentials({});
            return;
        }

        auto pingPending = QSharedPointer<int>::create(static_cast<int>(freshlyEnteredIps.size()));
        auto pingFailedIps = QSharedPointer<QSet<QString>>::create();
        for (const QString &deviceIp : freshlyEnteredIps) {
            const int repChannelId = representativeChannelIdByIp.value(deviceIp, -1);
            if (repChannelId < 0) {
                // ping에 쓸 PTZ 정보가 없는 디바이스(예외적) — 검증 없이 통과시키고 나중에 실제
                // RTSP/PTZ 시도에서 실패하도록 둔다.
                *pingPending -= 1;
                if (*pingPending <= 0) {
                    proceedWithCredentials(*pingFailedIps);
                }
                continue;
            }
            m_deviceService->fetchChannelDetail(repChannelId, this, [this, deviceIp, pingPending, pingFailedIps, proceedWithCredentials](const ChannelDetailResult &detail) {
                if (!detail.ok || detail.onvifPtzXAddr.trimmed().isEmpty() || detail.onvifProfileToken.trimmed().isEmpty()) {
                    *pingPending -= 1;
                    if (*pingPending <= 0) {
                        proceedWithCredentials(*pingFailedIps);
                    }
                    return;
                }
                auto &state = AppState::instance();
                const QString username = state.deviceCredentialUsernameByIp.value(deviceIp);
                const QString password = state.deviceCredentialPasswordByIp.value(deviceIp);
                m_onvifLiteClient->relativeMove(detail.onvifPtzXAddr, detail.onvifProfileToken, 0.0, username, password, this,
                    [deviceIp, pingPending, pingFailedIps, proceedWithCredentials](bool ok, const QString &/*errorMessage*/) {
                        if (!ok) {
                            auto &state = AppState::instance();
                            state.deviceCredentialUsernameByIp.remove(deviceIp);
                            state.deviceCredentialPasswordByIp.remove(deviceIp);
                            pingFailedIps->insert(deviceIp);
                        }
                        *pingPending -= 1;
                        if (*pingPending <= 0) {
                            proceedWithCredentials(*pingFailedIps);
                        }
                    });
            });
        }
    });
    connect(m_deviceScreen, &DeviceCheckScreen::backToLoginRequested, this, [this]() {
        if (m_loginScreen) {
            m_loginScreen->resetLoginInputs();
        }
        showScreen(ScreenId::Login);
    });

}

void MainWindow::runDeviceSelectionPipeline(std::function<void()> showSettingsDialog, const QVector<SelectedChannelContext> &normalized)
{
        auto pending = QSharedPointer<int>::create(0);
        auto resolvedRtsp = QSharedPointer<QHash<QString, QString>>::create();
        auto resolvedCodecs = QSharedPointer<QHash<QString, QString>>::create();
        auto resolvedOnvifPtzXAddrs = QSharedPointer<QHash<QString, QString>>::create();
        auto resolvedOnvifImagingXAddrs = QSharedPointer<QHash<QString, QString>>::create();
        auto resolvedOnvifProfileTokens = QSharedPointer<QHash<QString, QString>>::create();
        auto hadError = QSharedPointer<bool>::create(false);
        for (const auto &ctx : normalized) {
            if (ctx.channelId >= 0) {
                *pending += 1;
            }
        }

        auto finalize = [this, showSettingsDialog, normalized, resolvedRtsp, resolvedCodecs, resolvedOnvifPtzXAddrs, resolvedOnvifImagingXAddrs, resolvedOnvifProfileTokens, hadError]() {
            auto &state = AppState::instance();
            state.channelRtspByName = *resolvedRtsp;
            state.channelRtspById.clear();
            state.channelVideoCodecByName = *resolvedCodecs;
            state.channelVideoCodecById.clear();
            state.channelOnvifPtzXAddrById.clear();
            state.channelOnvifImagingXAddrById.clear();
            state.channelOnvifProfileTokenById.clear();
            state.channelOnvifUsernameById.clear();
            state.channelOnvifPasswordById.clear();

            QVector<SelectedChannelContext> resolvedContexts;
            resolvedContexts.reserve(normalized.size());
            QStringList cctvChannels;
            cctvChannels.reserve(normalized.size());
            for (auto ctx : normalized) {
                const QString name = ctx.displayName.trimmed();
                if (name.isEmpty()) {
                    continue;
                }
                if (state.channelRtspByName.value(name).trimmed().isEmpty()) {
                    continue;
                }
                ctx.videoCodec = state.channelVideoCodecByName.value(name).trimmed();
                // Phase 3b: 이 채널이 속한 디바이스의 세션 캐시 자격증명을 RTSP URL에 끼워 넣는다.
                const QString deviceIp = ctx.deviceIp.trimmed();
                const QString onvifUsername = state.deviceCredentialUsernameByIp.value(deviceIp);
                const QString onvifPassword = state.deviceCredentialPasswordByIp.value(deviceIp);
                const QString embeddedRtsp = embedRtspCredentials(
                    state.channelRtspByName.value(name).trimmed(), onvifUsername, onvifPassword);
                state.channelRtspByName.insert(name, embeddedRtsp);
                if (ctx.channelId >= 0) {
                    state.channelRtspById.insert(ctx.channelId, embeddedRtsp);
                    state.channelVideoCodecById.insert(ctx.channelId, ctx.videoCodec);
                    if (!onvifUsername.isEmpty() || !onvifPassword.isEmpty()) {
                        state.channelOnvifUsernameById.insert(ctx.channelId, onvifUsername);
                        state.channelOnvifPasswordById.insert(ctx.channelId, onvifPassword);
                    }
                    const QString onvifPtzXAddr = resolvedOnvifPtzXAddrs->value(name).trimmed();
                    const QString onvifImagingXAddr = resolvedOnvifImagingXAddrs->value(name).trimmed();
                    if (!onvifPtzXAddr.isEmpty() || !onvifImagingXAddr.isEmpty()) {
                        state.channelOnvifPtzXAddrById.insert(ctx.channelId, onvifPtzXAddr);
                        state.channelOnvifImagingXAddrById.insert(ctx.channelId, onvifImagingXAddr);
                        state.channelOnvifProfileTokenById.insert(ctx.channelId, resolvedOnvifProfileTokens->value(name).trimmed());
                    }
                }
                resolvedContexts.push_back(ctx);
                if (ctx.deviceType.trimmed().compare(QStringLiteral("CCTV"), Qt::CaseInsensitive) == 0) {
                    cctvChannels.push_back(name);
                }
            }

            if (resolvedContexts.isEmpty()) {
                state.selectedChannelContexts.clear();
                if (!normalized.isEmpty()) {
                    // 채널을 선택했는데 RTSP 조회가 전부 실패한 경우에만 진입을 막는다.
                    // 애초에 0개 선택(게스트/카메라 없음)은 아래로 흘러 Main까지 정상 진입해야 한다.
                    PopupManager::showInfo(this, "장치 탐색", "채널 RTSP 조회에 실패했습니다. 잠시 후 다시 시도해 주세요.");
                    if (m_deviceScreen) {
                        m_deviceScreen->setEnabled(true);
                    }
                    return;
                }
            }

            state.selectedChannelContexts = resolvedContexts;
            state.clearAllGridCells();
            const int assignCount = std::min<int>(cctvChannels.size(), static_cast<int>(state.gridCells.size()));
            for (int i = 0; i < assignCount; ++i) {
                const int channelId = selectedChannelIdForDisplayNameExact(cctvChannels[i], QStringLiteral("CCTV"));
                state.setGridCell(static_cast<std::size_t>(i),
                                  cctvChannels[i],
                                  channelId,
                                  deviceIdForChannelId(channelId));
            }
            state.activeChannel = cctvChannels.isEmpty() ? QString() : cctvChannels.first();
            state.activeCctvChannelId = state.activeChannel.isEmpty()
                ? -1
                : selectedChannelIdForDisplayNameExact(state.activeChannel, QStringLiteral("CCTV"));
            state.activeUgvChannelId = -1;
            state.activeUgvGatewayId = -1;

            if (*hadError) {
                PopupManager::showInfo(this, "장치 탐색", "일부 채널 RTSP 조회에 실패했습니다. 조회에 성공한 채널만 진입합니다.");
            }

            createRuntimeScreens(showSettingsDialog);
            showScreen(ScreenId::Main);
            if (m_deviceScreen) {
                m_deviceScreen->setEnabled(true);
            }
        };

        if (*pending <= 0) {
            finalize();
            return;
        }

        for (const auto &ctx : normalized) {
            if (ctx.channelId < 0) {
                continue;
            }
            const QString displayName = ctx.displayName.trimmed();
            m_deviceService->fetchChannelDetail(ctx.channelId, this, [this, pending, resolvedRtsp, resolvedCodecs, resolvedOnvifPtzXAddrs, resolvedOnvifImagingXAddrs, resolvedOnvifProfileTokens, hadError, displayName, finalize](const ChannelDetailResult &detail) {
                if (detail.ok && !detail.rtsp.trimmed().isEmpty() && !displayName.isEmpty()) {
                    resolvedRtsp->insert(displayName, detail.rtsp.trimmed());
                    resolvedCodecs->insert(displayName, detail.videoCodec.trimmed());
                    if (!detail.onvifPtzXAddr.trimmed().isEmpty() || !detail.onvifImagingXAddr.trimmed().isEmpty()) {
                        resolvedOnvifPtzXAddrs->insert(displayName, detail.onvifPtzXAddr.trimmed());
                        resolvedOnvifImagingXAddrs->insert(displayName, detail.onvifImagingXAddr.trimmed());
                        resolvedOnvifProfileTokens->insert(displayName, detail.onvifProfileToken.trimmed());
                    }
                } else {
                    *hadError = true;
                }
                *pending -= 1;
                if (*pending <= 0) {
                    finalize();
                }
            });
        }
}

void MainWindow::connectRuntimeScreens(std::function<void()> openSettingsHandler)
{
    auto allowLeaveUgvMission = [this]() {
        return !m_ugvScreen || AppState::instance().currentScreen != ScreenId::Ugv
            || m_ugvScreen->confirmNavigationAwayFromActiveMission();
    };

    connect(m_mainScreen, &MainScreen::openCctvRequested, this, [this]() {
        showScreen(ScreenId::Cctv);
    });
    connect(m_mainScreen, &MainScreen::openUgvRequested, this, [this]() {
        showScreen(ScreenId::Ugv);
    });
    connect(m_mainScreen, &MainScreen::openPlaybackRequested, this, [this]() {
        showScreen(ScreenId::Playback);
    });
    connect(m_mainScreen, &MainScreen::settingsRequested, this, openSettingsHandler);
    connect(m_mainScreen, &MainScreen::logoutRequested, this, [this]() {
        handleLogoutRequest();
    });

    connect(m_cctvScreen, &CctvScreen::backToMainRequested, this, [this]() {
        showScreen(ScreenId::Main);
    });
    connect(m_cctvScreen, &CctvScreen::openUgvRequested, this, [this]() {
        showScreen(ScreenId::Ugv);
    });
    connect(m_cctvScreen, &CctvScreen::openPlaybackRequested, this, [this]() {
        showScreen(ScreenId::Playback);
    });
    connect(m_cctvScreen, &CctvScreen::settingsRequested, this, openSettingsHandler);
    connect(m_cctvScreen, &CctvScreen::logoutRequested, this, [this]() {
        handleLogoutRequest();
    });

    connect(m_ugvScreen, &UgvScreen::backToMainRequested, this, [this, allowLeaveUgvMission]() {
        if (!allowLeaveUgvMission()) {
            return;
        }
        showScreen(ScreenId::Main);
    });
    connect(m_ugvScreen, &UgvScreen::missionEndRequested, this, [this]() {
        showScreen(ScreenId::Main);
    });
    connect(m_ugvScreen, &UgvScreen::openCctvRequested, this, [this, allowLeaveUgvMission]() {
        if (!allowLeaveUgvMission()) {
            return;
        }
        showScreen(ScreenId::Cctv);
    });
    connect(m_ugvScreen, &UgvScreen::openPlaybackRequested, this, [this, allowLeaveUgvMission]() {
        if (!allowLeaveUgvMission()) {
            return;
        }
        showScreen(ScreenId::Playback);
    });
    connect(m_ugvScreen, &UgvScreen::settingsRequested, this, openSettingsHandler);
    connect(m_ugvScreen, &UgvScreen::logoutRequested, this, [this, allowLeaveUgvMission]() {
        if (!allowLeaveUgvMission()) {
            return;
        }
        handleLogoutRequest();
    });

    connect(m_playbackScreen, &PlaybackScreen::backToMainRequested, this, [this]() {
        showScreen(ScreenId::Main);
    });
    connect(m_playbackScreen, &PlaybackScreen::openCctvRequested, this, [this]() {
        showScreen(ScreenId::Cctv);
    });
    connect(m_playbackScreen, &PlaybackScreen::openUgvRequested, this, [this]() {
        showScreen(ScreenId::Ugv);
    });
    connect(m_playbackScreen, &PlaybackScreen::settingsRequested, this, openSettingsHandler);
    connect(m_playbackScreen, &PlaybackScreen::logoutRequested, this, [this]() {
        handleLogoutRequest();
    });
}

bool MainWindow::initializeAuthServices()
{
    // 인증 이후의 거의 모든 네트워크 경로가 여기서 조립된다.
    // 구체적으로는 설정 파일을 읽어 각 서비스 객체를 생성하고 endpoint/토큰 의존성을 주입한다.
    // 단순 서비스 생성이 아니라 app_config.json을 해석해
    // Rest/Auth/Device/Playback/UGV/Event 계층을 서로 연결하는 부트스트랩 단계다.
    auto &state = AppState::instance();
    AppConfig config;
    QString configError;
    if (!loadAppConfig(&config, &configError)) {
        state.authConfigReady = false;
        state.authConfigError = configError;
        return false;
    }

    state.authConfigReady = true;
    state.authConfigError.clear();
    state.apiBaseUrl = config.apiBaseUrl;

    m_restClient = new RestClient(this);
    m_restClient->setBaseUrl(config.apiBaseUrl);
    m_restClient->setRequestTimeoutMs(config.requestTimeoutMs);
    connect(m_restClient, &RestClient::unauthorizedDetected, this, [this](const QString &requestTag) {
        const QString tag = requestTag.trimmed().toLower();
        if (tag == "login" || tag == "signup") {
            return;
        }
        handleUnauthorized();
    });

    m_authService = new AuthService(m_restClient, this);
    m_authService->setLoginPath(config.loginPath);
    m_authService->setLogoutPath(config.logoutPath);
    m_authService->setSignupPath(config.signupPath);

    m_onvifLiteClient = new OnvifLiteClient(this);
    m_onvifLiteClient->setDiscoveryTimeoutMs(config.onvifDiscoveryTimeoutMs);
    m_onvifLiteClient->setManualXAddr(config.onvifManualXAddr);

    m_deviceService = new DeviceService(m_restClient, m_onvifLiteClient, this);
    m_deviceService->setDevicesPath(config.devicesPath);
    m_deviceService->setDeviceChannelsPathTemplate(config.deviceChannelsPathTemplate);
    m_deviceService->setChannelDetailPathTemplate(config.channelDetailPathTemplate);
    m_deviceService->setDeviceSource(config.deviceSource);

    m_cctvControlService = new CctvControlService(m_onvifLiteClient, this);

    m_playbackService = new PlaybackService(m_restClient, this);
    m_playbackService->setChannelsByDatePathTemplate(config.playbackChannelsByDatePathTemplate);
    m_playbackService->setTimelinePath(config.playbackTimelinePath);
    m_playbackService->setStreamPath(config.playbackStreamPath);
    m_playbackService->setExportPath(config.playbackExportPath);
    m_playbackService->setExportStatusPathTemplate(config.playbackExportStatusPathTemplate);

    m_ugvService = new UgvService(this);
    m_ugvService->setWsUrl(config.ugvGatewayWsUrl);
    m_ugvService->setWsSubprotocol(config.ugvGatewayWsSubprotocol);
    m_ugvService->setAutoReconnectEnabled(false);
    m_ugvService->setAccessToken(state.accessToken);
    m_ugvMapMinLat = config.ugvMapMinLat;
    m_ugvMapMaxLat = config.ugvMapMaxLat;
    m_ugvMapMinLon = config.ugvMapMinLon;
    m_ugvMapMaxLon = config.ugvMapMaxLon;

    m_eventService = new EventService(m_restClient, this);
    m_eventService->setEventsPath(config.eventEventsPath);
    m_eventService->setEventDetailPathTemplate(config.eventDetailPathTemplate);
    EventUiHelpers::setEventService(m_eventService);
    m_wsClient = new WsClient(this);
    m_wsClient->setUrl(config.eventWsUrl);
    m_wsClient->setSubprotocol(config.eventWsSubprotocol);
    m_eventPingTimer = new QTimer(this);
    m_eventPingTimer->setInterval(30 * 1000);
    connect(m_wsClient, &WsClient::connected, this, [this]() {
        if (!m_eventService) {
            return;
        }
        if (!AppState::instance().isAuthenticated) {
            return;
        }
        const QString lastEventId = m_eventService->lastEventId();
        const QString requestId = QStringLiteral("sub-%1")
            .arg(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
        QJsonObject subscribe;
        subscribe.insert(QStringLiteral("type"), QStringLiteral("subscribe"));
        subscribe.insert(QStringLiteral("reqId"), requestId);
        if (!lastEventId.isEmpty()) {
            QJsonObject resume;
            resume.insert(QStringLiteral("lastEventId"), lastEventId);
            subscribe.insert(QStringLiteral("resumeFrom"), resume);
        }
        m_wsClient->sendJson(subscribe);
        m_eventPingTimer->start();
        if (m_skipNextWsRecovery) {
            m_skipNextWsRecovery = false;
            return;
        }
        m_eventService->fetchRecentEvents(this);
    });
    connect(m_wsClient, &WsClient::disconnected, this, [this]() {
        if (m_eventPingTimer) {
            m_eventPingTimer->stop();
        }
        m_eventSubscriptionId.clear();
    });
    if (m_eventPingTimer) {
        connect(m_eventPingTimer, &QTimer::timeout, this, [this]() {
            if (!m_wsClient || !m_wsClient->isConnected()) {
                return;
            }
            QJsonObject ping;
            ping.insert(QStringLiteral("type"), QStringLiteral("ping"));
            ping.insert(QStringLiteral("ts"),
                        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            m_wsClient->sendJson(ping);
        });
    }
    if (m_eventService) {
        connect(m_wsClient, &WsClient::jsonMessageReceived, this,
                [this](const QJsonObject &message) {
                    if (!m_eventService) {
                        return;
                    }
                    if (!AppState::instance().isAuthenticated) {
                        return;
                    }
                    const QString subscriptionId =
                        message.value(QStringLiteral("subscriptionId")).toString();
                    if (!subscriptionId.isEmpty()) {
                        m_eventSubscriptionId = subscriptionId;
                    }
                    m_eventService->ingestWsMessage(message);
                });
    }
    return true;
}

void MainWindow::configureLoginScreenState()
{
    // 로그인 화면은 "infra가 아예 준비되지 않은 상태"도 표현해야 한다.
    // 즉 로그인 가능 상태면 입력/상태를 초기화하고, 아니면 설정 오류 문구를 화면에 남긴다.
    // 따라서 단순 입력 초기화가 아니라, 설정 파일 오류를 사용자에게
    // 로그인 불가 사유로 보여주는 마지막 안전망 역할을 맡는다.
    if (!m_loginScreen) {
        return;
    }
    if (m_authInfraReady) {
        m_loginScreen->setLoginInProgress(false);
        m_loginScreen->clearLoginStatus();
        return;
    }

    const QString msg = AppState::instance().authConfigError.trimmed().isEmpty()
        ? "설정 오류로 로그인할 수 없습니다. app_config.json을 확인해 주세요."
        : AppState::instance().authConfigError;
    m_loginScreen->showConfigError(msg);
}

void MainWindow::handleUnauthorized()
{
    if (m_ugvScreen) {
        m_ugvScreen->prepareForShutdown();
    }
    showScreen(ScreenId::Login);
    clearAuthenticationState();
}

void MainWindow::clearAuthenticationState()
{
    // 로그아웃/401 복귀에서 가장 중요한 함수다.
    // 실제로는 인증 정보, 선택 채널, 런타임 화면, WebSocket/UGV/미디어 세션을 모두 정리한다.
    // 화면만 로그인으로 돌리는 것이 아니라:
    // - 토큰/선택 채널/AppState
    // - 이벤트/UGV/WebSocket 서비스 상태
    // - 미디어 세션(ChannelSessionManager)
    // 을 함께 초기화해 "이전 런타임의 잔상"이 남지 않게 만든다.
    auto &state = AppState::instance();
    state.isAuthenticated = false;
    state.currentUserId.clear();
    state.accessToken.clear();
    state.playbackAutoStartRequested = false;
    state.playbackTargetChannelId = -1;
    state.playbackTargetChannel.clear();
    state.playbackTargetDate.clear();
    state.selectedChannelContexts.clear();
    state.channelRtspByName.clear();
    state.channelRtspById.clear();
    state.channelOnvifPtzXAddrById.clear();
    state.channelOnvifImagingXAddrById.clear();
    state.channelOnvifProfileTokenById.clear();
    state.channelOnvifUsernameById.clear();
    state.channelOnvifPasswordById.clear();
    state.deviceCredentialUsernameByIp.clear();
    state.deviceCredentialPasswordByIp.clear();
    state.clearAllGridCells();
    state.activeChannel.clear();
    state.activeCctvChannelId = -1;
    state.activeUgvGatewayId = -1;
    state.activeUgvChannelId = -1;
    if (m_eventService) {
        m_eventService->reset();
    }
    if (m_restClient) {
        m_restClient->clearAccessToken();
    }
    if (m_wsClient && m_wsClient->isConnected() && !m_eventSubscriptionId.isEmpty()) {
        QJsonObject unsubscribe;
        unsubscribe.insert(QStringLiteral("type"), QStringLiteral("unsubscribed"));
        unsubscribe.insert(QStringLiteral("reqId"),
                           QStringLiteral("unsub-%1")
                               .arg(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()));
        unsubscribe.insert(QStringLiteral("subscriptionId"), m_eventSubscriptionId);
        m_wsClient->sendJson(unsubscribe);
        m_eventSubscriptionId.clear();
    }
    if (m_wsClient) {
        m_wsClient->disconnectFromServer();
    }
    if (m_ugvService) {
        m_ugvService->setAccessToken(QString());
        m_ugvService->shutdown();
    }
    ChannelSessionManager::instance().shutdown();
    destroyRuntimeScreens();
    m_skipNextWsRecovery = false;
    if (m_loginScreen) {
        m_loginScreen->resetLoginInputs();
    }
}

void MainWindow::handleLogoutRequest()
{
    auto &clipMgr = ClipCaptureManager::instance();
    const bool clipOn = (clipMgr.state() != ClipCaptureManager::State::Idle);
    const bool exportOn = (m_playbackScreen && m_playbackScreen->isExportBusy());
    QString message;
    if (clipOn && exportOn) {
        message = "클립 저장과 내보내기 작업이 진행 중입니다. 로그아웃하면 현재 작업은 저장되지 않고 중단됩니다. 계속하시겠습니까?";
    } else if (clipOn) {
        message = "클립 저장이 진행 중입니다. 로그아웃하면 현재 클립은 저장되지 않고 중단됩니다. 계속하시겠습니까?";
    } else if (exportOn) {
        message = "내보내기 작업이 진행 중입니다. 로그아웃하면 현재 작업은 중단됩니다. 계속하시겠습니까?";
    } else {
        message = "로그아웃 하시겠습니까?";
    }
    if (!PopupManager::confirm(this, "로그아웃", message)) {
        return;
    }
    if (clipOn) {
        clipMgr.discard();
        PopupManager::showInfo(this, "클립 저장", "클립 저장이 취소되었습니다.");
    }
    if (exportOn && m_playbackScreen) {
        m_playbackScreen->cancelExportOperations(true);
    }
    if (m_ugvScreen) {
        m_ugvScreen->prepareForShutdown();
    }
    if (m_authService) {
        m_authService->logout(this);
    }
    showScreen(ScreenId::Login);
    clearAuthenticationState();
}



