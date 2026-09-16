#ifndef APP_STATE_H
#define APP_STATE_H

#include "selected_channel_context.h"

#include <array>
#include <cstddef>
#include <QHash>
#include <QString>
#include <QVector>

enum class ScreenId {
    Login,
    Signup,
    DeviceCheck,
    Main,
    Cctv,
    Ugv,
    Playback
};

struct MainGridCellState
{
    QString displayName;
    int channelId = -1;
    int deviceId = -1;

    bool isEmpty() const
    {
        return displayName.trimmed().isEmpty();
    }

    void clear()
    {
        displayName.clear();
        channelId = -1;
        deviceId = -1;
    }
};

class AppState
{
public:
    static AppState &instance();

    ScreenId currentScreen = ScreenId::Login;
    bool isAuthenticated = false;
    bool authConfigReady = false;
    QString activeChannel;
    int activeCctvChannelId = -1;
    int activeUgvGatewayId = -1;
    int activeUgvChannelId = -1;
    QString accessToken;
    QString apiBaseUrl;
    QString currentUserId;
    QString authConfigError;
    bool playbackAutoStartRequested = false;
    int playbackTargetChannelId = -1;
    QString playbackTargetChannel;
    QString playbackTargetDate;
    std::array<MainGridCellState, 9> gridCells{};
    QVector<SelectedChannelContext> selectedChannelContexts;
    QHash<QString, QString> channelRtspByName;
    QHash<int, QString> channelRtspById;
    QHash<QString, QString> channelVideoCodecByName;
    QHash<int, QString> channelVideoCodecById;
    QHash<int, QString> channelOnvifPtzXAddrById;     // Phase 2: RelativeMove(zoom) 대상 PTZ Service 주소
    QHash<int, QString> channelOnvifImagingXAddrById; // Phase 2: Move(focus) 대상 Imaging Service 주소
    QHash<int, QString> channelOnvifProfileTokenById; // Phase 2: RelativeMove/Move에 넘길 프로필 토큰

    // Phase 3b: 자격증명 세션 캐시. deviceId(int)는 새로고침마다 재할당되어 불안정하므로
    // deviceIp(문자열)를 키로 쓴다 — QtKeychain 영구 저장 키와도 동일하게 맞춤.
    QHash<QString, QString> deviceCredentialUsernameByIp;
    QHash<QString, QString> deviceCredentialPasswordByIp;
    // 위 세션 캐시에서 finalize() 시점에 복제해두는 channelId 키 버전 — RTSP 임베드/PTZ 요청에서
    // channelOnvifPtzXAddrById 등과 동일한 방식으로 바로 조회할 수 있게.
    QHash<int, QString> channelOnvifUsernameById;
    QHash<int, QString> channelOnvifPasswordById;

    void setGridCell(std::size_t index, const QString &displayName, int channelId, int deviceId)
    {
        if (index >= gridCells.size()) {
            return;
        }

        MainGridCellState &cell = gridCells[index];
        cell.displayName = displayName;
        cell.channelId = channelId;
        cell.deviceId = deviceId;
        if (cell.isEmpty()) {
            cell.clear();
        }
    }

    void clearGridCell(std::size_t index)
    {
        if (index >= gridCells.size()) {
            return;
        }

        gridCells[index].clear();
    }

    void clearAllGridCells()
    {
        for (std::size_t i = 0; i < gridCells.size(); ++i) {
            clearGridCell(i);
        }
    }

private:
    AppState() = default;
};

#endif // APP_STATE_H
