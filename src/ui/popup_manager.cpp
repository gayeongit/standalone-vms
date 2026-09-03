#include "popup_manager.h"
#include "common_ui.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

int PopupManager::execDialog(QWidget *parent,
                             const QString &title,
                             const QString &message,
                             bool withCancel,
                             const QString &okText,
                             const QString &cancelText)
{
    static QPointer<QDialog> s_activeDialog;
    if (s_activeDialog && s_activeDialog->isVisible()) {
        return QDialog::Rejected;
    }

    QWidget *dimOverlay = nullptr;
    if (parent) {
        dimOverlay = new QWidget(parent);
        dimOverlay->setObjectName("popupDimOverlay");
        dimOverlay->setGeometry(parent->rect());
        dimOverlay->show();
        dimOverlay->raise();
    }

    QDialog dialog(parent);
    s_activeDialog = &dialog;
    dialog.setModal(true);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(360);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(12);

    auto *titleLabel = new QLabel(title, &dialog);
    titleLabel->setObjectName("title");
    auto *messageLabel = new QLabel(message, &dialog);
    messageLabel->setWordWrap(true);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch();

    const QString resolvedOkText = okText.isEmpty() ? (withCancel ? "확인" : "닫기") : okText;
    const QString resolvedCancelText = cancelText.isEmpty() ? "취소" : cancelText;
    auto *okButton = new QPushButton(resolvedOkText, &dialog);
    okButton->setObjectName("ok");
    buttonRow->addWidget(okButton);

    QPushButton *cancelButton = nullptr;
    if (withCancel) {
        cancelButton = new QPushButton(resolvedCancelText, &dialog);
        buttonRow->addWidget(cancelButton);
        const int uniformWidth = std::max(okButton->sizeHint().width(), cancelButton->sizeHint().width());
        okButton->setMinimumWidth(uniformWidth);
        cancelButton->setMinimumWidth(uniformWidth);
    }

    layout->addWidget(titleLabel);
    layout->addWidget(messageLabel);
    layout->addLayout(buttonRow);

    dialog.adjustSize();
    const QSize targetSize = dialog.sizeHint().expandedTo(QSize(360, 0));
    dialog.resize(targetSize);
    dialog.setMinimumSize(targetSize);
    dialog.winId();
    applyNativeDarkTitleBar(&dialog);

    QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (cancelButton) {
        QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    }

    const int result = dialog.exec();
    s_activeDialog = nullptr;
    if (dimOverlay) {
        dimOverlay->deleteLater();
    }
    return result;
}

void PopupManager::showInfo(QWidget *parent, const QString &title, const QString &message)
{
    execDialog(parent, title, message, false);
}

bool PopupManager::confirm(QWidget *parent, const QString &title, const QString &message)
{
    return execDialog(parent, title, message, true) == QDialog::Accepted;
}

bool PopupManager::confirmWithLabels(QWidget *parent,
                                     const QString &title,
                                     const QString &message,
                                     const QString &okText,
                                     const QString &cancelText)
{
    return execDialog(parent, title, message, true, okText, cancelText) == QDialog::Accepted;
}
