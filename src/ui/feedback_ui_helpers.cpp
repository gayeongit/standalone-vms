#include "feedback_ui_helpers.h"

#include <QLabel>
#include <QStyle>
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include <algorithm>

void clearActionStatus(QLabel *label)
{
    if (!label) {
        return;
    }
    label->clear();
    label->setToolTip({});
    label->setProperty("state", QVariant());
    label->style()->unpolish(label);
    label->style()->polish(label);
    label->hide();
}

void showActionStatus(QLabel *label, const QString &message, const QString &state, int durationMs)
{
    if (!label) {
        return;
    }

    auto *timer = label->findChild<QTimer *>("actionStatusTimer");
    if (!timer) {
        timer = new QTimer(label);
        timer->setObjectName("actionStatusTimer");
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, label, [label]() {
            clearActionStatus(label);
        });
    }
    timer->stop();

    label->setProperty("state", state.trimmed().isEmpty() ? "info" : state.trimmed());
    label->style()->unpolish(label);
    label->style()->polish(label);
    label->setText(message);
    label->setToolTip(message);
    label->show();

    if (durationMs > 0) {
        timer->start(std::max(250, durationMs));
    }
}

namespace {
QPoint toastPositionFor(QWidget *parent, QLabel *label, QWidget *anchor)
{
    const int margin = 16;
    const int gap = 8;
    if (!parent || !label) {
        return QPoint(0, 0);
    }

    int x = std::max(0, parent->width() - label->width() - margin);
    int y = std::max(0, parent->height() - label->height() - margin);

    if (anchor && anchor->isVisible()) {
        const QPoint anchorTopLeft = parent->mapFromGlobal(anchor->mapToGlobal(QPoint(0, 0)));
        // Sidebar action button: align with button start, place above first.
        x = anchorTopLeft.x();
        y = anchorTopLeft.y() - label->height() - gap;
        if (y < margin) {
            y = anchorTopLeft.y() + anchor->height() + gap;
        }
        x = std::clamp(x, margin, std::max(margin, parent->width() - label->width() - margin));
        y = std::clamp(y, margin, std::max(margin, parent->height() - label->height() - margin));
    }
    return QPoint(x, y);
}
} // namespace

void showToastMessage(QWidget *parent, const QString &message, int durationMs, QWidget *anchor)
{
    if (!parent) {
        return;
    }
    auto *toast = new QLabel(message, parent);
    toast->setObjectName("toastMessage");
    toast->setAttribute(Qt::WA_DeleteOnClose, true);
    toast->adjustSize();

    toast->move(toastPositionFor(parent, toast, anchor));
    toast->show();
    toast->raise();
    QTimer::singleShot(std::max(500, durationMs), toast, &QLabel::close);
}

void setWidgetStreamState(QLabel *label, const QString &stateName)
{
    if (!label) {
        return;
    }
    label->setProperty("streamState", stateName);
    label->style()->unpolish(label);
    label->style()->polish(label);
}

void setWidgetChannelState(QLabel *label, ChannelStatus status)
{
    if (!label) {
        return;
    }
    QString state;
    switch (status) {
    case ChannelStatus::Connected:
        state = "connected";
        break;
    case ChannelStatus::Delayed:
        state = "delayed";
        break;
    case ChannelStatus::Disconnected:
        state = "disconnected";
        break;
    }
    label->setProperty("channelState", state);
    label->style()->unpolish(label);
    label->style()->polish(label);
}
