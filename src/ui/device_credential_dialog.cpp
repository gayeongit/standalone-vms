#include "device_credential_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

DeviceCredentialDialog::DeviceCredentialDialog(const QString &deviceLabel, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("장치 인증"));
    setFixedWidth(320);

    auto *layout = new QVBoxLayout(this);

    auto *infoLabel = new QLabel(
        QStringLiteral("\"%1\"에 연결하려면 카메라 계정 정보가 필요합니다.").arg(deviceLabel), this);
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText(QStringLiteral("아이디"));
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setPlaceholderText(QStringLiteral("비밀번호"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    auto *form = new QFormLayout();
    form->addRow(QStringLiteral("아이디"), m_usernameEdit);
    form->addRow(QStringLiteral("비밀번호"), m_passwordEdit);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &QDialog::accept);
    m_usernameEdit->setFocus();
}

QString DeviceCredentialDialog::username() const
{
    return m_usernameEdit ? m_usernameEdit->text().trimmed() : QString();
}

QString DeviceCredentialDialog::password() const
{
    return m_passwordEdit ? m_passwordEdit->text() : QString();
}
