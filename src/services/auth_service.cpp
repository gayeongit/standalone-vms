#include "auth_service.h"

#include "rest_client.h"

#include <QJsonArray>
#include <QJsonValue>

namespace {
QString jsonValueToString(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString().trimmed();
    }
    if (value.isDouble()) {
        const double n = value.toDouble();
        if (qFuzzyCompare(n + 1.0, static_cast<double>(static_cast<qint64>(n)) + 1.0)) {
            return QString::number(static_cast<qint64>(n));
        }
        return QString::number(n, 'g', 15);
    }
    return {};
}

QString firstErrorFromErrorsNode(const QJsonValue &errorsValue)
{
    if (errorsValue.isString()) {
        return errorsValue.toString().trimmed();
    }
    if (errorsValue.isArray()) {
        const QJsonArray arr = errorsValue.toArray();
        for (const QJsonValue &item : arr) {
            const QString message = jsonValueToString(item);
            if (!message.isEmpty()) {
                return message;
            }
            if (item.isObject()) {
                const QJsonObject obj = item.toObject();
                const QString nestedMessage = obj.value("message").toString().trimmed();
                if (!nestedMessage.isEmpty()) {
                    return nestedMessage;
                }
            }
        }
    }
    if (errorsValue.isObject()) {
        const QJsonObject errorsObj = errorsValue.toObject();
        for (auto it = errorsObj.begin(); it != errorsObj.end(); ++it) {
            const QString item = firstErrorFromErrorsNode(it.value());
            if (!item.isEmpty()) {
                return item;
            }
        }
    }
    return {};
}
} // namespace

AuthService::AuthService(RestClient *restClient, QObject *parent)
    : QObject(parent)
    , m_restClient(restClient)
{}

void AuthService::setLoginPath(const QString &path)
{
    const QString p = path.trimmed();
    if (!p.isEmpty()) {
        m_loginPath = p;
    }
}

void AuthService::setLogoutPath(const QString &path)
{
    const QString p = path.trimmed();
    if (!p.isEmpty()) {
        m_logoutPath = p;
    }
}

void AuthService::setSignupPath(const QString &path)
{
    const QString p = path.trimmed();
    if (!p.isEmpty()) {
        m_signupPath = p;
    }
}

void AuthService::login(
    const QString &username,
    const QString &password,
    QObject *context,
    std::function<void(const AuthResult &)> callback)
{
    if (!callback) {
        return;
    }
    if (!m_restClient) {
        AuthResult result;
        result.ok = false;
        result.errorMessage = "AuthService가 초기화되지 않았습니다.";
        callback(result);
        return;
    }

    QJsonObject body;
    body.insert("id", username.trimmed());
    body.insert("pw", password);

    m_restClient->postJson(
        m_loginPath,
        body,
        "login",
        true,
        context,
        [this, callback = std::move(callback)](const RestResponse &resp) mutable {
        AuthResult result;
        result.ok = resp.ok;
        result.httpStatus = resp.httpStatus;
        result.payload = resp.json;
        result.errorMessage = extractErrorMessage(resp, "로그인에 실패했습니다.");

        if (result.ok) {
            result.accessToken = extractAccessToken(resp.json);
            result.userId = extractUserId(resp.json);
            if (result.accessToken.isEmpty()) {
                result.ok = false;
                result.errorMessage = "로그인 응답에 access token이 없습니다.";
            }
        }
        callback(result);
    });
}

void AuthService::signup(
    const QString &name,
    const QString &username,
    const QString &password,
    QObject *context,
    std::function<void(const AuthResult &)> callback)
{
    if (!callback) {
        return;
    }
    if (!m_restClient) {
        AuthResult result;
        result.ok = false;
        result.errorMessage = "AuthService가 초기화되지 않았습니다.";
        callback(result);
        return;
    }

    QJsonObject body;
    body.insert("name", name.trimmed());
    body.insert("id", username.trimmed());
    body.insert("pw", password);

    m_restClient->postJson(
        m_signupPath,
        body,
        "signup",
        true,
        context,
        [this, callback = std::move(callback)](const RestResponse &resp) mutable {
        AuthResult result;
        result.ok = resp.ok;
        result.httpStatus = resp.httpStatus;
        result.payload = resp.json;
        result.errorMessage = extractErrorMessage(resp, "회원가입에 실패했습니다.");
        callback(result);
    });
}

