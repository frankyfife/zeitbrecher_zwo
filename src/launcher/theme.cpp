#include "theme.h"

#include <QPixmap>
#include <QRadialGradient>

#include <cmath>

namespace theme {

namespace {
const QColor kCyan(57, 243, 255);
const QColor kGreen(125, 255, 106);
const QColor kPink(255, 79, 216);
}  // namespace

QString stylesheet() {
    return QStringLiteral(R"(
* { font-family: "Bahnschrift", "Segoe UI"; font-size: 15px; color: #e6e6e6; }
QWidget#page, QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }
QFrame#panel { background: #0d0d0d; border: 1px solid #262626; border-radius: 6px; }
QLabel#heading { font-size: 20px; color: #7dff6a; font-weight: 600; letter-spacing: 1px; }
QLabel#hint { color: #8a8a8a; font-size: 13px; }
QTabWidget::pane { border: none; border-top: 1px solid #262626; }
QTabBar { background: transparent; }
QTabBar::tab {
    background: transparent; color: #9a9a9a; padding: 10px 22px; margin-right: 2px;
    font-size: 16px; letter-spacing: 2px; border: none; border-bottom: 3px solid transparent;
}
QTabBar::tab:hover { color: #ffffff; border-bottom-color: #3a3a3a; }
QTabBar::tab:selected { color: #ffffff; border-bottom-color: #7dff6a; }
QPushButton#play {
    font-size: 22px; font-weight: 700; padding: 10px 44px; color: #000000; letter-spacing: 3px;
    border: none; border-radius: 4px; background: #7dff6a;
}
QPushButton#play:hover { background: #a6ff96; }
QPushButton#play:disabled { background: #2a2a2a; color: #666666; }
QPushButton, QToolButton {
    padding: 5px 14px; border: 1px solid #3a3a3a; border-radius: 4px; background: #161616;
}
QPushButton:hover { border-color: #7dff6a; }
QPushButton#keyslot { min-width: 110px; padding: 3px 8px; }
QPushButton#keyslot[capturing="true"] { border-color: #39f3ff; color: #39f3ff; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
    background: #000000; border: 1px solid #333333; border-radius: 4px; padding: 3px 6px;
    selection-background-color: #7dff6a; selection-color: #000000;
}
QComboBox QAbstractItemView { background: #0d0d0d; border: 1px solid #333333; }
QCheckBox::indicator, QRadioButton::indicator { width: 18px; height: 18px; }
QSlider::groove:horizontal { height: 4px; background: #333333; border-radius: 2px; }
QSlider::handle:horizontal { width: 16px; margin: -6px 0; border-radius: 8px; background: #7dff6a; }
QScrollBar:vertical { width: 10px; background: transparent; }
QScrollBar::handle:vertical { background: #333333; border-radius: 5px; min-height: 30px; }
QToolTip { background: #0d0d0d; color: #e6e6e6; border: 1px solid #7dff6a; }
)");
}

void paint_background(QPainter& p, const QRect& r) { p.fillRect(r, Qt::black); }

// Titel: "Zeitbrecher Zwo" (deutscher Spaß auf den englischen Titel), darunter
// klein der Originaltitel.
void paint_title(QPainter& p, const QRect& r) {
    p.setRenderHint(QPainter::Antialiasing);
    QFont f("Bahnschrift", 30, QFont::Bold);
    f.setStretch(QFont::Expanded);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 3);
    p.setFont(f);
    const QString title = QStringLiteral("ZEITBRECHER ZWO");
    for (int i = 4; i >= 1; --i) {  // schwaches Leuchten
        p.setPen(QColor(125, 255, 106, 18));
        for (int dx = -i; dx <= i; dx += i)
            for (int dy = -i; dy <= i; dy += i) p.drawText(r.translated(dx, dy), Qt::AlignLeft | Qt::AlignTop, title);
    }
    p.setPen(QColor(240, 240, 240));
    p.drawText(r, Qt::AlignLeft | Qt::AlignTop, title);
    QFont s("Bahnschrift", 11);
    s.setLetterSpacing(QFont::AbsoluteSpacing, 4);
    p.setFont(s);
    p.setPen(kGreen);
    p.drawText(r.adjusted(4, 50, 0, 0), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("TIMESPLITTERS 2  ·  STARTRAMPE"));
}

QIcon app_icon() {
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
    p.setPen(QPen(kCyan, 14));
    p.drawEllipse(QPointF(0, 0), 122, 34);
    p.setPen(QPen(kPink, 5));
    p.drawEllipse(QPointF(0, 0), 108, 26);
    return QIcon(px);
}

}  // namespace theme
