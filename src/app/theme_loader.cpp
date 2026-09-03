#include "theme_loader.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QDebug>
#include <QStringList>

void loadThemeFromRelativePaths()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/styles/v2_theme.qss",
        appDir + "/../styles/v2_theme.qss",
        appDir + "/../../styles/v2_theme.qss"
    };

    for (const QString &path : candidates) {
        QFile file(path);
        if (!file.exists()) {
            continue;
        }
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        QByteArray raw = file.readAll();
        if (raw.startsWith("\xEF\xBB\xBF")) {
            raw = raw.mid(3);
        }
        qApp->setStyleSheet(QString::fromUtf8(raw));
        return;
    }

    qWarning() << "Failed to load theme file. Tried paths:" << candidates;
}

void loadMergedThemeFromRelativePaths()
{
    loadThemeFromRelativePaths();
}
