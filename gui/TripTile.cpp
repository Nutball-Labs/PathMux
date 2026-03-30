// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "TripTile.h"
#include "TripPropertiesDialog.h"
#include "format_helpers.hpp"
#include <QLabel>
#include <QPainter>
#include <QStyleOption>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <cmath>

using namespace Pathmux;

static QString gpsStatusText(const Trip& t)
{
    if (t.gpsTrackStatus == "complete") return "GPS \u2713";   // ✓
    if (t.gpsTrackStatus == "partial")  return "GPS \u007e";   // ~
    return "GPS \u2014";                                        // —
}

TripTile::TripTile(const Trip& trip, bool imperial, QWidget* parent)
    : QWidget(parent), m_trip(trip)
{
    setFixedSize(W, H);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("TripTile { border: 1px solid #c8c8c8; "
                  "border-radius: 4px; background: white; }");

    // --- Front thumbnail ---
    m_frontThumb = new QLabel(this);
    m_frontThumb->setGeometry(PADDING, PADDING, THUMB_W, THUMB_H);
    m_frontThumb->setScaledContents(true);
    m_frontThumb->setStyleSheet("background: #d8d8d8; border-radius: 2px;");

    // --- Rear thumbnail ---
    m_rearThumb = new QLabel(this);
    m_rearThumb->setGeometry(PADDING, PADDING + THUMB_H + THUMB_GAP, THUMB_W, THUMB_H);
    m_rearThumb->setScaledContents(true);
    m_rearThumb->setStyleSheet("background: #d8d8d8; border-radius: 2px;");

    // --- Start time (date + time, bold) ---
    m_startLabel = new QLabel(this);
    m_startLabel->setGeometry(RIGHT_X, PADDING + 6, RIGHT_W, 22);
    QFont boldFont = font();
    boldFont.setBold(true);
    boldFont.setPointSize(10);
    m_startLabel->setFont(boldFont);
    m_startLabel->setText(
        QString::fromStdString(trip.date + "  " + trip.startTime));

    // --- Duration ---
    m_durationLabel = new QLabel(this);
    m_durationLabel->setGeometry(RIGHT_X, PADDING + 32, RIGHT_W, 20);
    QFont durFont = font();
    durFont.setPointSize(10);
    m_durationLabel->setFont(durFont);
    m_durationLabel->setText(QString::fromStdString(trip.duration));

    // --- Detail row (segs · distance · GPS) ---
    m_detailLabel = new QLabel(this);
    m_detailLabel->setGeometry(RIGHT_X, H - PADDING - 22, RIGHT_W, 18);
    QFont detFont = font();
    detFont.setPointSize(8);
    m_detailLabel->setFont(detFont);
    m_detailLabel->setEnabled(false);   // renders in palette disabled colour

    int segs = (int)trip.segments.size();
    QString detail = QString("%1 seg%2").arg(segs).arg(segs == 1 ? "" : "s");

    if ((trip.startLat != 0.0 || trip.startLon != 0.0) &&
        (trip.endLat   != 0.0 || trip.endLon   != 0.0)) {
        double dist = haversineKm(trip.startLat, trip.startLon,
                                  trip.endLat,   trip.endLon);
        detail += "  \u00b7  " + QString::fromStdString(formatDistance(dist, imperial));
    }
    detail += "  \u00b7  " + gpsStatusText(trip);
    m_detailLabel->setText(detail);
}

void TripTile::setThumbnail(const QString& slot, const QPixmap& pixmap)
{
    if (slot == "front") m_frontThumb->setPixmap(pixmap);
    else if (slot == "rear") m_rearThumb->setPixmap(pixmap);
}

void TripTile::setTextZoom(double factor)
{
    QFont sf = font();
    sf.setPointSize(qMax(7, int(10 * factor)));
    sf.setBold(true);
    m_startLabel->setFont(sf);
    sf.setBold(false);
    m_durationLabel->setFont(sf);

    QFont df = font();
    df.setPointSize(qMax(6, int(8 * factor)));
    m_detailLabel->setFont(df);
}

void TripTile::mouseDoubleClickEvent(QMouseEvent*)
{
    emit doubleClicked();
}

void TripTile::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    QAction* propsAct = menu.addAction("Properties");
    if (menu.exec(event->globalPos()) == propsAct) {
        TripPropertiesDialog dlg(m_trip, this);
        dlg.exec();
    }
}

void TripTile::enterEvent(QEnterEvent*)
{
    setStyleSheet("TripTile { border: 1px solid #0078d4; "
                  "border-radius: 4px; background: #f5f9ff; }");
}

void TripTile::leaveEvent(QEvent*)
{
    setStyleSheet("TripTile { border: 1px solid #c8c8c8; "
                  "border-radius: 4px; background: white; }");
}

void TripTile::paintEvent(QPaintEvent*)
{
    // Required for QWidget stylesheet background painting
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    // Vertical divider between thumbnail column and detail column
    p.setPen(QPen(QColor(0xc8c8c8), 1));
    p.drawLine(DIV_X, PADDING + 4, DIV_X, H - PADDING - 4);
}
// SN: 00090
