// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "TripTile.h"
#include "TripPropertiesDialog.h"
#include "ExtrasDialog.h"
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
using namespace CamClops;

TripTile::TripTile(const Trip& trip, bool imperial,
                   const std::string& manifestFile,
                   const std::string& mid, QWidget* parent)
    : QWidget(parent), m_trip(trip), m_sourcePath(manifestFile), m_mid(mid)
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
    updateTitleBar();

    // --- Start time (date + time, bold) ---
    m_startLabel = new QLabel(this);
    m_startLabel->setGeometry(RIGHT_X, BADGE_H + PADDING + 22, RIGHT_W, 22);
    QFont boldFont = font();
    boldFont.setBold(true);
    boldFont.setPointSize(10);
    m_startLabel->setFont(boldFont);
    m_startLabel->setStyleSheet("color: #1a1a1a;");
    m_startLabel->setText(
        QString::fromStdString(trip.date + "  " + trip.startTime));

    // --- Duration ---
    m_durationLabel = new QLabel(this);
    m_durationLabel->setGeometry(RIGHT_X, BADGE_H + PADDING + 48, RIGHT_W, 20);
    QFont durFont = font();
    durFont.setPointSize(10);
    m_durationLabel->setFont(durFont);
    m_durationLabel->setStyleSheet("color: #333;");
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

    // --- Detail row (segs · distance) ---
    m_detailLabel = new QLabel(this);
    m_detailLabel->setGeometry(RIGHT_X, DETAIL_LABEL_Y, RIGHT_W, 18);
    QFont detFont = font();
    detFont.setPointSize(8);
    m_detailLabel->setFont(detFont);
    m_detailLabel->setStyleSheet("color: #888;");

    int segs = (int)trip.segments.size();
    QString detail = QString("%1 seg%2").arg(segs).arg(segs == 1 ? "" : "s");
    if ((trip.startLat != 0.0 || trip.startLon != 0.0) &&
        (trip.endLat   != 0.0 || trip.endLon   != 0.0)) {
        double dist = haversineKm(trip.startLat, trip.startLon,
                                  trip.endLat,   trip.endLon);
        detail += "  ·  " + QString::fromStdString(formatDistance(dist, imperial));
    }
    m_detailLabel->setText(detail);

    // --- Build Video button ---
    m_buildBtn = new QPushButton("Build Video…", this);
    m_buildBtn->setGeometry(RIGHT_X, BUILD_BTN_Y, RIGHT_W, BUILD_BTN_H);
    m_buildBtn->setStyleSheet(
        "QPushButton { border: 1px solid #0078d4; border-radius: 3px; "
        "color: #0078d4; background: white; font-size: 9pt; }"
        "QPushButton:hover { background: #e5f1fb; }"
        "QPushButton:pressed { background: #cce4f7; }");
    connect(m_buildBtn, &QPushButton::clicked, this, &TripTile::buildRequested);

    // --- Extras button — opens lightweight job-queue dialog ---
    m_extrasBtn = new QPushButton("Extras…", this);
    m_extrasBtn->setGeometry(RIGHT_X, EXTRAS_BTN_Y, RIGHT_W, BUILD_BTN_H);
    m_extrasBtn->setStyleSheet(
        "QPushButton { border: 1px solid #aaaaaa; border-radius: 3px; "
        "color: #555555; background: white; font-size: 9pt; }"
        "QPushButton:hover { background: #f0f0f0; }"
        "QPushButton:pressed { background: #e0e0e0; }");
    connect(m_extrasBtn, &QPushButton::clicked, this, &TripTile::onExtrasClicked);
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
    int badgeH   = int(BADGE_H * factor);

    setFixedSize(w, h);

    m_frontThumb->setGeometry(pad, badgeH + pad, thumbW, thumbH);
    m_rearThumb ->setGeometry(pad, badgeH + pad + thumbH + thumbGap, thumbW, thumbH);

    m_idLabel      ->setGeometry(0, 0, w, badgeH);
    m_startLabel   ->setGeometry(rightX, badgeH + pad + int(22 * factor), rightW, int(22 * factor));
    m_durationLabel->setGeometry(rightX, badgeH + pad + int(48 * factor), rightW, int(20 * factor));
    m_noteLabel    ->setGeometry(rightX, badgeH + pad + int(NOTE_Y * factor), rightW, int(NOTE_H * factor));
    m_detailLabel  ->setGeometry(rightX, int(DETAIL_LABEL_Y * factor), rightW, int(18 * factor));
    m_buildBtn     ->setGeometry(rightX, int(BUILD_BTN_Y    * factor), rightW, int(BUILD_BTN_H * factor));

    if (m_extrasBtn)
        m_extrasBtn->setGeometry(rightX, int(EXTRAS_BTN_Y * factor), rightW, int(BUILD_BTN_H * factor));

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

