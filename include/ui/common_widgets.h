#ifndef COMMON_WIDGETS_H
#define COMMON_WIDGETS_H

#include "event_types.h"

#include <QFrame>
#include <QString>
#include <QVector>

class QLabel;
class PlaybackService;
class QPushButton;
class QTreeWidget;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

class TopbarWidget : public QFrame
{
    Q_OBJECT
public:
    struct Config {
        bool showNotification = false;
        bool showSettings = false;
        bool showLogout = false;
    };

    explicit TopbarWidget(const Config &config, QWidget *parent = nullptr);
    void setNotificationUnread(bool unread);
    void clearGlobalStatusMessage();

signals:
    void notificationCenterClicked();
    void settingsClicked();
    void logoutClicked();

private:
    class QLabel *m_notificationBadge = nullptr;
    class QLabel *m_globalStatusLabel = nullptr;
};

class EventViewWidget : public QFrame
{
    Q_OBJECT
public:
    explicit EventViewWidget(QWidget *parent = nullptr);

    void setEvents(const QVector<EventInfo> &events, int maxItems, bool showDispatchButton);

signals:
    void searchRequested();
    void ugvDispatchRequested();

private:
    QVBoxLayout *m_contentLayout = nullptr;
};

class SidebarWidget : public QFrame
{
    Q_OBJECT
public:
    struct Config {
        bool showTabs = true;
        bool showBottomButtons = true;
        QString primaryBottomText = "Snapshot";
        QString secondaryBottomText = "Clip";
    };

    explicit SidebarWidget(const Config &config, QWidget *parent = nullptr);

    void populateChannelTree();
    void setPlaybackService(PlaybackService *service);
    void reloadPlaybackTree();

    QPushButton *channelTab() const;
    QPushButton *playbackTab() const;
    QWidget *channelPage() const;
    QTreeWidget *channelTree() const;
    QTreeWidget *cctvTree() const;
    QTreeWidget *ugvTree() const;
    QTreeWidget *playbackTree() const;
    QStackedWidget *treeStack() const;
    QPushButton *primaryBottomButton() const;
    QPushButton *secondaryBottomButton() const;
    QLabel *actionStatusLabel() const;
    QVBoxLayout *preTreeLayout() const;
    QVBoxLayout *controlsLayout() const;

private:
    QPushButton *m_channelTab = nullptr;
    QPushButton *m_playbackTab = nullptr;
    QWidget *m_channelPage = nullptr;
    QTreeWidget *m_channelTree = nullptr;
    QTreeWidget *m_ugvTree = nullptr;
    QTreeWidget *m_playbackTree = nullptr;
    QStackedWidget *m_treeStack = nullptr;
    QPushButton *m_primaryBottomButton = nullptr;
    QPushButton *m_secondaryBottomButton = nullptr;
    QLabel *m_actionStatusLabel = nullptr;
    QVBoxLayout *m_preTreeLayout = nullptr;
    QVBoxLayout *m_controlsLayout = nullptr;
    PlaybackService *m_playbackService = nullptr;
    int m_playbackTreeLoadGeneration = 0;
};

void highlightSidebarChannelItem(QTreeWidget *tree, const QString &displayName);
void highlightSidebarPlaybackItem(QTreeWidget *tree, int channelId, const QString &displayName);

#endif // COMMON_WIDGETS_H
