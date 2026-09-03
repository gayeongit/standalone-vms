#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

class RestClient;
struct RestResponse;

struct AuthResult
{
    bool ok = false;
    QString accessToken;
    QString userId;
    QString errorMessage;
    int httpStatus = 0;
    QJsonObject payload;
};

class AuthService : public QObject
{
    Q_OBJECT
public:
    explicit AuthService(RestClient *restClient, QObject *parent = nullptr);

    void setLoginPath(const QString &path);
    void setLogoutPath(const QString &path);
    void setSignupPath(const QString &path);

    void login(
        const QString &username,
        const QString &password,
        QObject *context,
        std::function<void(const AuthResult &)> callback);

    void signup(
        const QString &name,
        const QString &username,
        const QString &password,
        QObject *context,
        std::function<void(const AuthResult &)> callback);

    void logout(
        QObject *context,
        std::function<void(const AuthResult &)> callback = {});

private:
    QString extractAccessToken(const QJsonObject &obj) const;
    QString extractUserId(const QJsonObject &obj) const;
    QString extractErrorMessage(const RestResponse &resp, const QString &fallback) const;

    RestClient *m_restClient = nullptr;
    QString m_loginPath = "/auth/login";
    QString m_logoutPath = "/auth/logout";
    QString m_signupPath = "/auth/signup";
};

#endif // AUTH_SERVICE_H
