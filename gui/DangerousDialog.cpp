// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "DangerousDialog.h"
#include "config_manager.hpp"
#include "compat.hpp"
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <filesystem>
#include <set>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace fs = std::filesystem;
using namespace CamClops;

// Parse a segment timestamp string "YYYYMMDD_HHMMSS" to time_t (local time).
// Returns 0 on parse failure.
static time_t parseSegTimestamp(const std::string& ts)
{
    std::tm t = {};
    std::istringstream ss(ts);
    ss >> std::get_time(&t, "%Y%m%d_%H%M%S");
    if (ss.fail()) return 0;
    t.tm_isdst = -1;
    return std::mktime(&t);
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

DangerousDialog::DangerousDialog(const Trip& trip,
                                 const std::string& sourcePath,
                                 QWidget* parent)
    : QDialog(parent), m_trip(trip), m_sourcePath(sourcePath)
{
    setWindowTitle(QString("Dangerous Operations \u2014 Trip [%1]")
                   .arg(QString::fromStdString(trip.id)));
    setMinimumSize(860, 480);
    resize(940, 560);

    // --- Determine active camera slots (canonical order, only those with data) ---
    static const std::vector<std::pair<std::string, std::string>> canonical = {
        {"front", "Front"}, {"rear", "Rear"}, {"left", "Left"}, {"right", "Right"}
    };
    for (const auto& [slot, display] : canonical) {
        for (const auto& seg : trip.segments) {
            auto it = seg.cameras.find(slot);
            if (it != seg.cameras.end() && !it->second.empty() && it->second != "-") {
                m_slots.push_back(slot);
                break;
            }
        }
    }

    // --- Info label ---
    auto* infoLabel = new QLabel(
        QString("Archive moves files to <b>%1/Archive/</b> and excludes them from "
                "future scans.  Permanently Delete cannot be undone.")
            .arg(QString::fromStdString(sourcePath)), this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: #555; font-size: 9pt; padding: 4px 0;");

    // --- Table ---
    int numCols = 2 + (int)m_slots.size();   // ☐ + Time + [cameras]
    m_table = new QTableWidget(0, numCols, this);

    QStringList headers;
    headers << "" << "Time";
    for (const auto& slot : m_slots) {
        std::string disp = slot;
        if (!disp.empty()) disp[0] = std::toupper((unsigned char)disp[0]);
        headers << QString::fromStdString(disp);
    }
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(true);
    m_table->setAlternatingRowColors(true);

    auto* hdr = m_table->horizontalHeader();
    hdr->setSectionResizeMode(0, QHeaderView::Fixed);
    hdr->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 30);
    m_table->setColumnWidth(1, 72);
    for (int c = 2; c < numCols; ++c)
        hdr->setSectionResizeMode(c, QHeaderView::Stretch);

    // Populate rows — block signals during construction to avoid spurious updates
    m_table->blockSignals(true);
    int segCount = (int)trip.segments.size();
    m_table->setRowCount(segCount);

    for (int i = 0; i < segCount; ++i) {
        const TripSegment& seg = trip.segments[i];

        // Col 0: checkbox; UserRole stores the original segment index
        auto* chk = new QTableWidgetItem();
        chk->setCheckState(Qt::Unchecked);
        chk->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        chk->setData(Qt::UserRole, i);
        m_table->setItem(i, 0, chk);

        // Col 1: time formatted from "YYYYMMDD_HHMMSS"
        QString timeStr;
        const std::string& ts = seg.timestamp;
        if (ts.size() >= 15) {
            timeStr = QString("%1:%2:%3")
                .arg(QString::fromStdString(ts.substr(9,  2)))
                .arg(QString::fromStdString(ts.substr(11, 2)))
                .arg(QString::fromStdString(ts.substr(13, 2)));
        } else {
            timeStr = QString::fromStdString(ts);
        }
        auto* timeItem = new QTableWidgetItem(timeStr);
        timeItem->setFlags(Qt::ItemIsEnabled);
        timeItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 1, timeItem);

        // Camera columns
        for (int c = 0; c < (int)m_slots.size(); ++c) {
            const std::string& path = camPath(seg, m_slots[c]);
            bool absent = (path.empty() || path == "-");

            QString display;
            if (absent) {
                display = "\u2014";  // em-dash
            } else {
                display = QString::fromStdString(
                    fs::path(path).filename().string());
            }

            auto* cell = new QTableWidgetItem(display);
            cell->setFlags(Qt::ItemIsEnabled);
            if (absent)
                cell->setForeground(QColor(0xb0b0b0));
            else
                cell->setToolTip(QString::fromStdString(path));
            m_table->setItem(i, 2 + c, cell);
        }
    }
    m_table->blockSignals(false);

    connect(m_table, &QTableWidget::itemChanged,
            this, &DangerousDialog::onTableItemChanged);

    // --- Status label ---
    m_statusLabel = new QLabel("No segments selected.", this);
    m_statusLabel->setStyleSheet("color: #555; font-size: 9pt;");

    // --- Buttons ---
    m_archiveBtn = new QPushButton("Move to Archive", this);
    m_archiveBtn->setEnabled(false);

    m_deleteBtn = new QPushButton("Permanently Delete", this);
    m_deleteBtn->setEnabled(false);
    m_deleteBtn->setStyleSheet(
        "QPushButton:enabled  { color: white; background: #c0392b;"
        "  border-radius: 3px; padding: 4px 12px; }"
        "QPushButton:disabled { color: #aaa; background: #eee;"
        "  border-radius: 3px; padding: 4px 12px; }");

    auto* closeBtn = new QPushButton("Close", this);
    connect(closeBtn,    &QPushButton::clicked, this, &QDialog::reject);
    connect(m_deleteBtn, &QPushButton::clicked, this, &DangerousDialog::onPermanentDelete);
    connect(m_archiveBtn,&QPushButton::clicked, this, &DangerousDialog::onMoveToArchive);

    auto* btnRow = new QHBoxLayout();
    btnRow->addWidget(m_statusLabel);
    btnRow->addStretch();
    btnRow->addWidget(m_archiveBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addWidget(closeBtn);

    auto* vlay = new QVBoxLayout(this);
    vlay->addWidget(infoLabel);
    vlay->addWidget(m_table, 1);
    vlay->addLayout(btnRow);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void DangerousDialog::onTableItemChanged(QTableWidgetItem* item)
{
    if (item && item->column() == 0)
        refreshActionState();
}

void DangerousDialog::onPermanentDelete()
{
    auto indices = checkedSegmentIndices();
    if (indices.empty()) return;

    int n = (int)indices.size();
    auto reply = QMessageBox::warning(
        this, "Permanently Delete Segments",
        QString("Permanently delete %1 segment(s) from trip [%2]?\n\n"
                "All video files and thumbnail sidecars will be removed.\n"
                "This cannot be undone.")
            .arg(n)
            .arg(QString::fromStdString(m_trip.id)),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (reply != QMessageBox::Yes) return;

    performOps(indices, false);
}

void DangerousDialog::onMoveToArchive()
{
    auto indices = checkedSegmentIndices();
    if (indices.empty()) return;
    performOps(indices, true);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::vector<int> DangerousDialog::checkedSegmentIndices() const
{
    std::vector<int> result;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto* item = m_table->item(r, 0);
        if (item && item->checkState() == Qt::Checked)
            result.push_back(item->data(Qt::UserRole).toInt());
    }
    return result;
}

void DangerousDialog::performOps(const std::vector<int>& segIndices, bool archive)
{
    std::vector<std::string> errors;
    std::set<int> processed;

    for (int si : segIndices) {
        if (si < 0 || si >= (int)m_trip.segments.size()) continue;
        const TripSegment& seg = m_trip.segments[si];

        // Collect all files for this segment (video + thumbnail sidecars)
        std::vector<std::string> files;
        for (const auto& [slot, path] : seg.cameras)
            if (!path.empty() && path != "-") files.push_back(path);
        for (const auto& [slot, path] : seg.thumbs)
            if (!path.empty()) files.push_back(path);

        bool segOk = true;
        for (const auto& f : files) {
            std::error_code ec;
            if (!fs::exists(f, ec)) continue;   // already gone — not an error

            try {
                if (archive) {
                    std::string dest = archiveDestPath(f);
                    if (dest.empty()) {
                        // File is outside sourcePath — skip with a note but don't fail
                        errors.push_back("Skipped (outside source path): " + f);
                        continue;
                    }
                    fs::create_directories(fs::path(dest).parent_path());
                    fs::rename(f, dest);
                } else {
                    fs::remove(f);
                }
            } catch (const std::exception& e) {
                errors.push_back(f + ":\n  " + e.what());
                segOk = false;
            }
        }
        if (segOk) processed.insert(si);
    }

    if (!errors.empty()) {
        QString msg = archive ? "Some files could not be archived:"
                              : "Some files could not be deleted:";
        for (const auto& e : errors)
            msg += "\n\u2022 " + QString::fromStdString(e);
        QMessageBox::warning(this, archive ? "Archive Errors" : "Delete Errors", msg);
    }

    if (!processed.empty()) {
        std::vector<int> processedVec(processed.begin(), processed.end());
        updateManifest(processedVec);
        removeProcessedRows(processedVec);
        emit manifestChanged();
        refreshActionState();
    }

    if (m_table->rowCount() == 0)
        m_statusLabel->setText("All segments processed.");
}

void DangerousDialog::updateManifest(const std::vector<int>& removedSegIdx)
{
    std::set<int> removedSet(removedSegIdx.begin(), removedSegIdx.end());

    ConfigManager cfg;
    cfg.loadSettings();
    auto trips = cfg.loadTripCache(m_sourcePath);

    for (auto& t : trips) {
        if (t.id != m_trip.id) continue;

        std::vector<TripSegment> kept;
        for (int i = 0; i < (int)t.segments.size(); ++i) {
            if (!removedSet.count(i))
                kept.push_back(t.segments[i]);
        }
        t.segments = std::move(kept);

        if (!t.segments.empty()) {
            // Recompute start fields if the leading segment changed.
            time_t firstEpoch = parseSegTimestamp(t.segments.front().timestamp);
            if (firstEpoch > 0 && firstEpoch != t.startEpoch) {
                t.startEpoch = firstEpoch;
                std::tm tmBuf{};
                localtime_r(&firstEpoch, &tmBuf);
                char dateBuf[12], timeBuf[10];
                std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &tmBuf);
                std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tmBuf);
                t.date      = dateBuf;
                t.startTime = timeBuf;
            }

            // Recompute duration: (lastEpoch - firstEpoch) + segdur.
            // Prefix with ~ only when middle segments were removed (non-contiguous
            // remaining block), i.e. any removed index falls between the first
            // and last kept segment index.
            time_t lastEpoch = parseSegTimestamp(t.segments.back().timestamp);
            if (firstEpoch > 0 && lastEpoch > 0) {
                int total = std::max(0, (int)(lastEpoch - firstEpoch) + t.segdur);
                std::string durStr = std::to_string(total / 60) + "m "
                                   + std::to_string(total % 60) + "s";

                // Determine if any gap was introduced: a gap exists if any
                // removed index falls strictly between the first and last kept index.
                int origN = (int)(t.segments.size() + removedSet.size());
                int fk = -1, lk = -1;
                for (int i = 0; i < origN; ++i) {
                    if (!removedSet.count(i)) {
                        if (fk < 0) fk = i;
                        lk = i;
                    }
                }
                bool hasGap = false;
                for (int ri : removedSet) {
                    if (ri > fk && ri < lk) { hasGap = true; break; }
                }

                t.duration = (hasGap ? "~" : "") + durStr;
            }

            // Recompute thumbnail convenience fields.
            int ti = t.segments.size() > 1 ? 1 : 0;
            t.firstThumbs = t.segments[ti].thumbs;
            t.lastThumbs  = t.segments.back().thumbs;
        } else {
            t.firstThumbs.clear();
            t.lastThumbs.clear();
        }

        // GPS track is now stale — clear it so re-extraction is required.
        t.gpsTrackStatus = "none";
        t.gpsLockSeconds = -1;
        t.gpsTrack.clear();

        break;
    }

    // Purge trips that have no segments left
    trips.erase(
        std::remove_if(trips.begin(), trips.end(),
                       [](const Trip& t) { return t.segments.empty(); }),
        trips.end());

    cfg.saveTripCache(m_sourcePath, trips);
}

std::string DangerousDialog::archiveDestPath(const std::string& absPath) const
{
    if (absPath.size() <= m_sourcePath.size()) return "";
    if (absPath.substr(0, m_sourcePath.size()) != m_sourcePath) return "";
    std::string rel = absPath.substr(m_sourcePath.size());
    if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
    return m_sourcePath + "/Archive/" + rel;
}

void DangerousDialog::removeProcessedRows(const std::vector<int>& segIndices)
{
    std::set<int> removed(segIndices.begin(), segIndices.end());
    for (int r = m_table->rowCount() - 1; r >= 0; --r) {
        auto* item = m_table->item(r, 0);
        if (item && removed.count(item->data(Qt::UserRole).toInt()))
            m_table->removeRow(r);
    }
}

void DangerousDialog::refreshActionState()
{
    int n = (int)checkedSegmentIndices().size();
    m_statusLabel->setText(n == 0 ? "No segments selected."
                                  : QString("%1 segment(s) selected.").arg(n));
    m_deleteBtn->setEnabled(n > 0);
    m_archiveBtn->setEnabled(n > 0);
}
// SN: 00106
