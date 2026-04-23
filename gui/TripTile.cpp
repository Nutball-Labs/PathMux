// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "TripTile.h"
#include "TripPropertiesDialog.h"
#include "DangerousDialog.h"
#include "format_helpers.hpp"
#include "config_manager.hpp"
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QStyleOption>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMessageBox>
#include <cmath>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;
using namespace Pathmux;

static QString gpsStatusText(const Trip& t)
{
    if (t.gpsTrackStatus == "complete") return "GPS \u2713";   // ✓
    if (t.gpsTrackStatus == "partial")  return "GPS \u007e";   // ~
    return "GPS \u2014";                                        // —
}

TripTile::TripTile(const Trip& trip, bool imperial,
                   const std::string& manifestFile, QWidget* parent)
    : QWidget(parent), m_trip(trip), m_sourcePath(manifestFile)
{
    setFixedSize(W, H);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("TripTile { border: 1px solid #c8c8c8; "
                  "border-radius: 4px; background: white; }");

    // --- Front thumbnail ---
    m_frontThumb = new QLabel(this);
    m_frontThumb->setGeometry(PADDING, BADGE_H + PADDING, THUMB_W, THUMB_H);
    m_frontThumb->setScaledContents(true);
    m_frontThumb->setStyleSheet("background: #d8d8d8; border-radius: 2px;");

    // --- Rear thumbnail ---
    m_rearThumb = new QLabel(this);
    m_rearThumb->setGeometry(PADDING, BADGE_H + PADDING + THUMB_H + THUMB_GAP, THUMB_W, THUMB_H);
    m_rearThumb->setScaledContents(true);
    m_rearThumb->setStyleSheet("background: #d8d8d8; border-radius: 2px;");

    // --- Trip ID badge — full-width header bar at top ---
    m_idLabel = new QLabel(this);
    m_idLabel->setGeometry(0, 0, W, BADGE_H);
    m_idLabel->setAlignment(Qt::AlignCenter);
    {
        QFont idFont = font();
        idFont.setPointSize(10);
        idFont.setBold(true);
        m_idLabel->setFont(idFont);
    }
    m_idLabel->setStyleSheet(
        "background: #1a3a5c; color: white;"
        "border-top-left-radius: 4px; border-top-right-radius: 4px;");
    m_idLabel->setText(QString::fromStdString("[" + trip.id + "]"));

    // --- Start time (date + time, bold) ---
    m_startLabel = new QLabel(this);
    m_startLabel->setGeometry(RIGHT_X, BADGE_H + PADDING + 22, RIGHT_W, 22);
    QFont boldFont = font();
    boldFont.setBold(true);
    boldFont.setPointSize(10);
    m_startLabel->setFont(boldFont);
    m_startLabel->setText(
        QString::fromStdString(trip.date + "  " + trip.startTime));

    // --- Duration ---
    m_durationLabel = new QLabel(this);
    m_durationLabel->setGeometry(RIGHT_X, BADGE_H + PADDING + 48, RIGHT_W, 20);
    QFont durFont = font();
    durFont.setPointSize(10);
    m_durationLabel->setFont(durFont);
    m_durationLabel->setText(QString::fromStdString(trip.duration));

    // --- Note (hidden when empty) ---
    m_noteLabel = new QLabel(this);
    m_noteLabel->setGeometry(RIGHT_X, BADGE_H + PADDING + NOTE_Y, RIGHT_W, NOTE_H);
    {
        QFont nf = font();
        nf.setPointSize(8);
        nf.setItalic(true);
        m_noteLabel->setFont(nf);
    }
    m_noteLabel->setStyleSheet("color: #555;");
    m_noteLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    if (trip.note.empty()) {
        m_noteLabel->hide();
    } else {
        m_noteLabel->setText(QString::fromStdString(trip.note));
    }

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

    // --- Build Video button ---
    m_buildBtn = new QPushButton("Build Video\u2026", this);
    m_buildBtn->setGeometry(RIGHT_X, BUILD_BTN_Y, RIGHT_W, BUILD_BTN_H);
    m_buildBtn->setStyleSheet(
        "QPushButton { border: 1px solid #0078d4; border-radius: 3px; "
        "color: #0078d4; background: white; font-size: 9pt; }"
        "QPushButton:hover { background: #e5f1fb; }"
        "QPushButton:pressed { background: #cce4f7; }"
    );
    connect(m_buildBtn, &QPushButton::clicked, this, &TripTile::buildRequested);
}

