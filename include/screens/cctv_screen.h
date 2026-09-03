#ifndef CCTV_SCREEN_H
#define CCTV_SCREEN_H

#include <QString>
#include <QWidget>

class QLabel;
class QTreeWidget;
class QTimer;
class QShowEvent;
class QHideEvent;
class QResizeEvent;
class QPushButton;
class CctvControlService;

class CctvScreen : public QWidget
{
    Q_OBJECT
public:
    explicit CctvScreen(QWidget *parent = nullptr);
    void setCctvControlService(CctvControlService *service);

signals:
    void backToMainRequested();
    void openUgvRequested();
    void openPlaybackRequested();
    void settingsRequested();
    void logoutRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void refreshStream();
    void refreshOsd();
    int resolveActiveCctvChannelId() const;
    void submitZoomStep(int step);
    void submitFocusStep(int step);

    QLabel *m_channelLabel = nullptr;
    QLabel *m_streamStatusLabel = nullptr;
    QLabel *m_osdTimestampLabel = nullptr;
    QLabel *m_osdConnectionLabel = nullptr;
    QLabel *m_osdChannelLabel = nullptr;
    QWidget *m_videoHost = nullptr;
    QTreeWidget *m_channelTree = nullptr;
    QTimer *m_osdTimer = nullptr;
    QString m_boundChannel;
    QPushButton *m_clipButton = nullptr;
    QLabel *m_actionStatusLabel = nullptr;
    CctvControlService *m_cctvControlService = nullptr;
    bool m_zoomRequestInFlight = false;
    bool m_focusRequestInFlight = false;
    int m_streamRefreshGeneration = 0;
};

#endif // CCTV_SCREEN_H
