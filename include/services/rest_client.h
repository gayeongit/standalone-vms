#ifndef REST_CLIENT_H
#define REST_CLIENT_H

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

struct RestResponse
{
    bool ok = false;
    int httpStatus = 0;
    QJsonObject json;
    QString errorMessage;
    QString requestTag;
};

class RestClient : public QObject
{
    Q_OBJECT
public:
    explicit RestClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &baseUrl);
    void setAccessToken(const QString &accessToken);
    void clearAccessToken();
    void setRequestTimeoutMs(int timeoutMs);

    bool isConfigured() const;
    QString baseUrl() const;

    void getJson(
        const QString &path,
        const QString &requestTag,
        bool suppressUnauthorized,
        QObject *context,
        std::function<void(const RestResponse &)> callback);

    void postJson(
        const QString &path,
        const QJsonObject &body,
        const QString &requestTag,
        bool suppressUnauthorized,
        QObject *context,
        std::function<void(const RestResponse &)> callback);

signals:
    void unauthorizedDetected(const QString &requestTag);

private:
    void requestJson(
        const QByteArray &method,
        const QString &path,
        const QJsonObject *body,
        const QString &requestTag,
        bool suppressUnauthorized,
        QObject *context,
        std::function<void(const RestResponse &)> callback);

    RestResponse buildNetworkErrorResponse(const QString &message) const;
    QString resolveUrl(const QString &path) const;
    void finalizeReply(
        QNetworkReply *reply,
        const QString &requestTag,
        bool suppressUnauthorized,
        QObject *context,
        std::function<void(const RestResponse &)> callback);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_baseUrl;
    QString m_accessToken;
    int m_timeoutMs = 8000;
};

#endif // REST_CLIENT_H
