#ifndef PLAYBACK_SCREEN_H
#define PLAYBACK_SCREEN_H

#include "playback_service.h"

#include <QString>
#include <QVector>
#include <QWidget>

class QTimer;
class QShowEvent;
class QResizeEvent;
class StreamPlayer;
class QTreeWidget;
class QLabel;
class QPushButton;
class QSlider;
class QFrame;
class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

class PlaybackScreen : public QWidget
{
    Q_OBJECT
public:
    explicit PlaybackScreen(QWidget *parent = nullptr);
    void setPlaybackService(PlaybackService *service);
    bool isExportBusy() const;
    void cancelExportOperations(bool notifyUser = false);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

signals:
    void backToMainRequested();
    void openCctvRequested();
    void openUgvRequested();
    void settingsRequested();
    void logoutRequested();

private:
    void startPlaybackForChannel(int channelId, const QString &channelName, const QString &date);
    void loadTimelineForChannel(int channelId, const QString &channelName, const QString &date, bool autoStart = false);
    void applyTimelineResult(const PlaybackTimelineResult &result, const QString &channelName, bool autoStart);
    void requestPlaybackStream(int channelId, const QString &channelName, const QString &timestamp, bool autoStart = true);
    void applyPlaybackCapabilities();
    void refreshTimelineUi();
    void rebuildEventMarkers();
    void updateTimelineHandle();
    void showPlaybackSnackbar(const QString &message);
    void clearPlaybackSnackbar();
    void placePlaybackSnackbar();
    void openExportDialog();
    void pollExportStatus();
    void startExportDownload(const QString &url, const QString &fileName);
    void updateExportUiState();

    StreamPlayer *m_player = nullptr;
    QWidget *m_videoHost = nullptr;
    QTreeWidget *m_playbackTree = nullptr;
    QLabel *m_playbackFileLabel = nullptr;
    QLabel *m_actionStatusLabel = nullptr;
    QPushButton *m_playPauseButton = nullptr;
    QPushButton *m_speedButton = nullptr;
    QSlider *m_timelineSlider = nullptr;
    QVector<QWidget *> m_markerWidgets;
    QFrame *m_timelineHandle = nullptr;
    QFrame *m_timelineHandleCore = nullptr;
    QLabel *m_currentTimeLabel = nullptr;
    QLabel *m_totalTimeLabel = nullptr;
    QTimer *m_timelineTimer = nullptr;
    QTimer *m_snackbarHideTimer = nullptr;
    QFrame *m_playbackSnackbarFrame = nullptr;
    QLabel *m_playbackSnackbarLabel = nullptr;
    int m_pendingSnackbarEvents = 0;
    int m_speedIndex = 1;
    qint64 m_timelineSpanStartMs = 0;
    qint64 m_timelineSpanEndMs = 24LL * 60LL * 60LL * 1000LL;
    QString m_currentPlaybackSource;
    QString m_selectedPlaybackDate;
    QString m_currentPlaybackChannelName;
    int m_currentPlaybackChannelId = -1;
    QVector<PlaybackTimeRange> m_availableRanges;
    QVector<PlaybackMarker> m_timelineMarkers;
    PlaybackService *m_playbackService = nullptr;
    int m_timelineLoadGeneration = 0;
    bool m_streamRequestInFlight = false;
    bool m_rateSupported = false;
    qint64 m_selectedTimelinePositionMs = -1;
    qint64 m_playbackStartTimelinePositionMs = -1;
    qint64 m_currentTimelinePositionMs = -1;
    qint64 m_playbackStartWallclockMs = -1;
    QPushButton *m_exportButton = nullptr;
    QTimer *m_exportPollTimer = nullptr;
    QString m_exportJobId;
    int m_exportPollAttempts = 0;
    int m_exportGeneration = 0;
    bool m_exportStatusInFlight = false;
    QNetworkAccessManager *m_exportDownloadManager = nullptr;
    QNetworkReply *m_exportDownloadReply = nullptr;
    QSaveFile *m_exportSaveFile = nullptr;
    bool m_exportDownloadAbortRequested = false;
    bool m_timelineServiceDisconnectPopupShown = false;
};

#endif // PLAYBACK_SCREEN_H
