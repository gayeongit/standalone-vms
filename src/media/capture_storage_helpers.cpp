#include "capture_storage_helpers.h"

#include "channel_session_manager.h"
#include "feedback_ui_helpers.h"
#include "popup_manager.h"
#include "stream_player.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QWidget>

namespace {
QString resolveLegacyAwareDefaultDir(const QString &newKey, const QString &defaultDir)
{
    // v2: capture paths are stored in VMS_v2 and lazily migrated from VMS_v1.
    QSettings v2("TeamClue", "VMS_v2");
    const QString directV2 = v2.value(newKey).toString().trimmed();
    if (!directV2.isEmpty()) {
        return directV2;
    }

    QSettings v1("TeamClue", "VMS_v1");
    QString migrated = v1.value(newKey).toString().trimmed();
    if (migrated.isEmpty()) {
        const QString legacySaveDir = v1.value("paths/saveDir").toString().trimmed();
        const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation).trimmed();
        if (!legacySaveDir.isEmpty() && legacySaveDir != desktop) {
            if (newKey == "paths/snapshotDir") {
                migrated = QDir(legacySaveDir).filePath("snapshot");
            } else if (newKey == "paths/clipDir") {
                migrated = QDir(legacySaveDir).filePath("videoclip");
            } else {
                migrated = legacySaveDir;
            }
        }
    }

    if (migrated.isEmpty()) {
        migrated = defaultDir;
    }
    v2.setValue(newKey, migrated);
    return migrated;
}

bool ensureWritableDirectory(const QString &dirPath, QString *resolvedPath, QString *errorMessage)
{
    if (resolvedPath) {
        resolvedPath->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    if (dirPath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = "저장 경로가 비어 있습니다. 설정에서 경로를 다시 지정해 주세요.";
        }
        return false;
    }

    QDir target(dirPath);
    if (!target.exists() && !QDir().mkpath(dirPath)) {
        if (errorMessage) {
            *errorMessage = "저장 경로 생성에 실패했습니다. 설정에서 경로를 다시 지정해 주세요.";
        }
        return false;
    }

    const QFileInfo info(QDir(dirPath).absolutePath());
    if (!info.isWritable()) {
        if (errorMessage) {
            *errorMessage = "저장 경로에 쓰기 권한이 없습니다. 설정에서 경로를 다시 지정해 주세요.";
        }
        return false;
    }
    if (resolvedPath) {
        *resolvedPath = QDir(dirPath).absolutePath();
    }
    return true;
}
} // namespace

QString snapshotSaveDirectory()
{
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation).trimmed();
    const QString fallbackBase = pictures.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        : pictures;
    const QString defaultDir = QDir(fallbackBase).filePath("snapshot");
    QString dir = resolveLegacyAwareDefaultDir("paths/snapshotDir", defaultDir).trimmed();
    if (dir.isEmpty()) {
        dir = defaultDir;
    }
    return dir;
}

QString clipSaveDirectory()
{
    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation).trimmed();
    const QString fallbackBase = videos.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        : videos;
    const QString defaultDir = QDir(fallbackBase).filePath("videoclip");
    QString dir = resolveLegacyAwareDefaultDir("paths/clipDir", defaultDir).trimmed();
    if (dir.isEmpty()) {
        dir = defaultDir;
    }
    return dir;
}

bool isCaptureSaveDirectoryValid(QString *resolvedPath, QString *errorMessage)
{
    return ensureWritableDirectory(snapshotSaveDirectory(), resolvedPath, errorMessage);
}

bool isClipSaveDirectoryValid(QString *resolvedPath, QString *errorMessage)
{
    return ensureWritableDirectory(clipSaveDirectory(), resolvedPath, errorMessage);
}

