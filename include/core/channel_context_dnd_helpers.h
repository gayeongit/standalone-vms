#ifndef CHANNEL_CONTEXT_DND_HELPERS_H
#define CHANNEL_CONTEXT_DND_HELPERS_H

#include "selected_channel_context.h"

#include <QString>

class QMimeData;

struct DroppedChannelInfo
{
    QString displayName;
    int channelId = -1;
    int deviceId = -1;
    QString deviceType;

    bool isValid() const
    {
        return !displayName.trimmed().isEmpty();
    }
};

bool isUgvChannelName(const QString &channelName);
bool isSelectedDevice(const QString &deviceName);
QString firstSelectedChannelByType(const QString &deviceType);
int firstSelectedChannelIdByType(const QString &deviceType);
int selectedChannelIdForDisplayName(const QString &displayName, const QString &deviceType = QString());
int selectedChannelIdForDisplayNameExact(const QString &displayName, const QString &deviceType = QString());
QString displayNameForChannelId(int channelId);
bool findSelectedChannelContextByChannelId(int channelId, SelectedChannelContext *outContext = nullptr);
// Resolves the current CCTV target and normalizes AppState::activeCctvChannelId/activeChannel.
// This is intentionally stateful and should not be treated as a pure read-only query helper.
bool resolveAndNormalizeActiveCctvTarget(int *channelId = nullptr, QString *displayName = nullptr);
// Backward-compatible alias. Prefer resolveAndNormalizeActiveCctvTarget() in new code.
bool resolveActiveCctvTarget(int *channelId = nullptr, QString *displayName = nullptr);
int deviceIdForChannelId(int channelId);
QString rtspUrlForChannelId(int channelId);
QString rtspUrlForDisplayName(const QString &displayName, const QString &deviceType = QString());
QString videoCodecForChannelId(int channelId);
QString videoCodecForDisplayName(const QString &displayName, const QString &deviceType = QString());
QString rtspUrlForChannel(const QString &channelName);
QString extractDroppedChannel(const QMimeData *mimeData);
DroppedChannelInfo extractDroppedChannelInfo(const QMimeData *mimeData);

#endif // CHANNEL_CONTEXT_DND_HELPERS_H
