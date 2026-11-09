// ts2launcher.exe: Startrampe für ts2.exe (Einstellungen in ts2.ini,
// Einrichten aus der eigenen Homefront-Installation, Spielen).
#include <QApplication>

#include "mainwindow.h"
#include "theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("TimeSplitters 2 Startrampe"));
    app.setStyleSheet(theme::stylesheet());
    MainWindow w;
    w.resize(1200, 780);
    w.show();
    return app.exec();
}
