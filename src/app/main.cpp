#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv); // QApplication 생성

    // GStreamer가 설치되지 않은 환경에서도 스트리밍 동작하도록
    // exe 옆 gstreamer-1.0 폴더를 플러그인 경로로 자동 지정
    // (GStreamer는 lazy init이므로 QApplication 생성 후 설정해도 됨)
    const QString gstPluginDir = QCoreApplication::applicationDirPath() + "/gstreamer-1.0";
    if (QDir(gstPluginDir).exists()) {
        qputenv("GST_PLUGIN_PATH", gstPluginDir.toUtf8());
        qputenv("GST_PLUGIN_SYSTEM_PATH", gstPluginDir.toUtf8());
    }

    a.setWindowIcon(QIcon(":/styles/clue_logomark.svg"));   // 전역 앱 아이콘 설정
    // MainWindow 생성 및 표시
    MainWindow w;
    w.show();
    return a.exec();
}
