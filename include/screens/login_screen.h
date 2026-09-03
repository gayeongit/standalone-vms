#ifndef LOGIN_SCREEN_H
#define LOGIN_SCREEN_H

#include "selected_channel_context.h"

#include <QPushButton>
#include <QString>
#include <QWidget>
#include <QVector>

class QLineEdit;
class QLabel;
class QPaintEvent;
class QTreeWidget;
class QTreeWidgetItem;
class DeviceService;
class QVariantAnimation;
struct DeviceServiceResult;

class SpinningButton : public QPushButton
{
    Q_OBJECT
public:
    explicit SpinningButton(QWidget *parent = nullptr);
    void setSpinning(bool spinning);
    bool isSpinning() const { return m_spinning; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_spinning = false;
    qreal m_angle = 0;
    QVariantAnimation *m_animation = nullptr;
};

class LoginScreen : public QWidget
{
    Q_OBJECT
public:
    explicit LoginScreen(QWidget *parent = nullptr);
    void setLoginInProgress(bool inProgress);
    void showLoginError(const QString &message);
    void showConfigError(const QString &message);
    void clearLoginStatus();
    void resetLoginInputs();

signals:
    void loginRequested(const QString &username, const QString &password);
    void signupRequested();

private:
    QLineEdit *m_idEdit = nullptr;
    QLineEdit *m_pwEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_loginButton = nullptr;
    QPushButton *m_signupButton = nullptr;
};

class SignupScreen : public QWidget
{
    Q_OBJECT
public:
    explicit SignupScreen(QWidget *parent = nullptr);
    void setSignupInProgress(bool inProgress);
    void showSignupError(const QString &message);
    void clearSignupStatus();
    void resetSignupInputs();

signals:
    void signupRequested(const QString &name, const QString &username, const QString &password);
    void backToLoginRequested();

private:
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_idEdit = nullptr;
    QLineEdit *m_pwEdit = nullptr;
    QLineEdit *m_pwConfirmEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_backButton = nullptr;
};

class DeviceCheckScreen : public QWidget
{
    Q_OBJECT
public:
    explicit DeviceCheckScreen(QWidget *parent = nullptr);
    void setDeviceService(DeviceService *service);
    void refreshDevices();

signals:
    void startRequested(const QVector<SelectedChannelContext> &selectedContexts);
    void backToLoginRequested();

private:
    void reloadDevices(int retryCount = 0);
    void applyDeviceTree(const QVector<SelectedChannelContext> &contexts);
    QVector<SelectedChannelContext> selectedContexts() const;
    void setUiBusy(bool busy);
    void showDeviceStatusMessage(const QString &message, const QString &state);
    QString describeFetchError(const DeviceServiceResult &result) const;

    DeviceService *m_deviceService = nullptr;
    QTreeWidget *m_cctvTree = nullptr;
    QTreeWidget *m_ugvTree = nullptr;
    QLabel *m_statusLabel = nullptr;
    SpinningButton *m_refreshButton = nullptr;
    QPushButton *m_startButton = nullptr;
    int m_reloadGeneration = 0;
};

#endif // LOGIN_SCREEN_H
