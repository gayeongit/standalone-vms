#include "clip_capture_manager.h"

#include "channel_session_manager.h"
#include "common_ui.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>

namespace {

QString resolveFfmpegExecutable()
{
    // 1순위: exe 옆에 번들된 ffmpeg.exe (배포 환경)
    const QString bundled = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
    if (QFileInfo::exists(bundled)) {
        return bundled;
    }
    // 2순위: 시스템 PATH (개발 환경)
    const QString fromPath = QStandardPaths::findExecutable("ffmpeg");
    if (!fromPath.isEmpty()) {
        return fromPath;
    }
    return {};
}

bool isValidFrameForEncode(const QImage &frame)
{
    if (frame.isNull()) {
        return false;
    }
    if (frame.width() <= 0 || frame.height() <= 0) {
        return false;
    }
    if (frame.width() > 8192 || frame.height() > 8192) {
        return false;
    }
    return true;
}

QString encodeErrorName(ClipCaptureManager::EncodeError code)
{
    switch (code) {
    case ClipCaptureManager::EncodeError::None:
        return QStringLiteral("none");
    case ClipCaptureManager::EncodeError::Cancelled:
        return QStringLiteral("cancelled");
    case ClipCaptureManager::EncodeError::AlreadyEncoding:
        return QStringLiteral("already_encoding");
    case ClipCaptureManager::EncodeError::NoFrames:
        return QStringLiteral("no_frames");
    case ClipCaptureManager::EncodeError::SaveDirectoryInvalid:
        return QStringLiteral("save_directory_invalid");
    case ClipCaptureManager::EncodeError::NoValidFrames:
        return QStringLiteral("no_valid_frames");
    case ClipCaptureManager::EncodeError::FfmpegNotFound:
        return QStringLiteral("ffmpeg_not_found");
    case ClipCaptureManager::EncodeError::FfmpegStartFailed:
        return QStringLiteral("ffmpeg_start_failed");
    case ClipCaptureManager::EncodeError::FfmpegTimeout:
        return QStringLiteral("ffmpeg_timeout");
    case ClipCaptureManager::EncodeError::FfmpegEncodeFailed:
        return QStringLiteral("ffmpeg_encode_failed");
    }
    return QStringLiteral("unknown");
}

QSize evenFrameSize(const QSize &size)
{
    return QSize(std::max(2, size.width() & ~1),
                 std::max(2, size.height() & ~1));
}

QImage normalizeFrameForPipe(const QImage &frame, const QSize &targetSize)
{
    if (!isValidFrameForEncode(frame)) {
        return {};
    }

    QImage normalized = (frame.format() == QImage::Format_ARGB32)
                            ? frame
                            : frame.convertToFormat(QImage::Format_ARGB32);
    if (normalized.isNull()) {
        return {};
    }
    if (normalized.size() != targetSize) {
        normalized = normalized.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    return normalized;
}

struct EncodeAttempt {
    ClipCaptureManager::EncodeResult result;
    bool allowPngFallback = false;
};

EncodeAttempt encodeSnapshotViaPngSequence(const ClipCaptureManager::EncodeSnapshot &snapshot,
                                           const QString &ffmpegExe,
                                           const QString &mp4Path)
{
    EncodeAttempt attempt;
    auto &result = attempt.result;
    result.pathTag = QStringLiteral("png_fallback");

    const auto isCancelled = [&snapshot]() {
        return snapshot.cancelRequested && snapshot.cancelRequested->load();
    };

    const QString tmpDirPath = QDir(QDir::tempPath()).filePath(QString("vms_clip_%1").arg(snapshot.stamp));
    QDir().mkpath(tmpDirPath);

    int frameIndex = 0;
    int validFrameCount = 0;
    for (const auto &frame : snapshot.frames) {
        if (isCancelled()) {
            QDir(tmpDirPath).removeRecursively();
            result.code = ClipCaptureManager::EncodeError::Cancelled;
            result.message = "클립 저장이 취소되었습니다.";
            return attempt;
        }
        if (!isValidFrameForEncode(frame)) {
            continue;
        }
        const QString framePath = QDir(tmpDirPath).filePath(
            QString("frame_%1.png").arg(frameIndex++, 6, 10, QLatin1Char('0')));
        if (!frame.save(framePath, "PNG")) {
            continue;
        }
        ++validFrameCount;
    }

    if (validFrameCount == 0) {
        QDir(tmpDirPath).removeRecursively();
        result.code = ClipCaptureManager::EncodeError::NoValidFrames;
        result.message = "인코딩 가능한 유효 프레임이 없습니다.";
        return attempt;
    }

    QProcess ffmpeg;
    QStringList args;
    args << "-y"
         << "-framerate"
         << "30"
         << "-i"
         << QDir(tmpDirPath).filePath("frame_%06d.png")
         << "-vf"
         << "scale=trunc(iw/2)*2:trunc(ih/2)*2"
         << "-c:v"
         << "libx264"
         << "-preset"
         << "ultrafast"
         << "-crf"
         << "28"
         << "-pix_fmt"
         << "yuv420p"
         << mp4Path;
    ffmpeg.start(ffmpegExe, args);
    if (!ffmpeg.waitForStarted(3000)) {
        QDir(tmpDirPath).removeRecursively();
        result.code = ClipCaptureManager::EncodeError::FfmpegStartFailed;
        result.message = QString("ffmpeg 시작 실패: %1").arg(ffmpeg.errorString());
        return attempt;
    }

    const qint64 startedMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 timeoutMs = 120000;
    while (ffmpeg.state() != QProcess::NotRunning) {
        ffmpeg.waitForFinished(100);
        if (isCancelled()) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(1000);
            QDir(tmpDirPath).removeRecursively();
            QFile::remove(mp4Path);
            result.code = ClipCaptureManager::EncodeError::Cancelled;
            result.message = "클립 저장이 취소되었습니다.";
            return attempt;
        }
        if (QDateTime::currentMSecsSinceEpoch() - startedMs > timeoutMs) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(1000);
            QDir(tmpDirPath).removeRecursively();
            QFile::remove(mp4Path);
            result.code = ClipCaptureManager::EncodeError::FfmpegTimeout;
            result.message = "ffmpeg 인코딩이 시간 초과되었습니다.";
            return attempt;
        }
    }

