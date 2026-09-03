#include "channel_context_dnd_helpers.h"

#include "app_state.h"

#include <QDataStream>
#include <QMap>
#include <QMimeData>
#include <QSettings>
#include <QVariant>

bool isUgvChannelName(const QString &channelName)
{
    return channelName.contains("UGV", Qt::CaseInsensitive);
}

bool isSelectedDevice(const QString &deviceName)
{
    const auto &contexts = AppState::instance().selectedChannelContexts;
    for (const auto &ctx : contexts) {
        if (ctx.displayName == deviceName) {
            return true;
        }
    }
    return false;
}

QString firstSelectedChannelByType(const QString &deviceType)
{
    const QString normalized = deviceType.trimmed().toUpper();
    const auto &contexts = AppState::instance().selectedChannelContexts;
    if (contexts.isEmpty()) {
        return {};
    }

    for (const auto &ctx : contexts) {
        if (ctx.deviceType.trimmed().toUpper() == normalized && ctx.online && !ctx.displayName.trimmed().isEmpty()) {
            return ctx.displayName.trimmed();
        }
    }
    for (const auto &ctx : contexts) {
        if (ctx.deviceType.trimmed().toUpper() == normalized && !ctx.displayName.trimmed().isEmpty()) {
            return ctx.displayName.trimmed();
        }
    }
    return {};
}

int firstSelectedChannelIdByType(const QString &deviceType)
{
    const QString normalized = deviceType.trimmed().toUpper();
    const auto &contexts = AppState::instance().selectedChannelContexts;
    if (contexts.isEmpty()) {
        return -1;
    }

    for (const auto &ctx : contexts) {
        if (ctx.channelId < 0) {
            continue;
        }
        if (ctx.deviceType.trimmed().toUpper() == normalized && ctx.online) {
            return ctx.channelId;
        }
    }
    for (const auto &ctx : contexts) {
        if (ctx.channelId < 0) {
            continue;
        }
        if (ctx.deviceType.trimmed().toUpper() == normalized) {
            return ctx.channelId;
        }
    }
    return -1;
}

int selectedChannelIdForDisplayName(const QString &displayName, const QString &deviceType)
{
    const QString targetName = displayName.trimmed();
    const QString normalizedType = deviceType.trimmed().toUpper();
    const auto &contexts = AppState::instance().selectedChannelContexts;

    for (const auto &ctx : contexts) {
        if (ctx.channelId < 0) {
            continue;
        }
        if (!targetName.isEmpty() && ctx.displayName.trimmed() == targetName) {
            if (normalizedType.isEmpty() || ctx.deviceType.trimmed().toUpper() == normalizedType) {
                return ctx.channelId;
            }
        }
    }
    if (normalizedType.isEmpty()) {
        return -1;
    }
    for (const auto &ctx : contexts) {
        if (ctx.channelId < 0) {
            continue;
        }
        if (ctx.deviceType.trimmed().toUpper() == normalizedType && ctx.online) {
            return ctx.channelId;
        }
    }
    for (const auto &ctx : contexts) {
        if (ctx.channelId < 0) {
            continue;
        }
        if (ctx.deviceType.trimmed().toUpper() == normalizedType) {
            return ctx.channelId;
        }
    }
    return -1;
}

int selectedChannelIdForDisplayNameExact(const QString &displayName, const QString &deviceType)
{
    const QString targetName = displayName.trimmed();
    const QString normalizedType = deviceType.trimmed().toUpper();
    if (targetName.isEmpty()) {
        return -1;
    }
    const auto &contexts = AppState::instance().selectedChannelContexts;
    for (const auto &ctx : contexts) {
        if (ctx.channelId < 0) {
            continue;
        }
        if (ctx.displayName.trimmed() != targetName) {
            continue;
        }
        if (!normalizedType.isEmpty() && ctx.deviceType.trimmed().toUpper() != normalizedType) {
            continue;
        }
        return ctx.channelId;
    }
    return -1;
}

