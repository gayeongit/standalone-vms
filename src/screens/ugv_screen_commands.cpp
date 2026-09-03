#include "ugv_screen.h"
#include <QDebug>
#include <QSpinBox>
#include <QSlider>
#include <QTimer>
#include <QToolButton>

void UgvScreen::sendDriveCommand(int forward, int back, int left, int right, int timeoutMs)
{
    // UI 버튼/키보드 hold 로직은 이 함수를 통해 서비스 계층으로 수렴한다.
    // 현재는 디버깅을 위해 실제 전송값을 qInfo()로 남기고 있으며,
    // 세션이 ConnectedUgv가 아닐 때는 조용히 무시한다.
    const int level = qBound(0, m_driveSpeedLevel, 3);
    const int payloadForward = forward > 0 ? level : 0;
    const int payloadBack = back > 0 ? level : 0;
    const int payloadLeft = left > 0 ? level : 0;
    const int payloadRight = right > 0 ? level : 0;
    qInfo().noquote() << QString("ugv ui sendDriveCommand dir=(%1,%2,%3,%4) level=%5 payload=(%6,%7,%8,%9) timeout=%10")
                             .arg(forward)
                             .arg(back)
                             .arg(left)
                             .arg(right)
                             .arg(level)
                             .arg(payloadForward)
                             .arg(payloadBack)
                             .arg(payloadLeft)
                             .arg(payloadRight)
                             .arg(timeoutMs);
    if (!m_ugvService || m_ugvService->sessionState() != UgvService::SessionState::ConnectedUgv) {
        return;
    }
    UgvTarget target;
    if (!resolveCurrentUgvTarget(&target)) {
        return;
    }
    m_ugvService->sendDrive(
        target.gatewayId, target.ugvId, payloadForward, payloadBack, payloadLeft, payloadRight, timeoutMs);
}

void UgvScreen::startDriveHold(int forward, int back, int left, int right)
{
    // Drive는 "누르는 동안 유지"가 중요하므로 pressed 시 한 번 보내고 끝내지 않는다.
    // active 방향을 저장한 뒤 즉시 1회 전송하고,
    // repeat timer가 살아 있는 동안 같은 값을 주기적으로 재전송한다.
    qInfo().noquote() << QString("ugv ui startDriveHold fwd=%1 back=%2 left=%3 right=%4")
                             .arg(forward)
                             .arg(back)
                             .arg(left)
                             .arg(right);
    m_activeDriveForward = forward;
    m_activeDriveBack = back;
    m_activeDriveLeft = left;
    m_activeDriveRight = right;
    sendDriveCommand(m_activeDriveForward, m_activeDriveBack, m_activeDriveLeft, m_activeDriveRight, 260);
    if (m_driveRepeatTimer) {
        m_driveRepeatTimer->start();
    }
}

void UgvScreen::stopDriveHold()
{
    // hold 해제 시에는 timer를 멈추고, 실제로 움직이는 명령이 있었을 때만
    // 0,0,0,0 stop payload를 한 번 전송한다.
    // 즉 버튼을 떼는 순간의 정지 명령은 여기서만 일관되게 만들어진다.
    qInfo().noquote() << QString("ugv ui stopDriveHold hadActive=%1 fwd=%2 back=%3 left=%4 right=%5")
                             .arg((m_activeDriveForward != 0 || m_activeDriveBack != 0
                                   || m_activeDriveLeft != 0 || m_activeDriveRight != 0)
                                      ? "true"
                                      : "false")
                             .arg(m_activeDriveForward)
                             .arg(m_activeDriveBack)
                             .arg(m_activeDriveLeft)
                             .arg(m_activeDriveRight);
    if (m_driveRepeatTimer) {
        m_driveRepeatTimer->stop();
    }
    const bool hadActiveCommand = (m_activeDriveForward != 0 || m_activeDriveBack != 0
                                   || m_activeDriveLeft != 0 || m_activeDriveRight != 0);
    m_activeDriveForward = 0;
    m_activeDriveBack = 0;
    m_activeDriveLeft = 0;
    m_activeDriveRight = 0;
    if (m_driveUpButton) {
        m_driveUpButton->setDown(false);
    }
    if (m_driveDownButton) {
        m_driveDownButton->setDown(false);
    }
    if (m_driveLeftButton) {
        m_driveLeftButton->setDown(false);
    }
    if (m_driveRightButton) {
        m_driveRightButton->setDown(false);
    }
    if (hadActiveCommand) {
        sendDriveCommand(0, 0, 0, 0, 120);
    }
}

void UgvScreen::sendPtzCommand(double pan, double tilt, double zoom, int timeoutMs)
{
    // PTZ는 상대 이동이 아니라 서버가 기대하는 절대 pan/tilt 값으로 보낸다.
    // 화면의 pad / WASD / 숫자 입력이 모두 이 함수 하나로 모이기 때문에
    // 현재 PTZ 정책(0~180, tilt 70~180, center 90/90)의 최종 송신 지점이라고 보면 된다.
    if (!m_ugvService || m_ugvService->sessionState() != UgvService::SessionState::ConnectedUgv) {
        return;
    }
    UgvTarget target;
    if (!resolveCurrentUgvTarget(&target)) {
        return;
    }
    m_ugvService->sendPtz(target.gatewayId, target.ugvId, pan, tilt, zoom, timeoutMs);
}

void UgvScreen::startPtzHold(double pan, double tilt)
{
    // PTZ hold는 drive와 달리 "현재 절대값을 조금씩 갱신"하는 방식이다.
    // 즉 active delta를 저장하고 spin 값 자체를 step 만큼 움직인 뒤,
    // 바뀐 절대 pan/tilt를 서버로 다시 전송한다.
    m_activePanCommand = pan;
    m_activeTiltCommand = tilt;
    if (m_panSpin && !qFuzzyIsNull(m_activePanCommand)) {
        const int nextPan = qBound(m_panSpin->minimum(),
                                   m_panSpin->value() + static_cast<int>(m_activePanCommand),
                                   m_panSpin->maximum());
        m_panSpin->setValue(nextPan);
    }
    if (m_tiltSpin && !qFuzzyIsNull(m_activeTiltCommand)) {
        const int nextTilt = qBound(m_tiltSpin->minimum(),
                                    m_tiltSpin->value() + static_cast<int>(m_activeTiltCommand),
                                    m_tiltSpin->maximum());
        m_tiltSpin->setValue(nextTilt);
    }
    sendPtzCommand(m_panSpin ? m_panSpin->value() : 90,
                   m_tiltSpin ? m_tiltSpin->value() : 90);
    if (m_ptzRepeatTimer) {
        m_ptzRepeatTimer->start();
    }
}

void UgvScreen::stopPtzHold()
{
    if (m_ptzRepeatTimer) {
        m_ptzRepeatTimer->stop();
    }
    const bool hadActiveCommand = !qFuzzyIsNull(m_activePanCommand) || !qFuzzyIsNull(m_activeTiltCommand);
    m_activePanCommand = 0.0;
    m_activeTiltCommand = 0.0;
    if (m_dpadUpButton) {
        m_dpadUpButton->setDown(false);
    }
    if (m_dpadDownButton) {
        m_dpadDownButton->setDown(false);
    }
    if (m_dpadLeftButton) {
        m_dpadLeftButton->setDown(false);
    }
    if (m_dpadRightButton) {
        m_dpadRightButton->setDown(false);
    }
    Q_UNUSED(hadActiveCommand);
}
