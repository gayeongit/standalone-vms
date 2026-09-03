#ifndef STREAM_PLAYER_H
#define STREAM_PLAYER_H

#include <QObject>
#include <QImage>
#include <QtGlobal>
#include <QMutex>
#include <QPointer>
#include <QSharedPointer>
#include <QString>
#include <QSet>
#include <QVector>

#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#endif

class QWidget;
class VideoRenderWidget;
class QEvent;

enum class StreamStatus {
    Idle,
    Connecting,
    Playing,
    Error
};

enum class StreamQualityProfile {
    Normal = 0,
    QuadGrid = 1,
    DenseGrid = 2
};

class StreamPlayer : public QObject
{
    Q_OBJECT
public:
    explicit StreamPlayer(QObject *parent = nullptr);
    ~StreamPlayer() override;

    // Backward-compatible single-target API.
    void setRenderWidget(QWidget *widget);
    // Multi-target APIs for channel fan-out reuse.
    void bindRenderWidget(QWidget *widget);
    void unbindRenderWidget(QWidget *widget);
    void clearRenderWidgets();

    void setSource(const QString &url);
    void setVideoCodecHint(const QString &codec);
    void setQualityProfile(StreamQualityProfile profile);
    void start();
    void stop();
    void setPaused(bool paused);
    bool isPaused() const;
    void seekToMs(qint64 positionMs);
    void setPlaybackRate(double rate);
    qint64 positionMs() const;
    qint64 durationMs() const;
    bool canSeek() const;
    bool canChangePlaybackRate() const;
    QSharedPointer<const QImage> latestFrameShared() const;
    QImage latestFrameCopy() const;

    StreamStatus status() const;

signals:
    void statusChanged(StreamStatus status);
    void errorOccurred(const QString &message);
    void eosReached();

private slots:
    void pollBus();
    void restartAfterFailure();

private:
    struct RenderBinding {
        QPointer<QWidget> host;
        QPointer<VideoRenderWidget> view;
    };

    VideoRenderWidget *ensureRenderTargetForHost(QWidget *host);
    void syncRenderTargetGeometry(QWidget *host);
    void clearBindingView(const RenderBinding &binding);
    QVector<QPointer<VideoRenderWidget>> snapshotRenderTargets() const;
    void setStatus(StreamStatus status);
    void destroyPipeline();
    bool applyQualityProfileCaps(StreamQualityProfile profile);
    void publishSampleFrame(struct _GstSample *sample);
    bool eventFilter(QObject *watched, QEvent *event) override;

    QVector<RenderBinding> m_renderBindings;
    mutable QMutex m_renderBindingsMutex;
    QSet<quintptr> m_renderDispatchPending;
    mutable QMutex m_renderDispatchMutex;
    QString m_sourceUrl;
    QString m_videoCodecHint;
    StreamQualityProfile m_qualityProfile = StreamQualityProfile::Normal;
    StreamStatus m_status = StreamStatus::Idle;
    bool m_manualStop = false;
    bool m_paused = false;
    qint64 m_lastHiddenLatestFrameUpdateMs = 0;
    QSharedPointer<const QImage> m_latestFrame;
    mutable QMutex m_latestFrameMutex;
    QString m_lastLoggedNegotiatedCaps;
    mutable QMutex m_negotiatedCapsLogMutex;

    class QTimer *m_busTimer = nullptr;
    class QTimer *m_reconnectTimer = nullptr;

#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
    static GstFlowReturn onNewSample(GstAppSink *sink, gpointer userData);

    GstElement *m_pipeline = nullptr;
    GstElement *m_appSink = nullptr;
    GstElement *m_qualityCapsFilter = nullptr;
#endif
};

#endif // STREAM_PLAYER_H
