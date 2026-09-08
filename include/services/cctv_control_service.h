#ifndef CCTV_CONTROL_SERVICE_H
#define CCTV_CONTROL_SERVICE_H

#include <QObject>
#include <QString>

#include <functional>

class OnvifLiteClient;

struct CctvControlResult
{
    bool ok = false;
    int channelId = -1;
    int value = 0;
    QString errorMessage;
};

class CctvControlService : public QObject
{
    Q_OBJECT
public:
    explicit CctvControlService(OnvifLiteClient *onvifClient, QObject *parent = nullptr);

    void zoomStep(int channelId, int value, QObject *context, std::function<void(const CctvControlResult &)> callback = {});
    void focusStep(int channelId, int value, QObject *context, std::function<void(const CctvControlResult &)> callback = {});

    static bool isSupportedStepValue(int value);

private:
    enum class ControlKind { Zoom, Focus };
    void requestControl(
        ControlKind kind,
        int channelId,
        int value,
        QObject *context,
        std::function<void(const CctvControlResult &)> callback);

    OnvifLiteClient *m_onvifClient = nullptr;
};

#endif // CCTV_CONTROL_SERVICE_H
