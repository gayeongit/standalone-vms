#ifndef CCTV_CONTROL_SERVICE_H
#define CCTV_CONTROL_SERVICE_H

#include <QObject>
#include <QString>

#include <functional>

class RestClient;
struct RestResponse;

struct CctvControlResult
{
    bool ok = false;
    int httpStatus = 0;
    int channelId = -1;
    int value = 0;
    QString result;
    QString errorCode;
    QString errorMessage;
};

class CctvControlService : public QObject
{
    Q_OBJECT
public:
    explicit CctvControlService(RestClient *restClient, QObject *parent = nullptr);

    void setZoomPathTemplate(const QString &pathTemplate);
    void setFocusPathTemplate(const QString &pathTemplate);

    void zoomStep(int channelId, int value, QObject *context, std::function<void(const CctvControlResult &)> callback = {});
    void focusStep(int channelId, int value, QObject *context, std::function<void(const CctvControlResult &)> callback = {});

    static bool isSupportedStepValue(int value);

private:
    void requestControl(
        const QString &pathTemplate,
        const QString &requestTag,
        int channelId,
        int value,
        QObject *context,
        std::function<void(const CctvControlResult &)> callback);
    QString resolvePathTemplate(const QString &pathTemplate, int channelId) const;
    QString extractErrorCode(const RestResponse &response) const;
    QString extractErrorMessage(const RestResponse &response, const QString &fallback) const;

    RestClient *m_restClient = nullptr;
    QString m_zoomPathTemplate = "/channel/{channelId}/zoom";
    QString m_focusPathTemplate = "/channel/{channelId}/focus";
};

#endif // CCTV_CONTROL_SERVICE_H
