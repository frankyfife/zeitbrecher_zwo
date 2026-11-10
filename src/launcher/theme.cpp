#include "theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QImage>
#include <QOperatingSystemVersion>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

#include <windows.h>

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

// Symbol: das Programmsymbol aus der eigenen EXE (res/launcher.rc,
// IDI_ICON1 = 1), jede Größe der .ico einzeln, damit Qt die passende wählt
QIcon app_icon() {
    static const QIcon icon = [] {
        QIcon ic;
        const HINSTANCE self = GetModuleHandleW(nullptr);
        for (int s : {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
            auto* h = static_cast<HICON>(LoadImageW(self, MAKEINTRESOURCEW(1), IMAGE_ICON, s, s, LR_DEFAULTCOLOR));
            if (!h) continue;
            const QImage img = QImage::fromHICON(h);
            DestroyIcon(h);
            if (!img.isNull()) ic.addPixmap(QPixmap::fromImage(img));
        }
        return ic;
    }();
    return icon;
}

QPixmap app_icon_pixmap(int logical_size, qreal dpr) {
    return app_icon().pixmap(QSize(logical_size, logical_size), dpr);
}

}  // namespace theme
