#ifndef MAIN_SCREEN_H
#define MAIN_SCREEN_H

#include "event_types.h"

#include <QString>
#include <QVector>
#include <QWidget>

class EventViewWidget;
class QShowEvent;
class QTimer;

class MainScreen : public QWidget
{
    Q_OBJECT
public:
    explicit MainScreen(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

signals:
    void openCctvRequested();
    void openUgvRequested();
    void openPlaybackRequested();
    void settingsRequested();
    void logoutRequested();

private:
    void scheduleEventViewRefresh(const QVector<EventInfo> &events);
    void applyPendingEventViewRefresh();

    EventViewWidget *m_eventView = nullptr;
    QTimer *m_eventViewRefreshTimer = nullptr;
    QVector<EventInfo> m_pendingEventViewEvents;
    bool m_eventViewRefreshPending = false;
    int m_eventViewPendingBurstCount = 0;
    QString m_lastFlushedEventViewHeadKey;
};

#endif // MAIN_SCREEN_H