void TripTile::setThumbnail(const QString& slot, const QPixmap& pixmap)
{
    if (slot == "front") m_frontThumb->setPixmap(pixmap);
    else if (slot == "rear") m_rearThumb->setPixmap(pixmap);
}

void TripTile::setZoom(double factor)
{
    m_zoomFactor = factor;

    int w        = int(W        * factor);
    int h        = int(H        * factor);
    int pad      = int(PADDING  * factor);
    int thumbW   = int(THUMB_W  * factor);
    int thumbH   = int(THUMB_H  * factor);
    int thumbGap = int(THUMB_GAP * factor);
    int divX     = pad + thumbW + pad;
    int rightX   = divX + pad;
    int rightW   = w - rightX - pad;

    int badgeH = int(BADGE_H * factor);

    setFixedSize(w, h);

    m_frontThumb->setGeometry(pad, badgeH + pad, thumbW, thumbH);
    m_rearThumb ->setGeometry(pad, badgeH + pad + thumbH + thumbGap, thumbW, thumbH);

    m_idLabel      ->setGeometry(0, 0, w, badgeH);
    m_startLabel   ->setGeometry(rightX, badgeH + pad + int(22 * factor), rightW, int(22 * factor));
    m_durationLabel->setGeometry(rightX, badgeH + pad + int(48 * factor), rightW, int(20 * factor));
    m_noteLabel    ->setGeometry(rightX, badgeH + pad + int(NOTE_Y * factor), rightW, int(NOTE_H * factor));
    m_detailLabel  ->setGeometry(rightX, h - pad - int(22 * factor), rightW, int(18 * factor));
    m_buildBtn     ->setGeometry(rightX, int(BUILD_BTN_Y * factor), rightW,
                                 int(BUILD_BTN_H * factor));

    QFont sf = font();
    sf.setPointSizeF(qMax(6.0, 10.0 * factor));
    sf.setBold(true);
    m_startLabel->setFont(sf);
    sf.setBold(false);
    m_durationLabel->setFont(sf);

    QFont idf = font();
    idf.setPointSizeF(qMax(6.0, 10.0 * factor));
    idf.setBold(true);
    m_idLabel->setFont(idf);

    QFont df = font();
    df.setPointSizeF(qMax(5.0, 8.0 * factor));
    m_detailLabel->setFont(df);

    QFont nf = font();
    nf.setPointSizeF(qMax(5.0, 8.0 * factor));
    nf.setItalic(true);
    m_noteLabel->setFont(nf);

    update();
}

void TripTile::mouseDoubleClickEvent(QMouseEvent*)
{
    emit doubleClicked();
}

void TripTile::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    QAction* propsAct       = menu.addAction("Properties");
    QAction* dangerousAct   = menu.addAction("Dangerous\u2026");
    menu.addSeparator();
    QAction* archiveTripAct = menu.addAction("Archive Trip\u2026");
    QAction* deleteTripAct  = menu.addAction("Delete Trip\u2026");

    QAction* chosen = menu.exec(event->globalPos());
    if (!chosen) return;

    if (chosen == propsAct) {
        auto* dlg = new TripPropertiesDialog(m_trip, m_sourcePath, nullptr);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &TripPropertiesDialog::accepted, this, [this, dlg]() {
            m_trip.note = dlg->updatedNote();
            if (m_trip.note.empty()) {
                m_noteLabel->hide();
            } else {
                m_noteLabel->setText(QString::fromStdString(m_trip.note));
                m_noteLabel->show();
            }
        });
        connect(dlg, &TripPropertiesDialog::gpsExtracted, this, [this]() {
            m_trip.gpsTrackStatus = "complete";
        });
        dlg->show();

    } else if (chosen == dangerousAct) {
        auto* dlg = new DangerousDialog(m_trip, m_sourcePath, nullptr);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &DangerousDialog::manifestChanged, this, &TripTile::tripChanged);
        dlg->show();

    } else if (chosen == archiveTripAct) {
        archiveWholeTrip();

    } else if (chosen == deleteTripAct) {
        deleteWholeTrip();
    }
}

