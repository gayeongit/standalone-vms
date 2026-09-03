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
