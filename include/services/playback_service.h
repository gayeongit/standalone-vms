#ifndef PLAYBACK_SERVICE_H
#define PLAYBACK_SERVICE_H

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class RestClient;
class QJsonArray;
class QJsonObject;

struct PlaybackChannelSummary
{
    int channelId = -1;
    QString name;
};

struct PlaybackTimeRange
{
    QString from;
    QString to;
};

struct PlaybackMarker
{
    QString ts;
    QString eventId;
    QString type;
    int channelId = -1;
};

struct PlaybackChannelsResult
{
    bool ok = false;
    int httpStatus = 0;
    QString errorMessage;
    QString date;
    QVector<PlaybackChannelSummary> channels;
};

struct PlaybackTimelineResult
{
    bool ok = false;
    int httpStatus = 0;
    QString errorMessage;
    QString date;
    int channelId = -1;
    QVector<PlaybackTimeRange> availableRanges;
    QVector<PlaybackTimeRange> gaps;
    QVector<PlaybackMarker> eventMarkers;
};

struct PlaybackStreamResult
{
    bool ok = false;
    int httpStatus = 0;
    QString errorMessage;
    int channelId = -1;
    QString protocol;
    QString uri;
    QString absoluteUri;
    QString ts;
};

struct PlaybackExportStartResult
{
    bool ok = false;
    int httpStatus = 0;
    QString errorMessage;
    QString jobId;
    QString status;
};

struct PlaybackExportStatusResult
{
    bool ok = false;
    int httpStatus = 0;
    QString errorMessage;
    QString jobId;
    QString status;
    QString uri;
    QString absoluteUri;
    QString fileName;
    QString message;
};

class PlaybackService : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackService(RestClient *restClient, QObject *parent = nullptr);

    void setChannelsByDatePathTemplate(const QString &pathTemplate);
    void setTimelinePath(const QString &path);
    void setStreamPath(const QString &path);
    void setExportPath(const QString &path);
    void setExportStatusPathTemplate(const QString &pathTemplate);

    void fetchAvailableChannels(
        const QString &date,
        QObject *context,
        std::function<void(const PlaybackChannelsResult &)> callback);
    void fetchTimeline(
        int channelId,
        const QString &date,
        QObject *context,
        std::function<void(const PlaybackTimelineResult &)> callback);
    void requestStream(
        int channelId,
        const QString &timestamp,
        QObject *context,
        std::function<void(const PlaybackStreamResult &)> callback);
    void requestExport(
        int channelId,
        const QString &startTimestamp,
        const QString &endTimestamp,
        const QString &format,
        QObject *context,
        std::function<void(const PlaybackExportStartResult &)> callback);
    void fetchExportStatus(
        const QString &jobId,
        QObject *context,
        std::function<void(const PlaybackExportStatusResult &)> callback);

private:
    QVector<PlaybackChannelSummary> parseChannelArray(const QJsonArray &array) const;
    PlaybackChannelSummary parseChannelObject(const QJsonObject &obj) const;
    QVector<PlaybackTimeRange> parseRangeArray(const QJsonArray &array) const;
    PlaybackTimeRange parseRangeObject(const QJsonObject &obj) const;
    QVector<PlaybackMarker> parseMarkerArray(const QJsonArray &array, int channelId) const;
    PlaybackMarker parseMarkerObject(const QJsonObject &obj, int channelId) const;
    QString resolvePathTemplate(const QString &pathTemplate, const QString &placeholder, const QString &value) const;
    QString buildTimelineQueryPath(int channelId, const QString &date) const;
    QString buildStreamQueryPath(int channelId, const QString &timestamp) const;
    QString buildExportStatusPath(const QString &jobId) const;
    QString resolvePlaybackUri(const QString &uri) const;

    RestClient *m_restClient = nullptr;
    QString m_channelsByDatePathTemplate = "/playback/dates/{date}/channels";
    QString m_timelinePath = "/playback/timeline";
    QString m_streamPath = "/playback/stream";
    QString m_exportPath = "/playback/export";
    QString m_exportStatusPathTemplate = "/playback/export/{jobId}";
};

#endif // PLAYBACK_SERVICE_H