// ---------------------------------------------------------------------------
// Whole-trip archive / delete
// ---------------------------------------------------------------------------

std::string TripTile::archiveDestPath(const std::string& absPath) const
{
    if (absPath.size() <= m_sourcePath.size()) return "";
    if (absPath.substr(0, m_sourcePath.size()) != m_sourcePath) return "";
    std::string rel = absPath.substr(m_sourcePath.size());
    if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
    return m_sourcePath + "/Archive/" + rel;
}

void TripTile::archiveWholeTrip()
{
    int n = (int)m_trip.segments.size();
    auto reply = QMessageBox::question(
        this, "Archive Trip",
        QString("Move all %1 segment(s) of trip [%2] to the Archive directory?\n\n"
                "Archived trips are excluded from future scans.")
            .arg(n).arg(QString::fromStdString(m_trip.id)),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (reply != QMessageBox::Yes) return;

    std::vector<std::string> errors;
    for (const auto& seg : m_trip.segments) {
        for (const auto& [slot, path] : seg.cameras) {
            if (path.empty() || path == "-") continue;
            std::error_code ec;
            if (!fs::exists(path, ec)) continue;
            std::string dest = archiveDestPath(path);
            if (dest.empty()) continue;
            try {
                fs::create_directories(fs::path(dest).parent_path());
                fs::rename(path, dest);
            } catch (const std::exception& e) { errors.push_back(e.what()); }
        }
        for (const auto& [slot, path] : seg.thumbs) {
            if (path.empty()) continue;
            std::error_code ec;
            if (!fs::exists(path, ec)) continue;
            std::string dest = archiveDestPath(path);
            if (dest.empty()) continue;
            try {
                fs::create_directories(fs::path(dest).parent_path());
                fs::rename(path, dest);
            } catch (const std::exception& e) { errors.push_back(e.what()); }
        }
    }

    if (!errors.empty())
        QMessageBox::warning(this, "Archive Errors",
            QString("Some files could not be archived:\n\u2022 %1")
                .arg(QString::fromStdString(errors.front())));

    ConfigManager cfg;
    cfg.loadSettings();
    auto trips = cfg.loadTripCache(m_sourcePath);
    trips.erase(std::remove_if(trips.begin(), trips.end(),
        [this](const Trip& t) { return t.id == m_trip.id; }), trips.end());
    cfg.saveTripCache(m_sourcePath, trips);
    emit tripChanged();
}

void TripTile::deleteWholeTrip()
{
    int n = (int)m_trip.segments.size();
    auto reply = QMessageBox::warning(
        this, "Delete Trip",
        QString("Permanently delete all %1 segment(s) of trip [%2]?\n\n"
                "All video files and thumbnails will be removed.\n"
                "This cannot be undone.")
            .arg(n).arg(QString::fromStdString(m_trip.id)),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (reply != QMessageBox::Yes) return;

    for (const auto& seg : m_trip.segments) {
        for (const auto& [slot, path] : seg.cameras) {
            if (!path.empty() && path != "-") {
                try { fs::remove(path); } catch (...) {}
            }
        }
        for (const auto& [slot, path] : seg.thumbs) {
            if (!path.empty()) {
                try { fs::remove(path); } catch (...) {}
            }
        }
    }

    ConfigManager cfg;
    cfg.loadSettings();
    auto trips = cfg.loadTripCache(m_sourcePath);
    trips.erase(std::remove_if(trips.begin(), trips.end(),
        [this](const Trip& t) { return t.id == m_trip.id; }), trips.end());
    cfg.saveTripCache(m_sourcePath, trips);
    emit tripChanged();
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

    // Vertical divider — computed from current (possibly zoomed) size
    int pad    = int(PADDING * m_zoomFactor);
    int badgeH = int(BADGE_H * m_zoomFactor);
    int divX   = int(DIV_X  * m_zoomFactor);
    p.setPen(QPen(QColor(0xc8c8c8), 1));
    p.drawLine(divX, badgeH + pad + 4, divX, height() - pad - 4);
}
// SN: 00098
