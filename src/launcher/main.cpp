// ts2launcher.exe: Startrampe für ts2.exe (Einstellungen in ts2.ini,
// Einrichten aus der eigenen Homefront-Installation, Spielen).
#include <QApplication>

#include "mainwindow.h"
#include "theme.h"
#include "xboxtext.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Zeitbrecher Zwo"));
    theme::apply_style(app);
    app.setWindowIcon(theme::app_icon());
    // --texte-holen <image> <ziel>: nur die Spieltexte holen (für Tests)
    const QStringList pre = app.arguments();
    const int t = int(pre.indexOf(QStringLiteral("--texte-holen")));
    if (t >= 0 && t + 2 < pre.size())
        return xboxtext::extract(pre[t + 1].toStdWString(), pre[t + 2].toStdWString()).error.empty() ? 0 : 1;
    MainWindow w;
    w.resize(1040, 820);
    // --seite N: mit Reiter N öffnen (0 Spiel, 1 Freischalten, 2 Grafik, 3 Steuerung, 4 Waffen, 5 Ton, 6 Über; für Tests)
    const QStringList args = app.arguments();
    const int i = int(args.indexOf(QStringLiteral("--seite")));
    if (i >= 0 && i + 1 < args.size()) w.show_page(args[i + 1].toInt());
    w.show();
    return app.exec();
}
