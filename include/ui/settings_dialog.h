#ifndef SETTINGS_DIALOG_H
#define SETTINGS_DIALOG_H

#include <QDialog>

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    bool devicesChanged() const;

private:
    bool m_devicesChanged = false;
};

#endif // SETTINGS_DIALOG_H
