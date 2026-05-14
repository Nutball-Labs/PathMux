// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "ExtrasDialog.h"
#include "JobQueue.h"   // GpsExtractJob, MapRenderJob, SyncAnalysisJob
#include "config_manager.hpp"
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <filesystem>

namespace fs = std::filesystem;
using namespace CamClops;

static bool anyExists(const std::vector<std::string>& paths)
{
    std::error_code ec;
    for (const auto& p : paths)
        if (!p.empty() && fs::exists(p, ec)) return true;
    return false;
}

static void applyColor(QCheckBox* cb, bool green)
{
    // :disabled selector overrides disabled-palette so the informational
    // colour still shows on items that are enabled=false.
    cb->setStyleSheet(green
        ? "QCheckBox { color: #22aa44; } QCheckBox:disabled { color: #22aa44; }"
        : "QCheckBox { color: #cc2222; } QCheckBox:disabled { color: #cc2222; }");
}

ExtrasDialog::ExtrasDialog(const Trip& trip,
                           const std::string& sourcePath,
                           const std::string& mid,
                           QWidget* parent)
    : QDialog(parent), m_trip(trip), m_sourcePath(sourcePath), m_mid(mid)
{
    QString addr = mid.empty()
        ? QString::fromStdString(trip.id)
        : QString::fromStdString(mid + ":" + trip.id);
    setWindowTitle(QString("Extras — %1").arg(addr));
    setFixedWidth(300);
    setSizeGripEnabled(false);

    bool gpsAvail = (trip.gpsTrackStatus != "unavailable");

    auto* vlay = new QVBoxLayout(this);
    vlay->setSpacing(6);
    vlay->setContentsMargins(14, 12, 14, 12);

    // Small ⓘ label with tooltip — explains what each item does.
    auto makeInfo = [&](const QString& tip) -> QLabel* {
        auto* lbl = new QLabel("ⓘ", this);
        lbl->setToolTip(tip);
        lbl->setStyleSheet("color: #5a9fd4; padding: 0 2px;");
        lbl->setCursor(Qt::WhatsThisCursor);
        lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        return lbl;
    };

    // ── GPS Extract (full row) ────────────────────────────────────────────
    m_gpsCheck = new QCheckBox("GPS Extract", this);
    m_gpsCheck->setEnabled(gpsAvail);
    if (!gpsAvail)
        m_gpsCheck->setToolTip("No GPS hardware on this camera profile");

    auto* gpsRow = new QHBoxLayout;
    gpsRow->setContentsMargins(0, 0, 0, 0);
    gpsRow->setSpacing(4);
    gpsRow->addWidget(m_gpsCheck, 1);
    gpsRow->addWidget(makeInfo(
        "<b>GPS Extract</b><br>"
        "Extracts the GPS track from all footage segments using exiftool. "
        "Stores coordinates, speed, heading, and altitude as a GeoJSON file.<br><br>"
        "Camera sync analysis runs automatically as a second stage — "
        "audio cross-correlation computes the exact start-time offset for each "
        "camera so collage builds are frame-accurate across all four cameras."));
    vlay->addLayout(gpsRow);

    // ── Tree children: Map, Dashboard, HUD ───────────────────────────────
    struct Child { const char* connector; QCheckBox*& cb; const char* label; const char* tip; };
    for (auto& c : std::initializer_list<Child>{
        { "├─", m_mapCheck,
          "Map",
          "<b>Map</b><br>"
          "Renders a GPS-synced moving-map video showing the vehicle route "
          "on OpenStreetMap tiles. Can be placed in a collage quadrant or "
          "used as a standalone overlay in TripBuildDialog." },
        { "├─", m_dashCheck,
          "Dashboard",
          "<b>Dashboard</b><br>"
          "Renders a telemetry dashboard panel showing speed, heading, "
          "and GPS timestamp. Composited as a center overlay in TripBuildDialog." },
        { "└─", m_hudCheck,
          "HUD",
          "<b>HUD</b><br>"
          "Renders a full-screen transparent heads-up display — speed tapes "
          "(KPH and MPH) and a circular compass rose — as a VP9/alpha WebM. "
          "Composited over the full 3840×2160 collage at final build time." },
    }) {
        auto* row  = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        auto* line = new QLabel(QString::fromUtf8(c.connector), this);
        line->setStyleSheet("color: #999; font-family: monospace;");
        line->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        row->addWidget(line);
        c.cb = new QCheckBox(QString::fromUtf8(c.label), this);
        row->addWidget(c.cb, 1);
        row->addWidget(makeInfo(QString::fromUtf8(c.tip)));
        vlay->addLayout(row);
    }

    // ── Separator ─────────────────────────────────────────────────────────
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    vlay->addWidget(sep);

    // ── Sync Cameras ──────────────────────────────────────────────────────
    auto* syncRow = new QHBoxLayout;
    syncRow->setContentsMargins(0, 0, 0, 0);
    syncRow->setSpacing(4);
    m_syncCheck = new QCheckBox("Sync Cameras", this);
    syncRow->addWidget(m_syncCheck, 1);
    syncRow->addWidget(makeInfo(
        "<b>Sync Cameras</b><br>"
        "Analyzes audio cross-correlation across all camera channels to compute "
        "the exact start-time offset for each camera relative to the others. "
        "Results are stored in the manifest and used by collage builds for "
        "frame-accurate multi-camera synchronization.<br><br>"
        "When <i>GPS Extract</i> is checked, sync runs automatically as stage 2 "
        "of that job. Check this box alone to re-run sync on a trip that already "
        "has GPS data, or on cameras with no GPS hardware."));
    vlay->addLayout(syncRow);

    vlay->addSpacing(4);

    // ── Buttons ───────────────────────────────────────────────────────────
    m_queueBtn = new QPushButton("Queue", this);
    m_queueBtn->setDefault(true);
    m_queueBtn->setEnabled(false);
    auto* cancelBtn = new QPushButton("Cancel", this);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_queueBtn);
    btnRow->addWidget(cancelBtn);
    vlay->addLayout(btnRow);

    connect(m_gpsCheck,  &QCheckBox::toggled, this, &ExtrasDialog::updateState);
    connect(m_syncCheck, &QCheckBox::toggled, this, &ExtrasDialog::updateState);
    connect(m_mapCheck,  &QCheckBox::toggled, this, &ExtrasDialog::onOutputChecked);
    connect(m_dashCheck, &QCheckBox::toggled, this, &ExtrasDialog::onOutputChecked);
    connect(m_hudCheck,  &QCheckBox::toggled, this, &ExtrasDialog::onOutputChecked);

    connect(m_queueBtn, &QPushButton::clicked, this, &ExtrasDialog::onQueue);
    connect(cancelBtn,  &QPushButton::clicked, this, &QDialog::reject);

    updateState();
}

