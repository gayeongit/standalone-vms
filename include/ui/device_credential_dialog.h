#ifndef DEVICE_CREDENTIAL_DIALOG_H
#define DEVICE_CREDENTIAL_DIALOG_H

#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;

// Phase 3b: 채널 선택 후 Main 진입 전, 자격증명이 아직 캐시되지 않은 장치마다 띄우는 모달.
class DeviceCredentialDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DeviceCredentialDialog(const QString &deviceLabel, QWidget *parent = nullptr);

    QString username() const;
    QString password() const;

private:
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QPushButton *m_okButton = nullptr;
};

#endif // DEVICE_CREDENTIAL_DIALOG_H
