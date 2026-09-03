#ifndef FEEDBACK_UI_HELPERS_H
#define FEEDBACK_UI_HELPERS_H

#include "channel_types.h"

#include <QString>

class QLabel;
class QWidget;

void clearActionStatus(QLabel *label);
void showActionStatus(QLabel *label, const QString &message, const QString &state, int durationMs = 0);
void showToastMessage(QWidget *parent, const QString &message, int durationMs = 2000, QWidget *anchor = nullptr);
void setWidgetStreamState(QLabel *label, const QString &stateName);
void setWidgetChannelState(QLabel *label, ChannelStatus status);

#endif // FEEDBACK_UI_HELPERS_H
