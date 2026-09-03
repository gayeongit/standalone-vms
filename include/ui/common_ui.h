#ifndef COMMON_UI_H
#define COMMON_UI_H

#include "channel_context_dnd_helpers.h"
#include "channel_types.h"
#include "capture_storage_helpers.h"
#include "event_types.h"
#include "feedback_ui_helpers.h"
#include "stream_player.h"

#include <QEvent>
#include <QColor>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <functional>

class QWidget;
class QTreeWidget;
class TopbarWidget;

class DoubleClickFilter final : public QObject
{
public:
    explicit DoubleClickFilter(std::function<void()> onDoubleClick, QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    std::function<void()> m_onDoubleClick;
};

QPushButton *makePrimaryButton(const QString &text, QWidget *parent);
QColor statusColor(ChannelStatus status);
bool showEventDetailDialog(QWidget *parent, const EventInfo &eventInfo, bool showDispatchButton);
void openEventSearchDialog(QWidget *parent);
void openNotificationCenterDialog(QWidget *parent, TopbarWidget *topbar = nullptr);
void applyNotificationUnreadState(TopbarWidget *topbar, bool unread, int unreadCount = -1);
QTreeWidget *createPlaybackTree(QWidget *parent);
void setSidebarTabState(QPushButton *channelTab, QPushButton *playbackTab, bool channelActive);
void applyNativeDarkTitleBar(QWidget *widget);

#endif // COMMON_UI_H
