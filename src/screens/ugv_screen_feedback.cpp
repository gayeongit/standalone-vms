#include "ugv_screen.h"
#include "common_ui.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <algorithm>

namespace {

struct UgvServiceErrorPresentation
{
    QString displayMessage;
    bool showSnackbar = false;
    int durationMs = 3000;
};

UgvServiceErrorPresentation classifyUgvServiceError(const QString &message)
{
    const QString trimmed = message.trimmed();
    UgvServiceErrorPresentation presentation;
    presentation.displayMessage = trimmed;

    if (trimmed.contains("DEVICE_OFFLINE", Qt::CaseInsensitive)
        || trimmed.contains("ROUTE_UNAVAILABLE", Qt::CaseInsensitive)
        || trimmed.contains("SOCKET_CLOSED", Qt::CaseInsensitive)
        || trimmed.contains("UNAUTHORIZED", Qt::CaseInsensitive)
        || trimmed.contains("failed to connect gateway websocket", Qt::CaseInsensitive)) {
        presentation.displayMessage = QStringLiteral("UGV device offline or gateway disconnected");
        presentation.showSnackbar = true;
        return presentation;
    }

    if (trimmed.contains("ACK timeout", Qt::CaseInsensitive)) {
        presentation.durationMs = 2500;
        return presentation;
    }

    if (trimmed.contains("telemetry.gps ACK", Qt::CaseInsensitive)
        || trimmed.contains("telemetry.rssi ACK", Qt::CaseInsensitive)) {
        presentation.durationMs = 2000;
        return presentation;
    }

    presentation.showSnackbar = true;
    return presentation;
}

} // namespace

void UgvScreen::showSnackbar(const QString &message)
{
    if (!m_ugvSnackbarFrame || !m_ugvSnackbarLabel) {
        return;
    }
    m_ugvSnackbarLabel->setText(message);
    m_ugvSnackbarFrame->adjustSize();
    placeSnackbar();
    m_ugvSnackbarFrame->show();
    m_ugvSnackbarFrame->raise();
    if (m_snackbarHideTimer) {
        m_snackbarHideTimer->start();
    }
}

void UgvScreen::clearSnackbar()
{
    if (m_snackbarHideTimer) {
        m_snackbarHideTimer->stop();
    }
    if (m_ugvSnackbarFrame) {
        m_ugvSnackbarFrame->hide();
    }
}

void UgvScreen::placeSnackbar()
{
    if (!m_ugvSnackbarFrame) {
        return;
    }
    m_ugvSnackbarFrame->adjustSize();
    const int marginRight = 12;
    const int marginBottom = 12;
    const int x = std::max(0, width() - m_ugvSnackbarFrame->width() - marginRight);
    const int y = std::max(0, height() - m_ugvSnackbarFrame->height() - marginBottom);
    m_ugvSnackbarFrame->move(x, y);
}

void UgvScreen::updateSessionUi()
{
    // UGV 화면의 컨트롤 활성화/문구/기본값을 세션 상태에 맞춰 한 곳에서 정리한다.
    // 세션 상태를 읽어 버튼/입력 위젯 enabled 상태, 미션 버튼 문구, action status를 한 번에 갱신한다.
    // 특히 disconnected 시 drive/PTZ hold를 끊고 pan/tilt 값을 90/90으로 되돌리는 규칙은
    // 서버와 약속한 "연결 종료 후 정면 초기화" 정책을 UI에 반영한 것이다.
    const auto state = m_ugvService ? m_ugvService->sessionState() : UgvService::SessionState::Disconnected;
    const bool connected = (state == UgvService::SessionState::ConnectedUgv);
    const bool connecting = (state == UgvService::SessionState::SocketConnecting
                             || state == UgvService::SessionState::SocketConnected
                             || state == UgvService::SessionState::ConnectingUgv
                             || state == UgvService::SessionState::DisconnectingUgv);

    if (!connected && !m_pressedDriveKeys.isEmpty()) {
        m_pressedDriveKeys.clear();
    }
    if (!connected && !m_pressedPtzKeys.isEmpty()) {
        m_pressedPtzKeys.clear();
    }
    if (!connected) {
        stopDriveHold();
        stopPtzHold();
    }

    if (!connected && !connecting) {
        resetTelemetryAndMapState(true);
    }

    const auto setEnabled = [connected](QWidget *widget) {
        if (widget) {
            widget->setEnabled(connected);
        }
    };
    setEnabled(m_driveUpButton);
    setEnabled(m_driveLeftButton);
    setEnabled(m_driveStopButton);
    setEnabled(m_driveRightButton);
    setEnabled(m_driveDownButton);
    setEnabled(m_driveSpeedSlider);
    setEnabled(m_dpadUpButton);
    setEnabled(m_dpadLeftButton);
    setEnabled(m_dpadCenterButton);
    setEnabled(m_dpadRightButton);
    setEnabled(m_dpadDownButton);
    setEnabled(m_panSpin);
    setEnabled(m_tiltSpin);

    if (m_sessionButton) {
        if (connected) {
            m_sessionButton->setText("End Mission");
        } else if (connecting) {
            m_sessionButton->setText("Connecting...");
        } else {
            m_sessionButton->setText("Start Mission");
        }
        m_sessionButton->setEnabled(!connecting);
    }
    if (m_actionStatusLabel) {
        if (connected) {
            showActionStatus(m_actionStatusLabel, "Connected", "success");
        } else if (connecting) {
            showActionStatus(m_actionStatusLabel, "Connecting...", "progress");
        } else {
            showActionStatus(m_actionStatusLabel, "Disconnected", "error");
        }
    }
    refreshSidebarStatus();
}

void UgvScreen::presentServiceError(const QString &message)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    const UgvServiceErrorPresentation presentation = classifyUgvServiceError(trimmed);
    const QString display = presentation.displayMessage.isEmpty() ? trimmed : presentation.displayMessage;
    showActionStatus(m_actionStatusLabel, display, "error", presentation.durationMs);
}