    const bool ok = (ffmpeg.exitStatus() == QProcess::NormalExit
                     && ffmpeg.exitCode() == 0
                     && QFileInfo::exists(mp4Path));
    QDir(tmpDirPath).removeRecursively();

    if (ok) {
        result.ok = true;
        result.code = ClipCaptureManager::EncodeError::None;
        result.outputPath = mp4Path;
        return attempt;
    }

    QFile::remove(mp4Path);
    result.code = ClipCaptureManager::EncodeError::FfmpegEncodeFailed;
    result.message = QString("ffmpeg 인코딩 실패: %1")
                         .arg(QString::fromUtf8(ffmpeg.readAllStandardError()));
    return attempt;
}

EncodeAttempt encodeSnapshotViaStdinPipe(const ClipCaptureManager::EncodeSnapshot &snapshot,
                                         const QString &ffmpegExe,
                                         const QString &mp4Path)
{
    EncodeAttempt attempt;
    auto &result = attempt.result;
    result.pathTag = QStringLiteral("stdin");

    const auto isCancelled = [&snapshot]() {
        return snapshot.cancelRequested && snapshot.cancelRequested->load();
    };

    QImage firstValidFrame;
    for (const auto &frame : snapshot.frames) {
        if (!isValidFrameForEncode(frame)) {
            continue;
        }
        firstValidFrame = frame;
        break;
    }

    if (firstValidFrame.isNull()) {
        result.code = ClipCaptureManager::EncodeError::NoValidFrames;
        result.message = "인코딩 가능한 유효 프레임이 없습니다.";
        return attempt;
    }

    const QSize targetSize = evenFrameSize(firstValidFrame.size());
    QProcess ffmpeg;
    QStringList args;
    args << "-y"
         << "-f"
         << "rawvideo"
         << "-pix_fmt"
         << "bgra"
         << "-video_size"
         << QString("%1x%2").arg(targetSize.width()).arg(targetSize.height())
         << "-framerate"
         << "30"
         << "-i"
         << "pipe:0"
         << "-c:v"
         << "libx264"
         << "-preset"
         << "ultrafast"
         << "-crf"
         << "28"
         << "-pix_fmt"
         << "yuv420p"
         << mp4Path;
    ffmpeg.start(ffmpegExe, args);
    if (!ffmpeg.waitForStarted(3000)) {
        result.code = ClipCaptureManager::EncodeError::FfmpegStartFailed;
        result.message = QString("ffmpeg 시작 실패: %1").arg(ffmpeg.errorString());
        attempt.allowPngFallback = true;
        return attempt;
    }

    const qint64 startedMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 timeoutMs = 120000;
    const auto timedOut = [startedMs, timeoutMs]() {
        return (QDateTime::currentMSecsSinceEpoch() - startedMs) > timeoutMs;
    };

    bool wroteAnyBytes = false;
    int validFrameCount = 0;
    for (const auto &frame : snapshot.frames) {
        if (isCancelled()) {
            ffmpeg.closeWriteChannel();
            ffmpeg.kill();
            ffmpeg.waitForFinished(1000);
            QFile::remove(mp4Path);
            result.code = ClipCaptureManager::EncodeError::Cancelled;
            result.message = "클립 저장이 취소되었습니다.";
            return attempt;
        }

        QImage normalized = normalizeFrameForPipe(frame, targetSize);
        if (normalized.isNull()) {
            continue;
        }
        ++validFrameCount;

        const char *data = reinterpret_cast<const char *>(normalized.constBits());
        const qint64 totalBytes = static_cast<qint64>(normalized.sizeInBytes());
        qint64 offset = 0;
        while (offset < totalBytes) {
            if (isCancelled()) {
                ffmpeg.closeWriteChannel();
                ffmpeg.kill();
                ffmpeg.waitForFinished(1000);
                QFile::remove(mp4Path);
                result.code = ClipCaptureManager::EncodeError::Cancelled;
                result.message = "클립 저장이 취소되었습니다.";
                return attempt;
            }
            if (timedOut()) {
                ffmpeg.closeWriteChannel();
                ffmpeg.kill();
                ffmpeg.waitForFinished(1000);
                QFile::remove(mp4Path);
                result.code = ClipCaptureManager::EncodeError::FfmpegTimeout;
                result.message = "ffmpeg 인코딩이 시간 초과되었습니다.";
                return attempt;
            }
            if (ffmpeg.state() == QProcess::NotRunning) {
                QFile::remove(mp4Path);
                result.code = ClipCaptureManager::EncodeError::FfmpegEncodeFailed;
                result.message = QString("ffmpeg stdin 쓰기 실패: %1")
                                     .arg(QString::fromUtf8(ffmpeg.readAllStandardError()));
                attempt.allowPngFallback = !wroteAnyBytes;
                return attempt;
            }

            const qint64 written = ffmpeg.write(data + offset, totalBytes - offset);
            if (written > 0) {
                offset += written;
                wroteAnyBytes = true;
                continue;
            }

            if (!ffmpeg.waitForBytesWritten(250)) {
                if (ffmpeg.state() == QProcess::NotRunning) {
                    QFile::remove(mp4Path);
                    result.code = ClipCaptureManager::EncodeError::FfmpegEncodeFailed;
                    result.message = QString("ffmpeg stdin 쓰기 실패: %1")
                                         .arg(QString::fromUtf8(ffmpeg.readAllStandardError()));
                    attempt.allowPngFallback = !wroteAnyBytes;
                    return attempt;
                }
                if (timedOut()) {
                    ffmpeg.closeWriteChannel();
                    ffmpeg.kill();
                    ffmpeg.waitForFinished(1000);
                    QFile::remove(mp4Path);
                    result.code = ClipCaptureManager::EncodeError::FfmpegTimeout;
                    result.message = "ffmpeg 인코딩이 시간 초과되었습니다.";
                    return attempt;
                }
            }
        }
    }

    if (validFrameCount == 0) {
        ffmpeg.closeWriteChannel();
        ffmpeg.kill();
        ffmpeg.waitForFinished(1000);
        QFile::remove(mp4Path);
        result.code = ClipCaptureManager::EncodeError::NoValidFrames;
        result.message = "인코딩 가능한 유효 프레임이 없습니다.";
        return attempt;
    }

    ffmpeg.closeWriteChannel();
    while (ffmpeg.state() != QProcess::NotRunning) {
        ffmpeg.waitForFinished(100);
        if (isCancelled()) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(1000);
            QFile::remove(mp4Path);
            result.code = ClipCaptureManager::EncodeError::Cancelled;
            result.message = "클립 저장이 취소되었습니다.";
            return attempt;
        }
        if (timedOut()) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(1000);
            QFile::remove(mp4Path);
            result.code = ClipCaptureManager::EncodeError::FfmpegTimeout;
            result.message = "ffmpeg 인코딩이 시간 초과되었습니다.";
            return attempt;
        }
    }

    if (ffmpeg.exitStatus() == QProcess::NormalExit
        && ffmpeg.exitCode() == 0
        && QFileInfo::exists(mp4Path)) {
        result.ok = true;
        result.code = ClipCaptureManager::EncodeError::None;
        result.outputPath = mp4Path;
        return attempt;
    }

    QFile::remove(mp4Path);
    result.code = ClipCaptureManager::EncodeError::FfmpegEncodeFailed;
    result.message = QString("ffmpeg 인코딩 실패: %1")
                         .arg(QString::fromUtf8(ffmpeg.readAllStandardError()));
    attempt.allowPngFallback = !wroteAnyBytes;
    return attempt;
}

} // namespace