void ExtrasDialog::onOutputChecked()
{
    // Auto-check GPS Extract when a dependent item is checked and GPS isn't ready.
    bool hasGps   = (m_trip.gpsTrackStatus == "complete");
    bool gpsChecked = m_gpsCheck->isEnabled() && m_gpsCheck->isChecked();
    if (!hasGps && !gpsChecked &&
        (m_mapCheck->isChecked() || m_dashCheck->isChecked() || m_hudCheck->isChecked())) {
        QSignalBlocker blk(m_gpsCheck);
        m_gpsCheck->setChecked(true);
    }
    updateState();
}

void ExtrasDialog::updateState()
{
    bool hasGps     = (m_trip.gpsTrackStatus == "complete");
    bool gpsChecked = m_gpsCheck->isEnabled() && m_gpsCheck->isChecked();
    bool gpsReady   = hasGps || gpsChecked;

    // ── GPS Extract colour ────────────────────────────────────────────────
    if (m_trip.gpsTrackStatus != "unavailable")
        applyColor(m_gpsCheck, hasGps || gpsChecked);

    // ── Map / Dashboard / HUD ────────────────────────────────────────────
    auto refreshOutput = [&](QCheckBox* cb, bool fileExists) {
        cb->setEnabled(gpsReady);
        if (!gpsReady)
            cb->setStyleSheet("");
        else
            applyColor(cb, fileExists || cb->isChecked());
    };

    refreshOutput(m_mapCheck,  anyExists(m_trip.mapVideos));
    refreshOutput(m_dashCheck, anyExists(m_trip.dashVideos));
    refreshOutput(m_hudCheck,  anyExists(m_trip.hudVideos));

    // ── Sync Cameras ─────────────────────────────────────────────────────
    // Disable when GPS Extract is checked — sync runs as stage 2 of that job.
    if (gpsChecked) {
        QSignalBlocker blk(m_syncCheck);
        m_syncCheck->setChecked(false);
        m_syncCheck->setEnabled(false);
        applyColor(m_syncCheck, true);
        m_syncCheck->setToolTip("Included automatically in GPS extraction");
    } else {
        m_syncCheck->setEnabled(true);
        m_syncCheck->setToolTip("");
        applyColor(m_syncCheck, m_trip.cameraSync.valid || m_syncCheck->isChecked());
    }

    // ── Queue button ─────────────────────────────────────────────────────
    bool anyChecked = gpsChecked ||
                      m_syncCheck->isChecked() ||
                      (m_mapCheck->isEnabled()  && m_mapCheck->isChecked())  ||
                      (m_dashCheck->isEnabled() && m_dashCheck->isChecked()) ||
                      (m_hudCheck->isEnabled()  && m_hudCheck->isChecked());
    m_queueBtn->setEnabled(anyChecked);
}