void AuthService::logout(
    QObject *context,
    std::function<void(const AuthResult &)> callback)
{
    if (!m_restClient) {
        AuthResult result;
        result.ok = false;
        result.errorMessage = "AuthService가 초기화되지 않았습니다.";
        if (callback) {
            callback(result);
        }
        return;
    }

    m_restClient->postJson(
        m_logoutPath,
        QJsonObject{},
        "logout",
        true,
        context,
        [this, callback = std::move(callback)](const RestResponse &resp) mutable {
            AuthResult result;
            result.ok = resp.ok;
            result.httpStatus = resp.httpStatus;
            result.payload = resp.json;
            result.errorMessage = extractErrorMessage(resp, "로그아웃에 실패했습니다.");
            if (callback) {
                callback(result);
            }
        });
}

QString AuthService::extractAccessToken(const QJsonObject &obj) const
{
    const QString directAccess = obj.value("accessToken").toString().trimmed();
    if (!directAccess.isEmpty()) {
        return directAccess;
    }
    const QString directToken = obj.value("token").toString().trimmed();
    if (!directToken.isEmpty()) {
        return directToken;
    }
    const QJsonValue dataValue = obj.value("data");
    if (dataValue.isObject()) {
        const QJsonObject dataObj = dataValue.toObject();
        const QString nestedAccess = dataObj.value("accessToken").toString().trimmed();
        if (!nestedAccess.isEmpty()) {
            return nestedAccess;
        }
        const QString nestedToken = dataObj.value("token").toString().trimmed();
        if (!nestedToken.isEmpty()) {
            return nestedToken;
        }
    }
    return {};
}

QString AuthService::extractUserId(const QJsonObject &obj) const
{
    const QString id = jsonValueToString(obj.value("userId"));
    if (!id.isEmpty()) {
        return id;
    }
    const QString altId = jsonValueToString(obj.value("id"));
    if (!altId.isEmpty()) {
        return altId;
    }
    const QJsonValue dataValue = obj.value("data");
    if (dataValue.isObject()) {
        const QJsonObject dataObj = dataValue.toObject();
        const QString dataUserId = jsonValueToString(dataObj.value("userId"));
        if (!dataUserId.isEmpty()) {
            return dataUserId;
        }
        const QString dataId = jsonValueToString(dataObj.value("id"));
        if (!dataId.isEmpty()) {
            return dataId;
        }
        if (dataObj.value("user").isObject()) {
            const QJsonObject userObj = dataObj.value("user").toObject();
            const QString nestedUserId = jsonValueToString(userObj.value("id"));
            if (!nestedUserId.isEmpty()) {
                return nestedUserId;
            }
        }
    }
    return {};
}

QString AuthService::extractErrorMessage(const RestResponse &resp, const QString &fallback) const
{
    const QJsonObject obj = resp.json;
    const QString code = jsonValueToString(obj.value("code"));
    if (code == "DUPLICATE_USER" || code == "USER_ALREADY_EXISTS") {
        return "이미 사용 중인 아이디입니다.";
    }
    if (resp.httpStatus == 409) {
        return "이미 사용 중인 아이디입니다.";
    }
    const QString message = obj.value("message").toString().trimmed();
    if (!message.isEmpty()) {
        return message;
    }
    const QString error = obj.value("error").toString().trimmed();
    if (!error.isEmpty()) {
        return error;
    }
    const QString errors = firstErrorFromErrorsNode(obj.value("errors"));
    if (!errors.isEmpty()) {
        return errors;
    }
    if (resp.httpStatus == 400) {
        return "입력값을 확인해 주세요.";
    }
    if (resp.httpStatus == 403) {
        return "요청이 거부되었습니다.";
    }
    if (resp.httpStatus >= 500) {
        return "서버 오류로 요청을 처리하지 못했습니다.";
    }
    if (!resp.errorMessage.trimmed().isEmpty()) {
        return resp.errorMessage.trimmed();
    }
    return fallback;
}
