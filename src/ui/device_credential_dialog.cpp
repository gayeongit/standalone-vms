#include "device_credential_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setEnabled(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // 아이디/비밀번호 둘 다 비어 있으면 확인을 눌러도 통과되지 않게 막는다 — 이전에는 빈 값으로도
    // 그냥 accept되어 캐시에 빈 자격증명이 들어갈 수 있었다.
    auto updateOkEnabled = [this]() {
        if (!m_okButton) {
            return;
        }
        const bool hasUsername = m_usernameEdit && !m_usernameEdit->text().trimmed().isEmpty();
        const bool hasPassword = m_passwordEdit && !m_passwordEdit->text().isEmpty();
        m_okButton->setEnabled(hasUsername && hasPassword);
    };
    connect(m_usernameEdit, &QLineEdit::textChanged, this, updateOkEnabled);
    connect(m_passwordEdit, &QLineEdit::textChanged, this, updateOkEnabled);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, [this]() {
        if (m_okButton && m_okButton->isEnabled()) {
            accept();
        }
    });
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