QString displayNameForChannelId(int channelId)
{
    if (channelId < 0) {
        return {};
    }
    const auto &contexts = AppState::instance().selectedChannelContexts;
    for (const auto &ctx : contexts) {
        if (ctx.channelId == channelId) {
            return ctx.displayName.trimmed();
        }
    }
    return {};
}

bool findSelectedChannelContextByChannelId(int channelId, SelectedChannelContext *outContext)
{
    if (channelId < 0) {
        return false;
    }
    const auto &contexts = AppState::instance().selectedChannelContexts;
    for (const auto &ctx : contexts) {
        if (ctx.channelId == channelId) {
            if (outContext) {
                *outContext = ctx;
            }
            return true;
        }
    }
    return false;
}

bool resolveAndNormalizeActiveCctvTarget(int *channelId, QString *displayName)
{
    // NOTE: This helper intentionally normalizes active CCTV state in AppState.
    // It is used by both warm-log and actual bind paths so they share one rule set.
    auto &state = AppState::instance();
    int resolvedChannelId = -1;
    QString resolvedDisplayName;

    if (state.activeCctvChannelId > 0) {
        SelectedChannelContext ctx;
        if (findSelectedChannelContextByChannelId(state.activeCctvChannelId, &ctx)
            && ctx.deviceType.trimmed().compare(QStringLiteral("CCTV"), Qt::CaseInsensitive) == 0) {
            resolvedChannelId = state.activeCctvChannelId;
            resolvedDisplayName = displayNameForChannelId(resolvedChannelId);
            if (resolvedDisplayName.trimmed().isEmpty()) {
                resolvedDisplayName = ctx.displayName.trimmed();
            }
        } else {
            state.activeCctvChannelId = -1;
        }
    }

    if (resolvedDisplayName.isEmpty()) {
        const QString activeFromState = state.activeChannel.trimmed();
        if (!activeFromState.isEmpty() && !isUgvChannelName(activeFromState)) {
            const int channelIdFromName =
                selectedChannelIdForDisplayNameExact(activeFromState, QStringLiteral("CCTV"));
            if (channelIdFromName > 0) {
                resolvedChannelId = channelIdFromName;
                resolvedDisplayName = displayNameForChannelId(channelIdFromName);
                if (resolvedDisplayName.trimmed().isEmpty()) {
                    resolvedDisplayName = activeFromState;
                }
            } else if (!rtspUrlForDisplayName(activeFromState, QStringLiteral("CCTV")).isEmpty()) {
                // Allow manual custom CCTV entries that are not in selectedChannelContexts.
                resolvedChannelId = -1;
                resolvedDisplayName = activeFromState;
            }
        }
    }

    if (resolvedDisplayName.isEmpty()) {
        const int firstCctvChannelId = firstSelectedChannelIdByType(QStringLiteral("CCTV"));
        if (firstCctvChannelId > 0) {
            resolvedChannelId = firstCctvChannelId;
            resolvedDisplayName = displayNameForChannelId(firstCctvChannelId);
            if (resolvedDisplayName.trimmed().isEmpty()) {
                SelectedChannelContext ctx;
                if (findSelectedChannelContextByChannelId(firstCctvChannelId, &ctx)) {
                    resolvedDisplayName = ctx.displayName.trimmed();
                }
            }
        }
    }

    state.activeCctvChannelId = resolvedChannelId > 0 ? resolvedChannelId : -1;
    if (!resolvedDisplayName.trimmed().isEmpty()) {
        state.activeChannel = resolvedDisplayName.trimmed();
    }

    if (channelId) {
        *channelId = state.activeCctvChannelId;
    }
    if (displayName) {
        *displayName = resolvedDisplayName.trimmed();
    }
    return state.activeCctvChannelId > 0;
}

bool resolveActiveCctvTarget(int *channelId, QString *displayName)
{
    return resolveAndNormalizeActiveCctvTarget(channelId, displayName);
}

int deviceIdForChannelId(int channelId)
{
    SelectedChannelContext ctx;
    return findSelectedChannelContextByChannelId(channelId, &ctx) ? ctx.deviceId : -1;
}

QString rtspUrlForChannelId(int channelId)
{
    if (channelId < 0) {
        return {};
    }
    return AppState::instance().channelRtspById.value(channelId).trimmed();
}