ClipCaptureManager &ClipCaptureManager::instance()
{
    static ClipCaptureManager manager;
    return manager;
}

ClipCaptureManager::ClipCaptureManager(QObject *parent)
    : QObject(parent)
{
    m_frameTimer = new QTimer(this);
    m_frameTimer->setInterval(33);
    connect(m_frameTimer, &QTimer::timeout, this, &ClipCaptureManager::captureFrame);
}

void ClipCaptureManager::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_state);
    emit recordingChanged(m_state == State::Recording);
}

bool ClipCaptureManager::start(QWidget *sourceWidget)
{
    if (m_state != State::Idle) {
        return false;
    }
    if (!sourceWidget) {
        return false;
    }

    m_sourceWidget = sourceWidget;
    m_sourceChannel.clear();
    m_sourceType = SourceType::WidgetComposite;
    m_capturedFrames.clear();
    m_capturedFrameCount = 0;
    m_startMs = QDateTime::currentMSecsSinceEpoch();
    m_frameTimer->start();
    setState(State::Recording);
    return true;
}

bool ClipCaptureManager::startChannel(const QString &channelName)
{
    if (m_state != State::Idle) {
        return false;
    }
    const QString trimmed = channelName.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    m_sourceWidget.clear();
    m_sourceChannel = trimmed;
    m_sourceType = SourceType::ChannelFrame;
    m_capturedFrames.clear();
    m_capturedFrameCount = 0;
    m_startMs = QDateTime::currentMSecsSinceEpoch();
    m_frameTimer->start();
    setState(State::Recording);
    return true;
}

