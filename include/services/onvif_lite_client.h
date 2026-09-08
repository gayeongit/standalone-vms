#ifndef ONVIF_LITE_CLIENT_H
#define ONVIF_LITE_CLIENT_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class QNetworkAccessManager;

class OnvifLiteClient : public QObject
{
    Q_OBJECT
public:
    struct DiscoveredDevice
    {
        QString uuid;
        QString xaddr;
        QString ip;
    };

    struct StreamProfile
    {
        QString token; // ONVIF profile token == 채널 identity
        QString name;
        QString videoCodec;
        int width = 0;
        int height = 0;
        QString rtsp;
    };

    struct DeviceProfilesResult
    {
        bool ok = false;
        QString errorMessage;
        QString uuid;
        QString xaddr;      // Device Service (WS-Discovery로 얻은 원래 주소)
        QString mediaXAddr;   // GetCapabilities로 얻은 Media Service 주소 (GetProfiles/GetStreamUri 대상)
        QString ptzXAddr;     // GetCapabilities로 얻은 PTZ Service 주소
        QString imagingXAddr; // GetCapabilities로 얻은 Imaging Service 주소
        QString ip;
        QString name;
        QString model;
        QVector<StreamProfile> profiles;
    };

    explicit OnvifLiteClient(QObject *parent = nullptr);

    void setDiscoveryTimeoutMs(int ms);
    void setDeviceServiceTimeoutMs(int ms);
    void setManualXAddr(const QString &xaddr);

    // WS-Discovery Probe. 네트워크 전체를 대상으로 한 번만 스캔하고,
    // invalidate() 전까지는 캐시된 결과를 그대로 콜백한다.
    void discover(QObject *context, std::function<void(const QVector<DiscoveredDevice> &)> callback);

    // GetDeviceInformation + GetProfiles + 프로필별 GetStreamUri를 한 시퀀스로 처리한다.
    // uuid별로 메모이즈되어 있어 같은 uuid로 다시 호출하면 네트워크 요청 없이 캐시를 반환한다.
    void fetchDeviceProfiles(
        const QString &uuid,
        const QString &xaddr,
        QObject *context,
        std::function<void(const DeviceProfilesResult &)> callback);

    void invalidate();

    // PTZ RelativeMove — zoomDelta는 정규화된 상대 이동량(-1.0~1.0 권장). Pan/Tilt는 안 씀(zoom만 조작).
    void relativeMove(
        const QString &xaddr,
        const QString &profileToken,
        double zoomDelta,
        QObject *context,
        std::function<void(bool ok, const QString &errorMessage)> callback);

    // Imaging Move(Relative) — focusDelta도 정규화된 상대 이동량.
    void imagingRelativeMove(
        const QString &xaddr,
        const QString &videoSourceToken,
        double focusDelta,
        QObject *context,
        std::function<void(bool ok, const QString &errorMessage)> callback);

private:
    void postSoap(
        const QString &xaddr,
        const QByteArray &bodyXml,
        QObject *context,
        std::function<void(bool ok, const QByteArray &responseBody, const QString &errorMessage)> callback);
    void fetchStreamUrisSequentially(
        DeviceProfilesResult result,
        int index,
        QObject *context,
        std::function<void(const DeviceProfilesResult &)> callback);

    QNetworkAccessManager *m_nam = nullptr;
    int m_discoveryTimeoutMs = 1500;
    int m_deviceServiceTimeoutMs = 4000;
    QString m_manualXAddr;
    bool m_discoveryDone = false;
    QVector<DiscoveredDevice> m_discoveredDevices;
    QHash<QString, DeviceProfilesResult> m_profileCache;
};

#endif // ONVIF_LITE_CLIENT_H