QString rtspUrlForDisplayName(const QString &displayName, const QString &deviceType)
{
    const QString trimmedName = displayName.trimmed();
    if (trimmedName.isEmpty()) {
        return {};
    }

    const int channelId = selectedChannelIdForDisplayName(trimmedName, deviceType);
    if (channelId >= 0) {
        const QString byId = rtspUrlForChannelId(channelId);
        if (!byId.isEmpty()) {
            return byId;
        }
    }

    const QString byName = AppState::instance().channelRtspByName.value(trimmedName).trimmed();
    if (!byName.isEmpty()) {
        return byName;
    }

    const QString normalizedType = deviceType.trimmed().toUpper();
    QSettings settings("TeamClue", "VMS_v1");
    const int size = settings.beginReadArray("devices");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        const QString name = settings.value("name").toString().trimmed();
        if (name != trimmedName) {
            continue;
        }
        QString rowType = settings.value("type").toString().trimmed().toUpper();
        if (rowType.isEmpty()) {
            rowType = QStringLiteral("CCTV");
        }
        if (!normalizedType.isEmpty() && rowType != normalizedType) {
            continue;
        }
        const QString url = settings.value("url").toString().trimmed();
        if (!url.isEmpty()) {
            settings.endArray();
            return url;
        }
    }
    settings.endArray();

    return {};
}

QString videoCodecForChannelId(int channelId)
{
    if (channelId < 0) {
        return {};
    }
    return AppState::instance().channelVideoCodecById.value(channelId).trimmed();
}

QString videoCodecForDisplayName(const QString &displayName, const QString &deviceType)
{
    const int channelId = selectedChannelIdForDisplayName(displayName, deviceType);
    if (channelId >= 0) {
        const QString byId = videoCodecForChannelId(channelId);
        if (!byId.isEmpty()) {
            return byId;
        }
    }
    return AppState::instance().channelVideoCodecByName.value(displayName.trimmed()).trimmed();
}

QString rtspUrlForChannel(const QString &channelName)
{
    const QString runtimeRtsp = rtspUrlForDisplayName(channelName);
    if (!runtimeRtsp.isEmpty()) {
        return runtimeRtsp;
    }

    QSettings settings("TeamClue", "VMS_v1");
    const int size = settings.beginReadArray("devices");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        const QString name = settings.value("name").toString().trimmed();
        if (name != channelName) {
            continue;
        }
        const QString url = settings.value("url").toString().trimmed();
        if (!url.isEmpty()) {
            settings.endArray();
            return url;
        }
    }
    settings.endArray();

    return {};
}

QString extractDroppedChannel(const QMimeData *mimeData)
{
    return extractDroppedChannelInfo(mimeData).displayName;
}

DroppedChannelInfo extractDroppedChannelInfo(const QMimeData *mimeData)
{
    DroppedChannelInfo info;
    if (!mimeData) {
        return info;
    }
    static const QString modelDataFormat = "application/x-qabstractitemmodeldatalist";
    if (mimeData->hasFormat(modelDataFormat)) {
        const QByteArray encoded = mimeData->data(modelDataFormat);
        QDataStream stream(encoded);
        while (!stream.atEnd()) {
            int row = 0;
            int column = 0;
            QMap<int, QVariant> roleDataMap;
            stream >> row >> column >> roleDataMap;
            Q_UNUSED(row);
            Q_UNUSED(column);
            const QString roleDisplayName = roleDataMap.value(Qt::UserRole + 2).toString().trimmed();
            info.displayName = roleDisplayName.isEmpty()
                ? roleDataMap.value(Qt::DisplayRole).toString().trimmed()
                : roleDisplayName;
            info.channelId = roleDataMap.value(Qt::UserRole).toInt();
            info.deviceId = roleDataMap.value(Qt::UserRole + 1).toInt();
            info.deviceType = roleDataMap.value(Qt::UserRole + 3).toString().trimmed();
            if (info.isValid()) {
                return info;
            }
        }
    }

    if (mimeData->hasText()) {
        info.displayName = mimeData->text().trimmed();
        return info;
    }

    return info;
}