void ClipCaptureManager::stop()
{
    if (m_state != State::Recording) {
        return;
    }

    m_frameTimer->stop();
    m_sourceWidget.clear();
    m_sourceChannel.clear();
    m_sourceType = SourceType::None;
    m_startMs = 0;
    setState(State::Idle);
}

void ClipCaptureManager::discard()
{
    if (m_state == State::Encoding) {
        if (m_activeEncodeCancelToken) {
            m_activeEncodeCancelToken->store(true);
        }
        setState(State::Idle);
        return;
    }
    if (m_frameTimer && m_frameTimer->isActive()) {
        m_frameTimer->stop();
    }
    m_sourceWidget.clear();
    m_sourceChannel.clear();
    m_sourceType = SourceType::None;
    m_capturedFrames.clear();
    m_capturedFrameCount = 0;
    m_startMs = 0;
    setState(State::Idle);
}

ClipCaptureManager::EncodeResult ClipCaptureManager::prepareEncoding(EncodeSnapshot *snapshot)
{
    EncodeResult result;
    if (!snapshot) {
        result.code = EncodeError::FfmpegEncodeFailed;
        result.message = "인코딩 스냅샷 버퍼를 준비할 수 없습니다.";
        return result;
    }
    snapshot->frames.clear();
    snapshot->outputDirectory.clear();
    snapshot->stamp.clear();
    snapshot->cancelRequested.reset();

    if (m_state == State::Encoding) {
        result.code = EncodeError::AlreadyEncoding;
        result.message = "이미 클립 인코딩이 진행 중입니다.";
        return result;
    }

    if (m_state == State::Recording) {
        m_frameTimer->stop();
        m_startMs = 0;
    }
    setState(State::Encoding);

    if (m_capturedFrames.isEmpty()) {
        result.code = EncodeError::NoFrames;
        result.message = "인코딩할 캡처 프레임이 없습니다.";
        setState(State::Idle);
        return result;
    }

    QString outDir;
    QString saveDirError;
    if (!isClipSaveDirectoryValid(&outDir, &saveDirError)) {
        result.code = EncodeError::SaveDirectoryInvalid;
        result.message = saveDirError;
        setState(State::Idle);
        return result;
    }

    snapshot->frames = std::move(m_capturedFrames);
    m_capturedFrames.clear();
    m_capturedFrameCount = 0;
    m_sourceWidget.clear();
    m_sourceChannel.clear();
    m_sourceType = SourceType::None;
    snapshot->outputDirectory = outDir;
    snapshot->stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    snapshot->cancelRequested = std::make_shared<std::atomic_bool>(false);
    m_activeEncodeCancelToken = snapshot->cancelRequested;
    result.ok = true;
    return result;
}