QString formatClipDuration(qint64 elapsedSeconds)
{
    if (elapsedSeconds < 0) {
        elapsedSeconds = 0;
    }
    const qint64 hh = elapsedSeconds / 3600;
    const qint64 mm = (elapsedSeconds % 3600) / 60;
    const qint64 ss = elapsedSeconds % 60;
    return QString("%1:%2:%3")
        .arg(hh, 2, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0'));
}

QString clipButtonText()
{
    const auto &clipMgr = ClipCaptureManager::instance();
    if (clipMgr.isRecording()) {
        return QStringLiteral("● %1").arg(formatClipDuration(clipMgr.elapsedSeconds()));
    }
    if (clipMgr.isEncoding()) {
        return QStringLiteral("저장중");
    }
    return QStringLiteral("● 클립");
}

bool handleClipEncodeFailure(QWidget *parent,
                             QLabel *statusLabel,
                             const QString &title,
                             const ClipCaptureManager::EncodeResult &encode,
                             const std::function<void()> &openSettings)
{
    if (encode.code == ClipCaptureManager::EncodeError::Cancelled) {
        showActionStatus(statusLabel, "클립 저장이 취소되었습니다.", "info", 1500);
        return true;
    }

    clearActionStatus(statusLabel);
    const QString merged = QString("클립 저장 실패: %1").arg(encode.message);
    if (encode.code == ClipCaptureManager::EncodeError::SaveDirectoryInvalid) {
        const bool open = PopupManager::confirmWithLabels(
            parent,
            title,
            merged,
            "설정으로 이동",
            "닫기");
        if (open && openSettings) {
            openSettings();
        }
        return true;
    }
    showActionStatus(statusLabel, merged, "error", 2500);
    return true;
}

bool saveSnapshotPng(QWidget *sourceWidget, const QString &prefix, QString *savedPath, QString *errorMessage)
{
    if (savedPath) {
        savedPath->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    if (!sourceWidget) {
        if (errorMessage) {
            *errorMessage = "캡처 소스가 없습니다.";
        }
        return false;
    }
    const QPixmap pix = sourceWidget->grab();
    if (pix.isNull()) {
        if (errorMessage) {
            *errorMessage = "현재 화면 캡처에 실패했습니다.";
        }
        return false;
    }
    const QImage frame = pix.toImage();
    QString dir;
    if (!isCaptureSaveDirectoryValid(&dir, errorMessage)) {
        return false;
    }
    const QString fileName = QString("%1_%2.png")
                                 .arg(prefix, QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString outPath = QDir(dir).filePath(fileName);
    if (!frame.save(outPath, "PNG")) {
        if (errorMessage) {
            *errorMessage = "PNG 파일 저장에 실패했습니다.";
        }
        return false;
    }
    if (savedPath) {
        *savedPath = outPath;
    }
    return true;
}

bool saveSnapshotPngFromChannel(const QString &channelName, const QString &prefix, QString *savedPath, QString *errorMessage)
{
    if (savedPath) {
        savedPath->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    const QString channel = channelName.trimmed();
    if (channel.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "캡처 채널명이 없습니다.";
        }
        return false;
    }

    const QImage frame = ChannelSessionManager::instance().latestFrameForChannelCopy(channel);
    if (frame.isNull()) {
        if (errorMessage) {
            *errorMessage = "아직 저장 가능한 영상 프레임이 없습니다.";
        }
        return false;
    }

    QString dir;
    if (!isCaptureSaveDirectoryValid(&dir, errorMessage)) {
        return false;
    }

    const QString fileName = QString("%1_%2.png")
                                 .arg(prefix, QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString outPath = QDir(dir).filePath(fileName);
    if (!frame.save(outPath, "PNG")) {
        if (errorMessage) {
            *errorMessage = "PNG 파일 저장에 실패했습니다.";
        }
        return false;
    }
    if (savedPath) {
        *savedPath = outPath;
    }
    return true;
}

bool saveSnapshotPngFromPlayer(StreamPlayer *player, const QString &prefix, QString *savedPath, QString *errorMessage)
{
    if (savedPath) {
        savedPath->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    if (!player) {
        if (errorMessage) {
            *errorMessage = "캡처 플레이어가 없습니다.";
        }
        return false;
    }

    const QImage frame = player->latestFrameCopy();
    if (frame.isNull()) {
        if (errorMessage) {
            *errorMessage = "아직 저장 가능한 영상 프레임이 없습니다.";
        }
        return false;
    }

    QString dir;
    if (!isCaptureSaveDirectoryValid(&dir, errorMessage)) {
        return false;
    }

    const QString fileName = QString("%1_%2.png")
                                 .arg(prefix, QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString outPath = QDir(dir).filePath(fileName);
    if (!frame.save(outPath, "PNG")) {
        if (errorMessage) {
            *errorMessage = "PNG 파일 저장에 실패했습니다.";
        }
        return false;
    }
    if (savedPath) {
        *savedPath = outPath;
    }
    return true;
}
