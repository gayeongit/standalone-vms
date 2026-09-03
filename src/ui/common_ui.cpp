#include "common_ui.h"

#include "common_widgets.h"
#include "event_ui_helpers.h"

#include <QColor>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QWidget>
#include <QWindow>

#include <functional>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dwmapi.h>
#  ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#    define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#  endif
#  ifndef DWMWA_BORDER_COLOR
#    define DWMWA_BORDER_COLOR 34
#  endif
#  ifndef DWMWA_CAPTION_COLOR
#    define DWMWA_CAPTION_COLOR 35
#  endif
#  ifndef DWMWA_TEXT_COLOR
#    define DWMWA_TEXT_COLOR 36
#  endif
#  pragma comment(lib, "Dwmapi.lib")
#endif

DoubleClickFilter::DoubleClickFilter(std::function<void()> onDoubleClick, QObject *parent)
    : QObject(parent)
    , m_onDoubleClick(std::move(onDoubleClick))
{}

bool DoubleClickFilter::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if (event && event->type() == QEvent::MouseButtonDblClick && m_onDoubleClick) {
        m_onDoubleClick();
        return true;
    }
    return false;
}

QPushButton *makePrimaryButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setMinimumHeight(36);
    button->setObjectName("primaryButton");
    return button;
}

QColor statusColor(ChannelStatus status)
{
    switch (status) {
    case ChannelStatus::Connected:
        return QColor("#34C759");
    case ChannelStatus::Delayed:
        return QColor("#FFCC00");
    case ChannelStatus::Disconnected:
        return QColor("#FF3B30");
    }
    return QColor("#8E8E93");
}

bool showEventDetailDialog(QWidget *parent, const EventInfo &eventInfo, bool showDispatchButton)
{
    return EventUiHelpers::showEventDetailDialog(parent, eventInfo, showDispatchButton);
}

void openEventSearchDialog(QWidget *parent)
{
    EventUiHelpers::openEventSearchDialog(parent);
}

void applyNotificationUnreadState(TopbarWidget *topbar, bool unread, int unreadCount)
{
    EventUiHelpers::applyNotificationUnreadState(topbar, unread, unreadCount);
}

void openNotificationCenterDialog(QWidget *parent, TopbarWidget *topbar)
{
    EventUiHelpers::openNotificationCenterDialog(parent, topbar);
}

QTreeWidget *createPlaybackTree(QWidget *parent)
{
    auto *tree = new QTreeWidget(parent);
    tree->setObjectName("playbackTree");
    tree->setHeaderHidden(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setIndentation(0);
    return tree;
}

void setSidebarTabState(QPushButton *channelTab, QPushButton *playbackTab, bool channelActive)
{
    if (!channelTab || !playbackTab) {
        return;
    }
    channelTab->setObjectName(channelActive ? "tabActive" : "tabInactive");
    playbackTab->setObjectName(channelActive ? "tabInactive" : "tabActive");
    channelTab->style()->unpolish(channelTab);
    channelTab->style()->polish(channelTab);
    playbackTab->style()->unpolish(playbackTab);
    playbackTab->style()->polish(playbackTab);
}

void applyNativeDarkTitleBar(QWidget *widget)
{
    if (!widget) {
        return;
    }

#ifdef Q_OS_WIN
    widget->winId();
    const HWND hwnd = reinterpret_cast<HWND>(widget->windowHandle() ? widget->windowHandle()->winId() : widget->winId());
    if (!hwnd) {
        return;
    }

    const BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    const COLORREF captionColor = RGB(31, 31, 31);
    const COLORREF borderColor = RGB(55, 55, 55);
    const COLORREF textColor = RGB(255, 255, 255);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
#else
    Q_UNUSED(widget);
#endif
}
