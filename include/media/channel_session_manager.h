#ifndef CHANNEL_SESSION_MANAGER_H
#define CHANNEL_SESSION_MANAGER_H

#include "stream_player.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QSet>
#include <QString>
#include <QVector>

class QWidget;

class ChannelSessionManager : public QObject
{
    Q_OBJECT
public:
    static ChannelSessionManager &instance();

    void bindChannelToWidget(const QString &channel, QWidget *widget, StreamQualityProfile profile = StreamQualityProfile::Normal);
    void unbindChannelFromWidget(const QString &channel, QWidget *widget);
    StreamStatus statusForChannel(const QString &channel) const;
    QSharedPointer<const QImage> latestFrameForChannelShared(const QString &channel) const;
    QImage latestFrameForChannelCopy(const QString &channel) const;
    void applyActiveChannels(const QSet<QString> &activeChannels);
    void shutdown();

private:
    explicit ChannelSessionManager(QObject *parent = nullptr);

    struct Session {
        StreamPlayer *player = nullptr;
        QString sourceUrl;
        QVector<QPointer<QWidget>> hosts;
        QHash<quintptr, StreamQualityProfile> hostProfiles;
    };

    void pruneHosts(Session &session);
    Session &ensureSession(const QString &channel, const QString &url);
    void refreshEffectiveProfile(Session &session);

    QHash<QString, Session> m_sessions;
    QSet<QString> m_lastActiveChannels;
    bool m_hasAppliedActiveChannels = false;
};

#endif // CHANNEL_SESSION_MANAGER_H
