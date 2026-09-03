#ifndef UGV_SCREEN_H
#define UGV_SCREEN_H

#include "ugv_service.h"

#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QTimer;
class QShowEvent;
class QHideEvent;
class QResizeEvent;
class QKeyEvent;
class QGraphicsView;
class QGraphicsScene;
class QGraphicsEllipseItem;
class QGraphicsPathItem;
class QGraphicsLineItem;
class QGraphicsPolygonItem;
class QFrame;
class QPushButton;
class QToolButton;
class QSpinBox;
class QTreeWidget;
class QSlider;

class UgvScreen : public QWidget
{
    Q_OBJECT
public:
    explicit UgvScreen(QWidget *parent = nullptr);
    void setUgvService(UgvService *service);
    void setMapBounds(double minLat, double maxLat, double minLon, double maxLon);
    void prepareForShutdown();
    bool confirmNavigationAwayFromActiveMission();

signals:
    void backToMainRequested();
    void openCctvRequested();
    void missionEndRequested();
    void openPlaybackRequested();
    void settingsRequested();
    void logoutRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct UgvTarget
    {
        int gatewayId = -1;
        int ugvId = -1;
        QString displayName;
    };

    void refreshStream();
    void refreshOsd();
    void updateSessionUi();
    void refreshSidebarStatus();
    void presentServiceError(const QString &message);
    bool resolveCurrentUgvTarget(UgvTarget *target) const;
    QPointF mapPointForGps(double latitude, double longitude) const;
    void updateMapTelemetry(const UgvGpsTelemetry &telemetry);
    void sendDriveCommand(int forward, int back, int left, int right, int timeoutMs = 100);
    void sendPtzCommand(double pan, double tilt, double zoom = 0.0, int timeoutMs = 100);
    void startDriveHold(int forward, int back, int left, int right);
    void stopDriveHold();
    void startPtzHold(double pan, double tilt);
    void stopPtzHold();
    void scheduleTelemetryUiRefresh();
    void applyTelemetryUiRefresh();
    void resetTelemetryAndMapState(bool resetPtzToCenter);
    void placeInfoPanel();
    bool handleDriveKeyPress(int key, bool isAutoRepeat);
    bool handleDriveKeyRelease(int key, bool isAutoRepeat);
    void dispatchDriveForArrowKey(int key);
    bool handlePtzKeyPress(int key, bool isAutoRepeat);
    bool handlePtzKeyRelease(int key, bool isAutoRepeat);
    void dispatchPtzForWasdKey(int key);
    void showSnackbar(const QString &message);
    void clearSnackbar();
    void placeSnackbar();

    QWidget *m_ugvVideoViewport = nullptr;
    QTreeWidget *m_ugvTree = nullptr;
    QFrame *m_ugvInfoPanel = nullptr;
    QFrame *m_ugvSnackbarFrame = nullptr;
    QLabel *m_ugvSnackbarLabel = nullptr;
    QGraphicsView *m_mapView = nullptr;
    QGraphicsScene *m_mapScene = nullptr;
    QGraphicsPathItem *m_mapRoute = nullptr;
    QGraphicsEllipseItem *m_mapPulse = nullptr;
    QGraphicsEllipseItem *m_mapMarker = nullptr;
    QGraphicsLineItem *m_mapHeadingLine = nullptr;
    QGraphicsPolygonItem *m_mapHeadingChevron = nullptr;
    QLabel *m_actionStatusLabel = nullptr;
    QLabel *m_ugvStatusConnectionValue = nullptr;
    QLabel *m_ugvStatusRssiValue = nullptr;
    QLabel *m_ugvStatusTimestampValue = nullptr;
    QLabel *m_ugvStatusTargetValue = nullptr;
    QLabel *m_ugvStatusIdsValue = nullptr;
    QLabel *m_ugvStatusFeedbackValue = nullptr;
    QLabel *m_panCurrentValueLabel = nullptr;
    QLabel *m_tiltCurrentValueLabel = nullptr;
    QPushButton *m_sessionButton = nullptr;
    QToolButton *m_driveUpButton = nullptr;
    QToolButton *m_driveLeftButton = nullptr;
    QToolButton *m_driveStopButton = nullptr;
    QToolButton *m_driveRightButton = nullptr;
    QToolButton *m_driveDownButton = nullptr;
    QToolButton *m_dpadUpButton = nullptr;
    QToolButton *m_dpadLeftButton = nullptr;
    QToolButton *m_dpadCenterButton = nullptr;
    QToolButton *m_dpadRightButton = nullptr;
    QToolButton *m_dpadDownButton = nullptr;
    QSpinBox *m_panSpin = nullptr;
    QSpinBox *m_tiltSpin = nullptr;
    QSlider *m_driveSpeedSlider = nullptr;
    QTimer *m_osdTimer = nullptr;
    QTimer *m_driveRepeatTimer = nullptr;
    QTimer *m_ptzRepeatTimer = nullptr;
    QTimer *m_snackbarHideTimer = nullptr;
    QTimer *m_telemetryRefreshTimer = nullptr;
    QPushButton *m_clipButton = nullptr;
    QString m_boundChannel;
    UgvService *m_ugvService = nullptr;
    double m_mapMinLat = 33.0;
    double m_mapMaxLat = 39.0;
    double m_mapMinLon = 124.0;
    double m_mapMaxLon = 132.0;
    bool m_hasGpsTelemetry = false;
    UgvGpsTelemetry m_lastGpsTelemetry;
    bool m_hasRssiTelemetry = false;
    UgvRssiTelemetry m_lastRssiTelemetry;
    QVector<QPointF> m_routePoints;
    bool m_hasLastMapPoint = false;
    QPointF m_lastMapPoint;
    bool m_hasEstimatedHeading = false;
    double m_estimatedHeadingDeg = 0.0;
    QVector<int> m_pressedDriveKeys;
    QVector<int> m_pressedPtzKeys;
    int m_activeDriveForward = 0;
    int m_activeDriveBack = 0;
    int m_activeDriveLeft = 0;
    int m_activeDriveRight = 0;
    int m_driveSpeedLevel = 2;
    double m_activePanCommand = 0.0;
    double m_activeTiltCommand = 0.0;
    bool m_skipDisconnectOnHide = false;
};

#endif // UGV_SCREEN_H
