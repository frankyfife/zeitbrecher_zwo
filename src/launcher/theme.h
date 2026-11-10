// Aussehen des Launchers wie der FuturamaPC-Launcher: Qt-Stil "windows11"
// (folgt dem hellen oder dunklen Schema von Windows 11), sonst Fusion mit
// dunkler Palette. Kein eigenes Stylesheet; Hinweise gedämpft, Statustexte in
// Farben, die auf beiden Schemata lesbar sind. Das Symbol wird hier gezeichnet.
#pragma once
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QApplication;
class QWidget;

namespace theme {

enum class Severity { Ok, Warning, Error };

// Stil und Palette für die ganze Anwendung (vor dem ersten Fenster)
void apply_style(QApplication& app);
bool is_dark();

QColor severity_color(Severity s);
// Text in der Farbe des Schweregrads (für Rich-Text-Labels)
QString colored(const QString& text, Severity s);
// Gedämpfte Schrift für Hinweise (Platzhalterfarbe der Palette)
QColor muted_color(const QWidget* w);

// Schrift relativ zur Widget-Schrift (keine festen Pixelgrößen: hohe DPI)
void scale_font(QWidget* w, qreal factor, bool bold = false);

QIcon app_icon();
QPixmap app_icon_pixmap(int logical_size, qreal dpr);

}  // namespace theme
