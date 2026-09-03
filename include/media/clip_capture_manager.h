#ifndef CLIP_CAPTURE_MANAGER_H
#define CLIP_CAPTURE_MANAGER_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

class QWidget;
class QImage;

class ClipCaptureManager : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Idle,
        Recording,
        Encoding
    };

    enum class EncodeError {
        None,
        Cancelled,
        AlreadyEncoding,
        NoFrames,
        SaveDirectoryInvalid,
        NoValidFrames,
        FfmpegNotFound,
        FfmpegStartFailed,
        FfmpegTimeout,
        FfmpegEncodeFailed
    };

    struct EncodeResult {
        bool ok = false;
        EncodeError code = EncodeError::None;
        QString message;
        QString outputPath;
        QString pathTag;
    };

    struct EncodeSnapshot {
        QVector<QImage> frames;
        QString outputDirectory;
        QString stamp;
        std::shared_ptr<std::atomic_bool> cancelRequested;
    };

    static ClipCaptureManager &instance();

    bool start(QWidget *sourceWidget);
    bool startChannel(const QString &channelName);
    void stop();
    void discard();
    EncodeResult prepareEncoding(EncodeSnapshot *snapshot);
    static EncodeResult encodeSnapshot(const EncodeSnapshot &snapshot);
    void finishEncoding(const std::shared_ptr<std::atomic_bool> &cancelToken);
    EncodeResult stopAndEncode();
    bool stopAndEncode(QString *outputPath, QString *errorMessage);
    void setSourceWidget(QWidget *sourceWidget);
    void setSourceChannel(const QString &channelName);

    bool isRecording() const;
    bool isEncoding() const;
    State state() const;
    qint64 elapsedSeconds() const;
    int capturedFrameCount() const;

signals:
    void recordingChanged(bool recording);
    void stateChanged(ClipCaptureManager::State state);

private slots:
    void captureFrame();

private:
    explicit ClipCaptureManager(QObject *parent = nullptr);
    void setState(State state);

    enum class SourceType {
        None,
        WidgetComposite,
        ChannelFrame
    };

    QPointer<QWidget> m_sourceWidget;
    QString m_sourceChannel;
    SourceType m_sourceType = SourceType::None;
    class QTimer *m_frameTimer = nullptr;
    QVector<QImage> m_capturedFrames;
    qint64 m_startMs = 0;
    int m_capturedFrameCount = 0;
    State m_state = State::Idle;
    std::shared_ptr<std::atomic_bool> m_activeEncodeCancelToken;
};

#endif // CLIP_CAPTURE_MANAGER_H
