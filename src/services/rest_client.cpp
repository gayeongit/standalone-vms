#include "rest_client.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <algorithm>

RestClient::RestClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{}

void RestClient::setBaseUrl(const QString &baseUrl)
{
    m_baseUrl = baseUrl.trimmed();
    while (m_baseUrl.endsWith('/')) {
        m_baseUrl.chop(1);
    }
}

void RestClient::setAccessToken(const QString &accessToken)
{
    m_accessToken = accessToken.trimmed();
}

void RestClient::clearAccessToken()
{
    m_accessToken.clear();
}

void RestClient::setRequestTimeoutMs(int timeoutMs)
{
    m_timeoutMs = std::max(1000, timeoutMs);
}

bool RestClient::isConfigured() const
{
    return !m_baseUrl.isEmpty();
}

QString RestClient::baseUrl() const
{
    return m_baseUrl;
}

void RestClient::getJson(
    const QString &path,
    const QString &requestTag,
    bool suppressUnauthorized,
    QObject *context,
    std::function<void(const RestResponse &)> callback)
{
    requestJson("GET", path, nullptr, requestTag, suppressUnauthorized, context, std::move(callback));
}

void RestClient::postJson(
    const QString &path,
    const QJsonObject &body,
    const QString &requestTag,
    bool suppressUnauthorized,
    QObject *context,
    std::function<void(const RestResponse &)> callback)
{
    requestJson("POST", path, &body, requestTag, suppressUnauthorized, context, std::move(callback));
}

void RestClient::requestJson(
    const QByteArray &method,
    const QString &path,
    const QJsonObject *body,
    const QString &requestTag,
    bool suppressUnauthorized,
    QObject *context,
    std::function<void(const RestResponse &)> callback)
{
    // RestClient의 공통 진입점이다.
    // GET/POST 요청을 만들고 auth header와 timeout timer를 붙인 뒤 finished callback을 공통 경로로 보낸다.
    // 모든 서비스는 결국 이 함수로 모이고, 여기서:
    // - base URL / auth header 적용
    // - timeout timer 부착
    // - context guard를 통한 stale callback 차단
    // 이 한 번에 처리된다.
    if (!callback) {
        return;
    }
    if (!isConfigured()) {
        callback(buildNetworkErrorResponse("API 주소가 설정되지 않았습니다."));
        return;
    }

    QNetworkRequest req(QUrl(resolveUrl(path)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    if (!m_accessToken.isEmpty()) {
        req.setRawHeader("Authorization", QByteArray("Bearer ").append(m_accessToken.toUtf8()));
    }

    QNetworkReply *reply = nullptr;
    if (method == "GET") {
        reply = m_nam->get(req);
    } else if (method == "POST") {
        const QByteArray payload = body ? QJsonDocument(*body).toJson(QJsonDocument::Compact) : QByteArray("{}");
        reply = m_nam->post(req, payload);
    } else {
        callback(buildNetworkErrorResponse("지원하지 않는 HTTP 메서드입니다."));
        return;
    }

    if (!reply) {
        callback(buildNetworkErrorResponse("네트워크 요청 생성에 실패했습니다."));
        return;
    }

    auto *timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(m_timeoutMs);
    QObject::connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
    timeoutTimer->start();

    QPointer<QObject> guard = context;
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, requestTag, suppressUnauthorized, guard, callback = std::move(callback)]() mutable {
        if (!guard) {
            reply->deleteLater();
            return;
        }
        finalizeReply(reply, requestTag, suppressUnauthorized, guard, std::move(callback));
    });
}

RestResponse RestClient::buildNetworkErrorResponse(const QString &message) const
{
    RestResponse resp;
    resp.ok = false;
    resp.errorMessage = message;
    return resp;
}

QString RestClient::resolveUrl(const QString &path) const
{
    QString normalized = path.trimmed();
    if (normalized.startsWith("http://") || normalized.startsWith("https://")) {
        return normalized;
    }
    if (!normalized.startsWith('/')) {
        normalized.prepend('/');
    }
    return m_baseUrl + normalized;
}

void RestClient::finalizeReply(
    QNetworkReply *reply,
    const QString &requestTag,
    bool suppressUnauthorized,
    QObject *context,
    std::function<void(const RestResponse &)> callback)
{
    // 네트워크 계층의 응답을 서비스가 다루기 쉬운 RestResponse로 정규화한다.
    // reply에서 body/status/error/json을 추출해 RestResponse를 만들고 최종 callback에 넘긴다.
    // 여기서 HTTP status / JSON body / error string을 한 번에 추출하고,
    // unauthorized short-circuit 규칙도 적용해 로그아웃 이후 stale UI callback을 막는다.
    Q_UNUSED(context);
    RestResponse resp;
    if (!reply) {
        resp.ok = false;
        resp.errorMessage = "네트워크 응답이 비어 있습니다.";
        callback(resp);
        return;
    }

    QByteArray raw;
    if (reply->isReadable()) {
        raw = reply->readAll();
    }
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    resp.httpStatus = status.isValid() ? status.toInt() : 0;
    resp.requestTag = requestTag;

    if (reply->error() != QNetworkReply::NoError) {
        resp.ok = false;
        resp.errorMessage = reply->errorString();
    } else {
        resp.ok = (resp.httpStatus >= 200 && resp.httpStatus < 300);
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        resp.json = doc.object();
    }

    if (!resp.ok && resp.errorMessage.trimmed().isEmpty()) {
        if (!resp.json.value("error").toString().trimmed().isEmpty()) {
            resp.errorMessage = resp.json.value("error").toString().trimmed();
        } else if (!resp.json.value("message").toString().trimmed().isEmpty()) {
            resp.errorMessage = resp.json.value("message").toString().trimmed();
        } else if (resp.httpStatus > 0) {
            resp.errorMessage = QString("HTTP %1 오류").arg(resp.httpStatus);
        } else {
            resp.errorMessage = "요청 처리에 실패했습니다.";
        }
    }

    // Team rule (v2): global unauthorized path short-circuits per-request callback.
    // Rationale: avoid post-logout UI/state writes from stale callbacks.
    if (resp.httpStatus == 401 && !suppressUnauthorized) {
        emit unauthorizedDetected(requestTag);
        reply->deleteLater();
        return;
    }

    callback(resp);
    reply->deleteLater();
}
