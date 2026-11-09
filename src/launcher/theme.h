// Aussehen des Launchers: Retro-Futurismus (Sternenhimmel, Ringplanet,
// Neonröhren, Chrom). Alles hier gezeichnet, keine fremden Grafiken.
#pragma once
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QString>

namespace theme {

QString stylesheet();
void paint_background(QPainter& p, const QRect& r);
void paint_title(QPainter& p, const QRect& r);
QIcon app_icon();

}  // namespace theme
