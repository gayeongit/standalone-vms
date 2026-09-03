#ifndef CAPTURE_STORAGE_HELPERS_H
#define CAPTURE_STORAGE_HELPERS_H

#include "clip_capture_manager.h"

#include <QString>

#include <functional>

class QLabel;
class QWidget;
class StreamPlayer;

QString snapshotSaveDirectory();
QString clipSaveDirectory();
bool isCaptureSaveDirectoryValid(QString *resolvedPath = nullptr, QString *errorMessage = nullptr);
bool isClipSaveDirectoryValid(QString *resolvedPath = nullptr, QString *errorMessage = nullptr);
QString formatClipDuration(qint64 elapsedSeconds);
QString clipButtonText();
bool handleClipEncodeFailure(QWidget *parent,
                             QLabel *statusLabel,
                             const QString &title,
                             const ClipCaptureManager::EncodeResult &encode,
                             const std::function<void()> &openSettings = {});
bool saveSnapshotPng(QWidget *sourceWidget, const QString &prefix, QString *savedPath, QString *errorMessage);
bool saveSnapshotPngFromChannel(const QString &channelName, const QString &prefix, QString *savedPath, QString *errorMessage);
bool saveSnapshotPngFromPlayer(StreamPlayer *player, const QString &prefix, QString *savedPath, QString *errorMessage);

#endif // CAPTURE_STORAGE_HELPERS_H