void TripTile::onExtrasClicked()
{
    auto* dlg = new ExtrasDialog(m_trip, m_sourcePath, m_mid, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();
}

void TripTile::mouseDoubleClickEvent(QMouseEvent*)
{
    emit doubleClicked();
}

void TripTile::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    QAction* propsAct       = menu.addAction("Properties");
    menu.addSeparator();
    QAction* archiveTripAct = menu.addAction("Archive Trip…");
    QAction* deleteTripAct  = menu.addAction("Delete Trip…");

    QAction* chosen = menu.exec(event->globalPos());
    if (!chosen) return;

    if (chosen == propsAct) {
        auto* dlg = new TripPropertiesDialog(m_trip, m_sourcePath, m_mid, nullptr);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &TripPropertiesDialog::accepted, this, [this, dlg]() {
            m_trip.note = dlg->updatedNote();
            updateTitleBar();
            if (m_trip.note.empty()) {
                m_noteLabel->hide();
            } else {
                m_noteLabel->setText(QString::fromStdString(m_trip.note));
                m_noteLabel->show();
            }
        });
        connect(dlg, &TripPropertiesDialog::gpsExtracted, this, [this]() {
            m_trip.gpsTrackStatus = "complete";
            update();
        });
        connect(dlg, &TripPropertiesDialog::videosChanged,
                this, &TripTile::tripChanged);
        connect(dlg, &TripPropertiesDialog::syncAnalyzed, this, [this]() {
            m_trip.cameraSync.valid = true;
            update();
        });
        connect(dlg, &TripPropertiesDialog::tripDeleted, this, &TripTile::tripChanged);
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
            QString("Some files could not be archived:\n• %1")
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

void TripTile::refreshFrom(const Trip& trip)
{
    m_trip = trip;
    updateTitleBar();
    if (m_trip.note.empty()) {
        m_noteLabel->hide();
    } else {
        m_noteLabel->setText(QString::fromStdString(m_trip.note));
        m_noteLabel->show();
    }
    update();
}

void TripTile::updateTitleBar()
{
    QString mid  = QString::fromStdString(m_mid).toUpper();
    QString tid  = QString::fromStdString(m_trip.id).toUpper();
    QString text = mid.isEmpty() ? "[" + tid + "]"
                                 : "[" + mid + ":" + tid + "]";
    if (!m_trip.note.empty())
        text += "  —  " + QString::fromStdString(m_trip.note);
    m_idLabel->setText(text);
}

void TripTile::drawStatusIndicators(QPainter& p) const
{
    struct Ind { const char* label; QColor color; bool drawCross = false; };

    static const QColor kGreen (0x28, 0xa7, 0x45);
    static const QColor kRed   (0xdc, 0x35, 0x45);
    static const QColor kAmber (0xfd, 0x7e, 0x14);

    auto anyExists = [](const std::vector<std::string>& paths) -> bool {
        std::error_code ec;
        for (const auto& p : paths)
            if (!p.empty() && fs::exists(p, ec)) return true;
        return false;
    };

    bool gpsUnavailable = (m_trip.gpsTrackStatus == "unavailable");
    QColor gpsColor;
    if      (gpsUnavailable)                         gpsColor = kRed;
    else if (m_trip.gpsTrackStatus == "complete")    gpsColor = kGreen;
    else if (m_trip.gpsTrackStatus == "partial")     gpsColor = kAmber;
    else                                             gpsColor = kRed;

    const Ind indicators[] = {
        { "GPS",  gpsColor, gpsUnavailable },
        { "SYNC", m_trip.cameraSync.valid      ? kGreen : kRed },
        { "MAP",  anyExists(m_trip.mapVideos)  ? kGreen : kRed },
        { "DASH", anyExists(m_trip.dashVideos) ? kGreen : kRed },
        { "HUD",  anyExists(m_trip.hudVideos)  ? kGreen : kRed },
    };

    double z       = m_zoomFactor;
    int    rightX  = int(RIGHT_X   * z);
    int    rightW  = width() - rightX - int(PADDING * z);
    int    r       = int(7    * z);
    int    spacing = int(28   * z);
    int    totalW  = 4 * spacing + 2 * r;
    int    startX  = rightX + (rightW - totalW) / 2 + r;
    int    cy      = int((BUILD_BTN_Y + BUILD_BTN_H + 10) * z) + r;
    int    labelY  = cy + r + int(2 * z);

    QFont lf = font();
    lf.setPointSizeF(qMax(5.0, 6.5 * z));
    p.setFont(lf);
    p.setRenderHint(QPainter::Antialiasing, true);

    for (int i = 0; i < 5; ++i) {
        int cx = startX + i * spacing;

        if (indicators[i].drawCross) {
            int d = int(r * 0.75);
            p.setPen(QPen(indicators[i].color, qMax(1.5, 2.0 * z), Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawLine(cx - d, cy - d, cx + d, cy + d);
            p.drawLine(cx - d, cy + d, cx + d, cy - d);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(indicators[i].color);
            p.drawEllipse(QPoint(cx, cy), r, r);
        }

        p.setPen(QColor(0x33, 0x33, 0x33));
        QRect tr(cx - spacing / 2, labelY, spacing, int(10 * z));
        p.drawText(tr, Qt::AlignHCenter | Qt::AlignTop,
                   QString::fromUtf8(indicators[i].label));
    }
}

void TripTile::paintEvent(QPaintEvent*)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    int pad    = int(PADDING * m_zoomFactor);
    int badgeH = int(BADGE_H * m_zoomFactor);
    int divX   = int(DIV_X  * m_zoomFactor);
    p.setPen(QPen(QColor(0xc8c8c8), 1));
    p.drawLine(divX, badgeH + pad + 4, divX, height() - pad - 4);

    drawStatusIndicators(p);
}
// SN: 00119
