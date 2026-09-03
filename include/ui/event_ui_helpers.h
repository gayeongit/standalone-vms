#ifndef EVENT_UI_HELPERS_H
#define EVENT_UI_HELPERS_H

#include "event_types.h"

#include <QVector>

class QWidget;
class TopbarWidget;
class EventService;

namespace EventUiHelpers {

void setEventService(EventService *service);
EventService *eventService();
QVector<EventInfo> currentEvents();
int currentUnreadCount();

bool showEventDetailDialog(QWidget *parent, const EventInfo &eventInfo, bool showDispatchButton);
void openEventSearchDialog(QWidget *parent);
void openNotificationCenterDialog(QWidget *parent, TopbarWidget *topbar = nullptr);
void applyNotificationUnreadState(TopbarWidget *topbar, bool unread, int unreadCount = -1);

} // namespace EventUiHelpers

#endif // EVENT_UI_HELPERS_H