ClipCaptureManager::EncodeResult ClipCaptureManager::encodeSnapshot(const EncodeSnapshot &snapshot)
{
    QElapsedTimer totalTimer;
    totalTimer.start();

    EncodeResult result;
    int validFrameCount = 0;
    for (const auto &frame : snapshot.frames) {
        if (isValidFrameForEncode(frame)) {
            ++validFrameCount;
        }
    }
    const auto logResult = [&](const EncodeResult &loggedResult, int frameCount) {
        qInfo().noquote()
            << QString("perf metric=clip_total_ms context=path:%1,frames:%2,ok:%3,code:%4 value=%5 unit=ms")
                  .arg(loggedResult.pathTag.isEmpty() ? QStringLiteral("unknown") : loggedResult.pathTag)
                  .arg(frameCount)
                  .arg(loggedResult.ok ? QStringLiteral("true") : QStringLiteral("false"))
                  .arg(encodeErrorName(loggedResult.code))
                  .arg(totalTimer.elapsed());
    };
    if (snapshot.frames.isEmpty()) {
        result.code = EncodeError::NoFrames;
        result.pathTag = QStringLiteral("n_a");
        logResult(result, 0);
        result.message = "인코딩할 캡처 프레임이 없습니다.";
        return result;
    }
    const auto isCancelled = [&snapshot]() {
        return snapshot.cancelRequested && snapshot.cancelRequested->load();
    };
    if (isCancelled()) {
        result.code = EncodeError::Cancelled;
        result.pathTag = QStringLiteral("n_a");
        logResult(result, validFrameCount);
        result.message = "클립 저장이 취소되었습니다.";
        return result;
    }

    const QString ffmpegExe = resolveFfmpegExecutable();
    if (ffmpegExe.isEmpty()) {
        result.code = EncodeError::FfmpegNotFound;
        result.pathTag = QStringLiteral("n_a");
        logResult(result, validFrameCount);
        result.message = "ffmpeg를 찾을 수 없습니다. ffmpeg 설치/경로를 확인하세요.";
        return result;
    }
    const QString mp4Path = QDir(snapshot.outputDirectory).filePath(QString("vms_clip_%1.mp4").arg(snapshot.stamp));

    EncodeAttempt attempt = encodeSnapshotViaStdinPipe(snapshot, ffmpegExe, mp4Path);
    if (attempt.result.ok || !attempt.allowPngFallback) {
        logResult(attempt.result, validFrameCount);
        return attempt.result;
    }

    attempt = encodeSnapshotViaPngSequence(snapshot, ffmpegExe, mp4Path);
    logResult(attempt.result, validFrameCount);
    return attempt.result;
}

