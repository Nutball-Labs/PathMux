// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "TripPropertiesDialog.h"
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QString>
#include <vector>
#include <string>
#include <algorithm>

using namespace Pathmux;

// ---------------------------------------------------------------------------
// Humanize a byte count: B / KB / MB / GB
// ---------------------------------------------------------------------------
static QString humanizeSize(qint64 bytes)
{
    if (bytes < 0)               return "?";
    if (bytes < 1024)            return QString("%1 B").arg(bytes);
    if (bytes < 1024LL * 1024)   return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
                                 return QString("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 1);
    return                              QString("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
}

// ---------------------------------------------------------------------------
// Build a read-only value label for the form layout
// ---------------------------------------------------------------------------
static QLabel* valueLabel(const QString& text, QWidget* parent)
{
    auto* lbl = new QLabel(text, parent);
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return lbl;
}

// ---------------------------------------------------------------------------
// General tab
// ---------------------------------------------------------------------------
static QWidget* makeGeneralTab(const Trip& t, QWidget* parent)
{
    auto* w      = new QWidget(parent);
    auto* form   = new QFormLayout(w);
    form->setContentsMargins(12, 12, 12, 12);
    form->setVerticalSpacing(6);
    form->setHorizontalSpacing(16);

    form->addRow("Trip ID:",   valueLabel(QString::fromStdString(t.id), w));
    form->addRow("Date:",      valueLabel(QString::fromStdString(t.date), w));
    form->addRow("Start:",     valueLabel(QString::fromStdString(t.startTime), w));
    form->addRow("Duration:",  valueLabel(QString::fromStdString(t.duration), w));
    form->addRow("Segments:",  valueLabel(QString::number(t.segments.size()), w));

    // GPS status
    QString gpsText = QString::fromStdString(t.gpsTrackStatus);
    if (gpsText.isEmpty()) gpsText = "none";
    form->addRow("GPS Status:", valueLabel(gpsText, w));

    // GPS lock time
    QString lockText;
    if (t.gpsLockSeconds < 0)
        lockText = "Not scanned";
    else if (t.gpsLockSeconds == 0)
        lockText = "Immediate";
    else
        lockText = QString("%1 s").arg(t.gpsLockSeconds);
    form->addRow("GPS Lock:", valueLabel(lockText, w));

    // Start / end coordinates
    auto fmtCoord = [](double lat, double lon) -> QString {
        if (lat == 0.0 && lon == 0.0) return "\u2014";   // —
        return QString("%1\u00b0, %2\u00b0").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6);
    };
    form->addRow("Start Coords:", valueLabel(fmtCoord(t.startLat, t.startLon), w));
    form->addRow("End Coords:",   valueLabel(fmtCoord(t.endLat,   t.endLon),   w));

    // Video profile
    QString vp = QString("%1\u00d7%2  %3  %4 fps")
        .arg(t.videoProfile.width)
        .arg(t.videoProfile.height)
        .arg(QString::fromStdString(t.videoProfile.pixFmt))
        .arg(QString::fromStdString(t.videoProfile.frameRate));
    form->addRow("Video:", valueLabel(vp, w));

    // Note (only if non-empty)
    if (!t.note.empty()) {
        auto* noteLbl = valueLabel(QString::fromStdString(t.note), w);
        noteLbl->setWordWrap(true);
        form->addRow("Note:", noteLbl);
    }

    return w;
}

// ---------------------------------------------------------------------------
// Per-camera tab: table of segments (path | size)
// ---------------------------------------------------------------------------
static QWidget* makeCameraTab(const Trip& t, const std::string& slot, QWidget* parent)
{
    auto* w     = new QWidget(parent);
    auto* vbox  = new QVBoxLayout(w);
    vbox->setContentsMargins(0, 0, 0, 0);

    auto* table = new QTableWidget(0, 2, w);
    table->setHorizontalHeaderLabels({"Path", "Size"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table->setColumnWidth(1, 80);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);

    for (const auto& seg : t.segments) {
        auto it = seg.cameras.find(slot);
        QString path = (it != seg.cameras.end() && !it->second.empty() && it->second != "-")
                       ? QString::fromStdString(it->second)
                       : QString();

        int row = table->rowCount();
        table->insertRow(row);

        if (path.isEmpty()) {
            auto* pathItem = new QTableWidgetItem("\u2014");   // —
            pathItem->setForeground(Qt::gray);
            auto* sizeItem = new QTableWidgetItem("\u2014");
            sizeItem->setForeground(Qt::gray);
            sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(row, 0, pathItem);
            table->setItem(row, 1, sizeItem);
        } else {
            QFileInfo fi(path);
            QString sizeStr = fi.exists() ? humanizeSize(fi.size()) : "?";
            auto* pathItem = new QTableWidgetItem(path);
            auto* sizeItem = new QTableWidgetItem(sizeStr);
            sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(row, 0, pathItem);
            table->setItem(row, 1, sizeItem);
        }
    }

    // Double-click a row to open the file in the system default viewer.
    QObject::connect(table, &QTableWidget::itemDoubleClicked,
                     [table](QTableWidgetItem* item) {
        if (!item) return;
        QString path = table->item(item->row(), 0)->text();
        if (path == "\u2014") return;   // skip absent-segment rows
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    vbox->addWidget(table);
    return w;
}

// ---------------------------------------------------------------------------
// TripPropertiesDialog
// ---------------------------------------------------------------------------
TripPropertiesDialog::TripPropertiesDialog(const Trip& trip, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString("Trip Properties \u2014 %1  %2")
        .arg(QString::fromStdString(trip.date))
        .arg(QString::fromStdString(trip.startTime)));
    resize(620, 420);

    auto* vbox = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);

    tabs->addTab(makeGeneralTab(trip, tabs), "General");

    // Collect unique camera slot names; sort for deterministic tab order.
    // Note: "slots" is a Qt macro (expands to empty) — use cameraSlots instead.
    std::vector<std::string> cameraSlots;
    for (const auto& seg : trip.segments) {
        for (const auto& kv : seg.cameras) {
            const std::string& k = kv.first;
            if (std::find(cameraSlots.begin(), cameraSlots.end(), k) == cameraSlots.end())
                cameraSlots.push_back(k);
        }
    }
    std::sort(cameraSlots.begin(), cameraSlots.end());

    for (const auto& camSlot : cameraSlots) {
        // Only add a tab if at least one segment has a real path for this camera
        bool hasAny = false;
        for (const auto& seg : trip.segments) {
            auto it = seg.cameras.find(camSlot);
            if (it != seg.cameras.end() && !it->second.empty() && it->second != "-") {
                hasAny = true;
                break;
            }
        }
        if (!hasAny) continue;

        QString tabName = QString::fromStdString(camSlot);
        tabName[0] = tabName[0].toUpper();   // "front" -> "Front"
        tabs->addTab(makeCameraTab(trip, camSlot, tabs), tabName);
    }

    vbox->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    vbox->addWidget(buttons);
}
// SN: 00092
