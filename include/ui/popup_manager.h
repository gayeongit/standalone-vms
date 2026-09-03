#ifndef POPUP_MANAGER_H
#define POPUP_MANAGER_H

#include <QString>

class QWidget;

class PopupManager
{
public:
    static void showInfo(QWidget *parent, const QString &title, const QString &message);
    static bool confirm(QWidget *parent, const QString &title, const QString &message);
    static bool confirmWithLabels(QWidget *parent,
                                  const QString &title,
                                  const QString &message,
                                  const QString &okText,
                                  const QString &cancelText);

private:
    static int execDialog(QWidget *parent,
                          const QString &title,
                          const QString &message,
                          bool withCancel,
                          const QString &okText = QString(),
                          const QString &cancelText = QString());
};

#endif // POPUP_MANAGER_H
