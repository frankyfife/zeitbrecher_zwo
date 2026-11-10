#include "theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QOperatingSystemVersion>
#include <QPainter>
#include <QPalette>
#include <QRadialGradient>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

namespace theme {

namespace {

bool g_forced_dark = false;

// Dunkle Palette für Fusion (wie im FuturamaPC-Launcher): Eingabefelder und
// Kästchen heller als das Fenster, damit leere Kästchen sichtbar bleiben.
QPalette dark_palette() {
    QPalette p;
    const QColor window(32, 32, 32);
    const QColor base(48, 48, 48);
    const QColor text(240, 240, 240);
    const QColor disabled(128, 128, 128);
    const QColor accent(76, 160, 224);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(40, 40, 40));
    p.setColor(QPalette::ToolTipBase, QColor(45, 45, 45));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, QColor(150, 150, 150));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(52, 52, 52));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::Accent, accent);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Light, QColor(96, 96, 96));
    p.setColor(QPalette::Midlight, QColor(72, 72, 72));
    p.setColor(QPalette::Mid, QColor(60, 60, 60));
    p.setColor(QPalette::Dark, QColor(16, 16, 16));
    p.setColor(QPalette::Shadow, Qt::black);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    return p;
}

}  // namespace

void apply_style(QApplication& app) {
    const QStringList keys = QStyleFactory::keys();
    auto has = [&](const char* name) { return keys.contains(QLatin1String(name), Qt::CaseInsensitive); };
    const bool platform_dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    // windows11 nur auf Windows 11 mit echtem Fenstersystem (offscreen fehlt das
    // native Thema); Windows 10 hell: windowsvista, sonst Fusion
    const bool offscreen = QGuiApplication::platformName() == QLatin1String("offscreen");
    const bool windows11 = QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11;
    QString style;
    if (!offscreen && windows11 && has("windows11")) style = QStringLiteral("windows11");
    else if (!offscreen && has("windowsvista") && !platform_dark) style = QStringLiteral("windowsvista");
    else style = QStringLiteral("Fusion");
    app.setStyle(QStyleFactory::create(style));
    if (style == QLatin1String("Fusion") && platform_dark) {
        // die Plattform liefert kein dunkles Schema: eigene Palette
        g_forced_dark = true;
        app.setPalette(dark_palette());
    }
}

bool is_dark() {
    if (g_forced_dark) return true;
    if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark) return true;
    // sonst: dunkler Fensterhintergrund
    return qApp != nullptr && QGuiApplication::palette().color(QPalette::Window).lightness() < 100;
}

QColor severity_color(Severity s) {
    const bool dark = is_dark();
    switch (s) {
        case Severity::Error: return dark ? QColor(255, 153, 164) : QColor(196, 43, 28);
        case Severity::Warning: return dark ? QColor(252, 225, 0) : QColor(157, 93, 0);
        case Severity::Ok: return dark ? QColor(108, 203, 95) : QColor(16, 124, 16);
    }
    return {};
}

QString colored(const QString& text, Severity s) {
    return QStringLiteral("<span style='color:%1'>%2</span>").arg(severity_color(s).name(QColor::HexRgb), text);
}

QColor muted_color(const QWidget* w) {
    const QPalette p = w ? w->palette() : QGuiApplication::palette();
    QColor c = p.color(QPalette::PlaceholderText);
    if (!c.isValid() || c == p.color(QPalette::WindowText)) c = p.color(QPalette::Disabled, QPalette::WindowText);
    return c;
}

void scale_font(QWidget* w, qreal factor, bool bold) {
    QFont f = w->font();
    if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * factor);
    else f.setPixelSize(qRound(f.pixelSize() * factor));
    f.setBold(bold);
    w->setFont(f);
}

// Symbol: Planet mit Zeitring, gezeichnet (keine fremden Grafiken)
QIcon app_icon() {
    static const QIcon icon = [] {
        QPixmap px(256, 256);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing);
        QRadialGradient pl(QPointF(100, 100), 150);
        pl.setColorAt(0, QColor(150, 255, 190));
        pl.setColorAt(0.6, QColor(40, 150, 120));
        pl.setColorAt(1, QColor(10, 40, 50));
        p.setPen(Qt::NoPen);
        p.setBrush(pl);
        p.drawEllipse(QPointF(128, 128), 82, 82);
        p.translate(128, 128);
        p.rotate(-20);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(57, 243, 255), 14));
        p.drawEllipse(QPointF(0, 0), 122, 34);
        p.setPen(QPen(QColor(255, 79, 216), 5));
        p.drawEllipse(QPointF(0, 0), 108, 26);
        p.end();
        return QIcon(px);
    }();
    return icon;
}

QPixmap app_icon_pixmap(int logical_size, qreal dpr) {
    return app_icon().pixmap(QSize(logical_size, logical_size), dpr);
}

}  // namespace theme
