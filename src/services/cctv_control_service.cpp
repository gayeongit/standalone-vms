#include "cctv_control_service.h"

#include "app_state.h"
#include "onvif_lite_client.h"

#include <QSet>

// Phase 2: zoom/focus는 서버 프록시를 거치지 않고 항상 카메라(ONVIF PTZ/Imaging)로 직접 간다.
// 서버는 원래도 이 기능에 필수가 아니었음(카메라가 자체적으로 PTZ를 지원) — 서버 상태와 무관.
// channelId -> ONVIF xaddr/profileToken 매핑은 Phase 1의 DeviceService가 discovery 시점에
// AppState.channelOnvifXAddrById/channelOnvifProfileTokenById로 이미 채워둔다 (설계 원칙 1).

CctvControlService::CctvControlService(OnvifLiteClient *onvifClient, QObject *parent)
    : QObject(parent)
    , m_onvifClient(onvifClient)
{
}

void CctvControlService::zoomStep(
    int channelId,
    int value,
    QObject *context,
    std::function<void(const CctvControlResult &)> callback)
{
    requestControl(ControlKind::Zoom, channelId, value, context, std::move(callback));
}

void CctvControlService::focusStep(
    int channelId,
    int value,
    QObject *context,
    std::function<void(const CctvControlResult &)> callback)
{
    requestControl(ControlKind::Focus, channelId, value, context, std::move(callback));
}

bool CctvControlService::isSupportedStepValue(int value)
{
    static const QSet<int> supportedValues = {-100, -10, -1, 1, 10, 100};
    return supportedValues.contains(value);
}

void CctvControlService::requestControl(
    ControlKind kind,
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
        base.errorMessage = QStringLiteral("지원하지 않는 제어 값입니다.");
        if (callback) {
            callback(base);
        }
        return;
    }
    if (!m_onvifClient) {
        base.errorMessage = QStringLiteral("CctvControlService가 초기화되지 않았습니다.");
        if (callback) {
            callback(base);
        }
        return;
    }

    const auto &state = AppState::instance();
    const QString xaddr = state.channelOnvifXAddrById.value(channelId).trimmed();
    const QString token = state.channelOnvifProfileTokenById.value(channelId).trimmed();
    if (xaddr.isEmpty() || token.isEmpty()) {
        base.errorMessage = QStringLiteral("이 채널의 카메라 제어 주소를 찾을 수 없습니다.");
        if (callback) {
            callback(base);
        }
        return;
    }

    // ONVIF 정규화 공간(-1.0~1.0)에 맞춰 -100~100 step을 스케일링.
    const double delta = value / 100.0;

    auto onDone = [callback, channelId, value](bool ok, const QString &errorMessage) {
        if (!callback) {
            return;
        }
        CctvControlResult result;
        result.ok = ok;
        result.channelId = channelId;
        result.value = value;
        if (!ok) {
            const QString trimmed = errorMessage.trimmed();
            result.errorMessage = trimmed.isEmpty() ? QStringLiteral("CCTV 제어 요청에 실패했습니다.") : trimmed;
        }
        callback(result);
    };

    QObject *effectiveContext = context ? context : this;
    if (kind == ControlKind::Zoom) {
        m_onvifClient->relativeMove(xaddr, token, delta, effectiveContext, onDone);
    } else {
        m_onvifClient->imagingRelativeMove(xaddr, token, delta, effectiveContext, onDone);
    }
}
