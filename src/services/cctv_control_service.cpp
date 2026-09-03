#include "cctv_control_service.h"

#include "rest_client.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

namespace {

QString jsonString(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString().trimmed();
    }
    if (value.isDouble()) {
        return QString::number(value.toVariant().toLongLong());
    }
    return {};
}

QString nestedErrorCode(const QJsonObject &root)
{
    const QString directCode = jsonString(root.value(QStringLiteral("code")));
    if (!directCode.isEmpty()) {
        return directCode;
    }
    const QJsonValue errorValue = root.value(QStringLiteral("error"));
    if (errorValue.isString()) {
        return errorValue.toString().trimmed();
    }
    if (errorValue.isObject()) {
        return jsonString(errorValue.toObject().value(QStringLiteral("code")));
    }
    return {};
}

QString nestedMessage(const QJsonObject &root)
{
    const QString directMessage = jsonString(root.value(QStringLiteral("message")));
    if (!directMessage.isEmpty()) {
        return directMessage;
    }
    const QJsonValue errorValue = root.value(QStringLiteral("error"));
    if (errorValue.isString()) {
        return errorValue.toString().trimmed();
    }
    if (errorValue.isObject()) {
        const QJsonObject errorObject = errorValue.toObject();
        const QString errorMessage = jsonString(errorObject.value(QStringLiteral("message")));
        if (!errorMessage.isEmpty()) {
            return errorMessage;
        }
        return jsonString(errorObject.value(QStringLiteral("code")));
    }
    return {};
}

} // namespace

CctvControlService::CctvControlService(RestClient *restClient, QObject *parent)
    : QObject(parent)
    , m_restClient(restClient)
{
}

void CctvControlService::setZoomPathTemplate(const QString &pathTemplate)
{
    const QString trimmed = pathTemplate.trimmed();
    if (!trimmed.isEmpty()) {
        m_zoomPathTemplate = trimmed;
    }
}

void CctvControlService::setFocusPathTemplate(const QString &pathTemplate)
{
    const QString trimmed = pathTemplate.trimmed();
    if (!trimmed.isEmpty()) {
        m_focusPathTemplate = trimmed;
    }
}

void CctvControlService::zoomStep(
    int channelId,
    int value,
    QObject *context,
    std::function<void(const CctvControlResult &)> callback)
{
    requestControl(m_zoomPathTemplate, QStringLiteral("cctv_zoom"), channelId, value, context, std::move(callback));
}

void CctvControlService::focusStep(
    int channelId,
    int value,
    QObject *context,
    std::function<void(const CctvControlResult &)> callback)
{
    requestControl(m_focusPathTemplate, QStringLiteral("cctv_focus"), channelId, value, context, std::move(callback));
}

bool CctvControlService::isSupportedStepValue(int value)
{
    static const QSet<int> supportedValues = {-100, -10, -1, 1, 10, 100};
    return supportedValues.contains(value);
}

void CctvControlService::requestControl(
    const QString &pathTemplate,
    const QString &requestTag,
    int channelId,
    int value,
    QObject *context,
    std::function<void(const CctvControlResult &)> callback)
{
    CctvControlResult base;
    base.channelId = channelId;
    base.value = value;

    if (channelId < 0) {
        base.errorMessage = QStringLiteral("channelId가 유효하지 않습니다.");
        if (callback) {
            callback(base);
        }
        return;
    }
    if (!isSupportedStepValue(value)) {
        base.errorCode = QStringLiteral("INVALID_ARGUMENT");
        base.errorMessage = QStringLiteral("지원하지 않는 제어 값입니다.");
        if (callback) {
            callback(base);
        }
        return;
    }
    if (!m_restClient) {
        base.errorMessage = QStringLiteral("CctvControlService가 초기화되지 않았습니다.");
        if (callback) {
            callback(base);
        }
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("value"), value);

    m_restClient->postJson(
        resolvePathTemplate(pathTemplate, channelId),
        body,
        requestTag,
        false,
        context ? context : this,
        [this, callback = std::move(callback), channelId, value](const RestResponse &response) mutable {
            CctvControlResult result;
            result.ok = response.ok;
            result.httpStatus = response.httpStatus;
            result.channelId = channelId;
            result.value = value;
            result.errorCode = extractErrorCode(response);
            result.errorMessage = extractErrorMessage(response, QStringLiteral("CCTV 제어 요청에 실패했습니다."));

            const QJsonObject dataObject = response.json.value(QStringLiteral("data")).toObject();
            result.result = jsonString(dataObject.value(QStringLiteral("result")));
            if (result.result.isEmpty()) {
                result.result = jsonString(response.json.value(QStringLiteral("result")));
            }

            if (callback) {
                callback(result);
            }
        });
}

QString CctvControlService::resolvePathTemplate(const QString &pathTemplate, int channelId) const
{
    QString path = pathTemplate.trimmed();
    path.replace(QStringLiteral("{channelId}"), QString::number(channelId));
    return path;
}

QString CctvControlService::extractErrorCode(const RestResponse &response) const
{
    return nestedErrorCode(response.json);
}

QString CctvControlService::extractErrorMessage(const RestResponse &response, const QString &fallback) const
{
    const QString message = nestedMessage(response.json);
    if (!message.isEmpty()) {
        return message;
    }
    if (!response.errorMessage.trimmed().isEmpty()) {
        return response.errorMessage.trimmed();
    }
    if (response.httpStatus == 400) {
        return QStringLiteral("제어 값이 올바르지 않습니다.");
    }
    if (response.httpStatus == 404) {
        return QStringLiteral("채널을 찾을 수 없습니다.");
    }
    if (response.httpStatus == 409) {
        return QStringLiteral("장치 상태로 인해 제어를 수행할 수 없습니다.");
    }
    if (response.httpStatus >= 500) {
        return QStringLiteral("서버 오류로 CCTV 제어에 실패했습니다.");
    }
    return fallback;
}
