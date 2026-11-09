#include "theme.h"

#include <QLinearGradient>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>
#include <QRandomGenerator>

#include <cmath>

namespace theme {

namespace {
const QColor kCyan(57, 243, 255);
const QColor kGreen(125, 255, 106);
const QColor kPink(255, 79, 216);
}  // namespace

QString stylesheet() {
    return QStringLiteral(R"(
* { font-family: "Bahnschrift", "Segoe UI"; font-size: 15px; color: #dff8ff; }
QWidget#page, QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }
QFrame#panel {
    background: rgba(10, 22, 52, 205);
    border: 2px solid rgba(57, 243, 255, 150);
    border-radius: 18px;
}
QLabel#heading { font-size: 22px; color: #7dff6a; font-weight: 600; }
QLabel#hint { color: #8fb3c9; font-size: 13px; }
QPushButton#nav {
    text-align: left; padding: 10px 18px; margin: 3px 0;
    border: 2px solid rgba(57, 243, 255, 90); border-radius: 22px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(40,70,120,200), stop:1 rgba(10,20,45,220));
    font-size: 17px;
}
QPushButton#nav:hover { border-color: #39f3ff; }
QPushButton#nav:checked {
    border-color: #7dff6a; color: #0b1430;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #b9ffa8, stop:0.5 #7dff6a, stop:1 #3fcf5a);
}
QPushButton#play {
    font-size: 26px; font-weight: 700; padding: 10px 40px; color: #0b1430;
    border: 3px solid #ffffff; border-radius: 28px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffd1f5, stop:0.45 #ff4fd8, stop:1 #a8188f);
}
QPushButton#play:hover { border-color: #39f3ff; }
QPushButton#play:disabled { background: #445; color: #889; border-color: #667; }
QPushButton, QToolButton {
    padding: 5px 14px; border: 2px solid rgba(57, 243, 255, 140); border-radius: 14px;
    background: rgba(20, 40, 80, 220);
}
QPushButton:hover { border-color: #39f3ff; }
QPushButton#keyslot { min-width: 110px; padding: 3px 8px; border-radius: 10px; }
QPushButton#keyslot[capturing="true"] { border-color: #ff4fd8; color: #ff4fd8; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
    background: rgba(5, 10, 30, 230); border: 1px solid rgba(57, 243, 255, 120);
    border-radius: 8px; padding: 3px 6px; selection-background-color: #39f3ff; selection-color: #05061a;
}
QCheckBox::indicator, QRadioButton::indicator { width: 18px; height: 18px; }
QSlider::groove:horizontal { height: 6px; background: rgba(57,243,255,80); border-radius: 3px; }
QSlider::handle:horizontal { width: 18px; margin: -7px 0; border-radius: 9px; background: #7dff6a; }
QScrollBar:vertical { width: 10px; background: transparent; }
QScrollBar::handle:vertical { background: rgba(57,243,255,120); border-radius: 5px; min-height: 30px; }
QToolTip { background: #0b1430; color: #dff8ff; border: 1px solid #39f3ff; }
)");
}

void paint_background(QPainter& p, const QRect& r) {
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(r.topLeft(), r.bottomLeft());
    sky.setColorAt(0, QColor(5, 6, 26));
    sky.setColorAt(0.6, QColor(14, 22, 60));
    sky.setColorAt(1, QColor(30, 14, 54));
    p.fillRect(r, sky);
    // Sterne, fest gesät
    QRandomGenerator rng(2002);
    for (int i = 0; i < 260; ++i) {
        const QPointF pt(rng.bounded(r.width()), rng.bounded(r.height()));
        const int a = 80 + rng.bounded(175);
        const double s = 0.6 + rng.generateDouble() * 1.6;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(220, 240, 255, a));
        p.drawEllipse(pt, s, s);
    }
    // Nebel
    QRadialGradient neb(QPointF(r.width() * 0.25, r.height() * 0.8), r.width() * 0.45);
    neb.setColorAt(0, QColor(255, 79, 216, 50));
    neb.setColorAt(1, QColor(255, 79, 216, 0));
    p.fillRect(r, neb);
    // Ringplanet unten rechts
    const QPointF c(r.width() * 0.86, r.height() * 0.86);
    const double rad = r.height() * 0.26;
    QRadialGradient pl(c - QPointF(rad * 0.4, rad * 0.4), rad * 1.3);
    pl.setColorAt(0, QColor(150, 255, 190));
    pl.setColorAt(0.5, QColor(40, 150, 120));
    pl.setColorAt(1, QColor(10, 40, 50));
    p.setBrush(pl);
    p.drawEllipse(c, rad, rad);
    p.save();
    p.translate(c);
    p.rotate(-18);
    p.setBrush(Qt::NoBrush);
    for (int i = 0; i < 3; ++i) {
        p.setPen(QPen(QColor(57, 243, 255, 140 - i * 35), 6 - i * 2));
        p.drawEllipse(QPointF(0, 0), rad * (1.6 + i * 0.12), rad * (0.36 + i * 0.03));
    }
    p.restore();
    // Röhre oben
    QLinearGradient tube(0, 0, 0, 96);
    tube.setColorAt(0, QColor(57, 243, 255, 0));
    tube.setColorAt(0.85, QColor(57, 243, 255, 40));
    tube.setColorAt(1, QColor(57, 243, 255, 160));
    p.fillRect(QRect(0, 0, r.width(), 96), tube);
}

void paint_title(QPainter& p, const QRect& r) {
    p.setRenderHint(QPainter::Antialiasing);
    QFont f("Bahnschrift", 34, QFont::Bold);
    f.setStretch(QFont::SemiExpanded);
    p.setFont(f);
    const QString title = QStringLiteral("TIMESPLITTERS 2");
    // Leuchten: mehrfach versetzt und durchscheinend
    for (int i = 6; i >= 1; --i) {
        p.setPen(QColor(57, 243, 255, 22));
        for (int dx = -i; dx <= i; dx += i)
            for (int dy = -i; dy <= i; dy += i) p.drawText(r.translated(dx, dy), Qt::AlignLeft | Qt::AlignTop, title);
    }
    QLinearGradient chrome(r.topLeft(), QPoint(r.left(), r.top() + 46));
    chrome.setColorAt(0, QColor(255, 255, 255));
    chrome.setColorAt(0.5, QColor(170, 220, 240));
    chrome.setColorAt(0.55, QColor(90, 140, 170));
    chrome.setColorAt(1, QColor(220, 250, 255));
    p.setPen(QPen(QBrush(chrome), 1));
    p.drawText(r, Qt::AlignLeft | Qt::AlignTop, title);
    QFont s("Bahnschrift", 13);
    s.setLetterSpacing(QFont::AbsoluteSpacing, 4);
    p.setFont(s);
    p.setPen(kGreen);
    p.drawText(r.adjusted(4, 52, 0, 0), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("ZEITBRECHER ZWO  ·  STARTRAMPE"));
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