void ExtrasDialog::onQueue()
{
    ConfigManager cfg;
    cfg.loadSettings();
    std::string manifestFile = cfg.lookupManifestFilePath(m_sourcePath);
    if (manifestFile.empty()) { reject(); return; }

    QString src      = QString::fromStdString(m_sourcePath);
    QString tid      = QString::fromStdString(m_trip.id);
    QString manifest = QString::fromStdString(manifestFile);
    QString addr     = QString::fromStdString(
        m_mid.empty() ? m_trip.id : m_mid + ":" + m_trip.id).toUpper();
    // Filename-safe trip address: MID-TID (colon invalid on Windows/macOS)
    QString ftid     = m_mid.empty() ? tid.toUpper()
                     : QString::fromStdString(m_mid).toUpper() + "-" + tid.toUpper();

    // GPS Extract: two-stage job (exiftool GPS → sync analysis).
    // Standalone sync: queued only when GPS Extract is not checked.
    if (m_gpsCheck->isEnabled() && m_gpsCheck->isChecked())
        JobQueue::instance().enqueue(new GpsExtractJob(
            m_mid, m_trip.id, manifestFile, cfg.getExiftoolPath()));
    else if (m_syncCheck->isChecked())
        JobQueue::instance().enqueue(new SyncAnalysisJob(m_mid, m_trip.id));

    if (m_mapCheck->isEnabled() && m_mapCheck->isChecked())
        JobQueue::instance().enqueue(new MapRenderJob(
            "Map — " + addr, "clops_maprender.py", manifest, tid,
            src + "/clops_trip_" + ftid + "_map.mp4",
            m_trip.videoProfile.width, m_trip.videoProfile.height));

    if (m_dashCheck->isEnabled() && m_dashCheck->isChecked())
        JobQueue::instance().enqueue(new MapRenderJob(
            "Dashboard — " + addr, "clops_dashboard.py", manifest, tid,
            src + "/clops_trip_" + ftid + "_dash.mp4",
            960, 540));

    if (m_hudCheck->isEnabled() && m_hudCheck->isChecked())
        // HUD overlays the full 3840×2160 collage output — must NOT use the
        // per-camera videoProfile dimensions or it only covers one quadrant.
        JobQueue::instance().enqueue(new MapRenderJob(
            "HUD — " + addr, "clops_hud.py", manifest, tid,
            src + "/clops_trip_" + ftid + "_hud.webm",
            3840, 2160));

    accept();
}
// SN: 00117