void ClipCaptureManager::finishEncoding(const std::shared_ptr<std::atomic_bool> &cancelToken)
{
    if (cancelToken && m_activeEncodeCancelToken != cancelToken) {
        return;
    }
    m_activeEncodeCancelToken.reset();
    if (m_state == State::Encoding) {
        setState(State::Idle);
    }
}

ClipCaptureManager::EncodeResult ClipCaptureManager::stopAndEncode()
{
    EncodeSnapshot snapshot;
    const EncodeResult prepared = prepareEncoding(&snapshot);
    if (!prepared.ok) {
        return prepared;
    }
    EncodeResult result = encodeSnapshot(snapshot);
    finishEncoding(snapshot.cancelRequested);
    return result;
}

bool ClipCaptureManager::stopAndEncode(QString *outputPath, QString *errorMessage)
{
    if (outputPath) {
        outputPath->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    const EncodeResult result = stopAndEncode();
    if (result.ok) {
        if (outputPath) {
            *outputPath = result.outputPath;
        }
        return true;
    }
    if (errorMessage) {
        *errorMessage = result.message;
    }
    return false;
}

void ClipCaptureManager::setSourceWidget(QWidget *sourceWidget)
{
    if (m_state != State::Recording) {
        return;
    }
    m_sourceWidget = sourceWidget;
    if (!m_sourceWidget.isNull()) {
        m_sourceType = SourceType::WidgetComposite;
        m_sourceChannel.clear();
    }
}

void ClipCaptureManager::setSourceChannel(const QString &channelName)
{
    if (m_state != State::Recording) {
        return;
    }
    const QString trimmed = channelName.trimmed();
    m_sourceChannel = trimmed;
    if (!m_sourceChannel.isEmpty()) {
        m_sourceType = SourceType::ChannelFrame;
        m_sourceWidget.clear();
    }
}

bool ClipCaptureManager::isRecording() const
{
    return m_state == State::Recording;
}

bool ClipCaptureManager::isEncoding() const
{
    return m_state == State::Encoding;
}

ClipCaptureManager::State ClipCaptureManager::state() const
{
    return m_state;
}

qint64 ClipCaptureManager::elapsedSeconds() const
{
    if (!isRecording() || m_startMs <= 0) {
        return 0;
    }
    return (QDateTime::currentMSecsSinceEpoch() - m_startMs) / 1000;
}

int ClipCaptureManager::capturedFrameCount() const
{
    return m_capturedFrameCount;
}

void ClipCaptureManager::captureFrame()
{
    if (m_state != State::Recording) {
        return;
    }

    QImage frame;
    switch (m_sourceType) {
    case SourceType::WidgetComposite: {
        if (m_sourceWidget.isNull()) {
            return;
        }
        const QPixmap pix = m_sourceWidget->grab();
        if (pix.isNull()) {
            return;
        }
        frame = pix.toImage();
        break;
    }
    case SourceType::ChannelFrame: {
        frame = ChannelSessionManager::instance().latestFrameForChannelCopy(m_sourceChannel);
        if (frame.isNull()) {
            return;
        }
        break;
    }
    case SourceType::None:
    default:
        return;
    }

    if (!isValidFrameForEncode(frame)) {
        return;
    }
    m_capturedFrames.push_back(frame);
    ++m_capturedFrameCount;

    const int kMaxBufferedFrames = 300;
    while (m_capturedFrames.size() > kMaxBufferedFrames) {
        m_capturedFrames.pop_front();
    }
}
