// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "TripBuildDialog.h"
#include "LogoMorphWidget.h"
#include <algorithm>
#include <filesystem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QFileInfo>
#include <QStandardItemModel>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QProcess>
#include <QMessageBox>
#include <QSignalBlocker>

using namespace CamClops;

// ---------------------------------------------------------------------------
// Extract logo_morph.mp4 from QRC to a temp file once per process.
// Returns the absolute file path, or "" if the resource is unavailable.
// ---------------------------------------------------------------------------
static std::string logoMorphPath()
{
    static std::string cached;
    if (!cached.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(cached, ec)) return cached;
        cached.clear();
    }
    QFile src(":/images/logo_morph.mp4");
    if (!src.open(QIODevice::ReadOnly)) return {};
    QString tmp = QDir::temp().filePath("clops_logo_morph.mp4");
    QFile dst(tmp);
    if (!dst.open(QIODevice::WriteOnly)) return {};
    dst.write(src.readAll());
    cached = tmp.toStdString();
    return cached;
}

// ---------------------------------------------------------------------------
static std::set<std::string> presentCameras(const Trip& trip)
{
    std::set<std::string> present;
    for (const auto& seg : trip.segments)
        for (const auto& [slot, path] : seg.cameras)
            if (!path.empty() && path != "-")
                present.insert(slot);
    return present;
}

// Encode a combo item's data: "camera:front", "file:/path", "browse"
static QString camData(const char* slot)   { return QString("camera:") + slot; }
static QString fileData(const QString& p)  { return "file:" + p; }
static constexpr const char* kBrowse = "browse";
static constexpr const char* kNone   = "none";

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
TripBuildDialog::TripBuildDialog(const ManifestEntry& manifest,
                                 const Trip& trip,
                                 QWidget* parent)
    : QDialog(parent), m_manifest(manifest), m_trip(trip)
{
    setWindowTitle(QString("Build Trip \u2014 %1:%2")
        .arg(QString::fromStdString(manifest.id).toUpper())
        .arg(QString::fromStdString(trip.id).toUpper()));
    setMinimumWidth(560);

    ConfigManager config;
    config.loadSettings();
    m_ffmpegPath = config.getFfmpegPath();
    m_encode     = config.getEncodeSettings();

    // Build quadrant→slot mapping from the embedded camera profile.
    // Slots with an explicit quadrant field are placed first; remaining slots
    // fill unoccupied positions in declaration order.
    {
        CameraProfile profile = config.getManifestProfile(manifest.path);
        // Initialise to empty (no assignment)
        for (int i = 0; i < 4; ++i) {
            m_kSlot[i] = "";
            m_kCam[i]  = QString(kPos[i]);
        }
        // Two passes: assigned quadrants first, then unassigned fill gaps.
        struct Entry { int q; std::string name; QString disp; };
        std::vector<Entry> entries;
        std::vector<Entry> unassigned;
        for (const auto& s : profile.cameraSlots) {
            QString disp = QString::fromStdString(
                s.displayName.empty() ? s.name : s.displayName);
            if (s.quadrant >= 0 && s.quadrant < 4)
                entries.push_back({s.quadrant, s.name, disp});
            else
                unassigned.push_back({-1, s.name, disp});
        }
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b){ return a.q < b.q; });
        // Fill unassigned into remaining positions in order
        std::set<int> used;
        for (const auto& e : entries) used.insert(e.q);
        int next = 0;
        for (auto& u : unassigned) {
            while (next < 4 && used.count(next)) ++next;
            if (next >= 4) break;
            u.q = next;
            used.insert(next++);
            entries.push_back(u);
        }
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b){ return a.q < b.q; });
        for (const auto& e : entries)
            if (e.q >= 0 && e.q < 4) { m_kSlot[e.q] = e.name; m_kCam[e.q] = e.disp; }
    }

    auto* vbox = new QVBoxLayout(this);
    vbox->setSpacing(8);
    vbox->setContentsMargins(12, 12, 12, 12);

    auto cams = presentCameras(trip);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(makeOutputTab(),      "Output");
    tabs->addTab(makeCollageTab(cams), "Collage Layout");
    tabs->addTab(makeFilesTab(cams),   "Camera Files");
    vbox->addWidget(tabs);

    vbox->addWidget(makeActionRow());

    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    vbox->addWidget(bbox);
}

// ---------------------------------------------------------------------------
// Collage Layout tab
// ---------------------------------------------------------------------------
QWidget* TripBuildDialog::makeCollageTab(const std::set<std::string>& cams)
{
    auto* w    = new QWidget(this);
    auto* vbox = new QVBoxLayout(w);
    vbox->setSpacing(10);
    vbox->setContentsMargins(10, 10, 10, 10);

    // ── 2-column / 3-row grid ─────────────────────────────────────────────────
    // Row 0: TL (front) | TR (rear)
    // Row 1: overlay centred, spanning both columns
    // Row 2: BL (right) | BR (left)
    auto* gridFrame = new QFrame(w);
    gridFrame->setStyleSheet(
        "QFrame { background: #111118; border: 2px solid #303048; border-radius: 6px; }"
    );
    auto* grid = new QGridLayout(gridFrame);
    grid->setSpacing(4);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 0);
    grid->setRowStretch(2, 1);

    grid->addWidget(makeQuadrantCell(0, cams), 0, 0);  // TL = front
    grid->addWidget(makeQuadrantCell(1, cams), 0, 1);  // TR = rear

    // Overlay row: centred horizontally across both columns
    {
        auto* overlayRowW = new QWidget(gridFrame);
        overlayRowW->setStyleSheet("QWidget { background: transparent; border: none; }");
        auto* overlayRowL = new QHBoxLayout(overlayRowW);
        overlayRowL->setContentsMargins(0, 0, 0, 0);
        overlayRowL->addStretch();
        overlayRowL->addWidget(makeOverlayCell());
        overlayRowL->addStretch();
        grid->addWidget(overlayRowW, 1, 0, 1, 2);
    }

    grid->addWidget(makeQuadrantCell(2, cams), 2, 0);  // BL = right
    grid->addWidget(makeQuadrantCell(3, cams), 2, 1);  // BR = left

    vbox->addWidget(gridFrame);

    // ── HUD overlay (full-screen, below grid) ────────────────────────────────
    vbox->addWidget(makeHudRow());

    // ── Audio source ─────────────────────────────────────────────────────────
    auto* audioRow   = new QHBoxLayout;
    auto* audioLabel = new QLabel("Collage audio from:", w);
    m_audioQuad      = new QComboBox(w);
    for (int i = 0; i < 4; ++i)
        m_audioQuad->addItem(
            QString("%1 (%2)").arg(kPos[i]).arg(m_kCam[i]));
    // Default audio to left camera (driver's position in LHD countries).
    // Fallback: first present camera if no left slot exists in the profile.
    {
        int leftQuad = -1, firstPresent = -1;
        for (int i = 0; i < 4; ++i) {
            if (m_kSlot[i].empty() || !cams.count(m_kSlot[i])) continue;
            if (firstPresent < 0) firstPresent = i;
            if (m_kSlot[i] == "left") { leftQuad = i; break; }
        }
        m_audioQuad->setCurrentIndex(leftQuad >= 0 ? leftQuad : std::max(firstPresent, 0));
    }
    audioRow->addWidget(audioLabel);
    audioRow->addWidget(m_audioQuad);
    audioRow->addStretch();
    vbox->addLayout(audioRow);

    // ── Resolution ───────────────────────────────────────────────────────────
    auto* resRow = new QHBoxLayout;
    auto* resLbl = new QLabel("Build:", w);
    m_chk4K      = new QCheckBox("4K  (3840\u00d72160 H.265)", w);
    m_chk1080    = new QCheckBox("1080p downscale", w);
    m_chk4K->setChecked(true);
    resRow->addWidget(resLbl);
    resRow->addSpacing(8);
    resRow->addWidget(m_chk4K);
    resRow->addSpacing(20);
    resRow->addWidget(m_chk1080);
    resRow->addStretch();
    vbox->addLayout(resRow);

    // ── Preview Frame ─────────────────────────────────────────────────────────
    m_btnPreview = new QPushButton("Preview Frame\u2026", w);
    m_btnPreview->setToolTip(
        "Grab one frame from each active quadrant source and\n"
        "show a composite preview of the collage layout.");
    connect(m_btnPreview, &QPushButton::clicked,
            this, &TripBuildDialog::onPreviewFrame);
    auto* prevRow = new QHBoxLayout;
    prevRow->addStretch();
    prevRow->addWidget(m_btnPreview);
    vbox->addLayout(prevRow);

    vbox->addStretch();
    return w;
}

// ---------------------------------------------------------------------------
// Centre overlay cell — sits at grid position (1,1) between the four quadrants
// ---------------------------------------------------------------------------
QWidget* TripBuildDialog::makeOverlayCell()
{
    auto* cell = new QFrame(this);
    cell->setObjectName("overlayCell");
    cell->setStyleSheet(
        "QFrame#overlayCell { background: #161628; border: 1px dashed #5050a0;"
        " border-radius: 4px; min-width: 160px; }"
        "QCheckBox { color: #e8e8ff; font-size: 9pt; font-weight: bold; border: none; spacing: 6px; }"
        "QCheckBox::indicator { width: 15px; height: 15px; border: 2px solid #6060b0; border-radius: 3px; }"
        "QCheckBox::indicator:checked { border-color: #a0a0ff; background-color: #3838a0; }"
        "QCheckBox::indicator:hover { border-color: #9090cc; }"
        "QLabel    { color: #505090; font-size: 7pt; border: none; }"
        "QLineEdit { background: #202038; color: #c0c0e0;"
        "  border: 1px solid #404070; border-radius: 3px;"
        "  padding: 1px 3px; font-size: 8pt; }"
        "QPushButton { background: #252540; color: #a0a0c8;"
        "  border: 1px solid #404070; border-radius: 3px; font-size: 8pt; }"
        "QComboBox { background: #202038; color: #c0c0e0;"
        "  border: 1px solid #404070; border-radius: 3px;"
        "  padding: 2px 4px; font-size: 8pt; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView {"
        "  background: #202038; color: #c0c0e0;"
        "  selection-background-color: #3030a0;"
        "  selection-color: #e0e0f0;"
        "  border: 1px solid #404070; }"
    );
    auto* vbox = new QVBoxLayout(cell);
    vbox->setSpacing(3);
    vbox->setContentsMargins(6, 4, 6, 6);

    auto* title = new QLabel("OVERLAY", cell);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("color: #9090cc; font-size: 8pt; font-weight: bold; letter-spacing: 1px;");
    vbox->addWidget(title);

    m_chkMapOverlay = new QCheckBox("Enable", cell);
    vbox->addWidget(m_chkMapOverlay);

    // Source combo — same options as quadrants: cameras, map/dash files, external, none
    m_overlayCombo = new QComboBox(cell);
    vbox->addWidget(m_overlayCombo);

    auto cams = presentCameras(m_trip);
    for (int k = 0; k < 4; ++k) {
        if (m_kSlot[k].empty() || !cams.count(m_kSlot[k])) continue;
        m_overlayCombo->addItem(QString("Camera: %1").arg(m_kCam[k]),
                                camData(m_kSlot[k].c_str()));
    }
    for (const auto& p : m_trip.mapVideos) {
        QFileInfo fi(QString::fromStdString(p));
        m_overlayCombo->addItem("Map: " + fi.fileName(),
                                fileData(fi.absoluteFilePath()));
    }
    for (const auto& p : m_trip.dashVideos) {
        QFileInfo fi(QString::fromStdString(p));
        m_overlayCombo->addItem("Dashboard: " + fi.fileName(),
                                fileData(fi.absoluteFilePath()));
    }
    for (const auto& p : m_trip.hudVideos) {
        QFileInfo fi(QString::fromStdString(p));
        m_overlayCombo->addItem("HUD: " + fi.fileName(),
                                fileData(fi.absoluteFilePath()));
    }
    m_overlayCombo->addItem("External video\u2026", QString(kBrowse));
    m_overlayCombo->addItem("None", QString(kNone));

    // Pre-select first map video if one exists, else default to first item
    {
        namespace fs = std::filesystem;
        std::string candidate = m_manifest.path
            + "/clops_trip_" + m_trip.id + "_map.mp4";
        std::error_code ec;
        bool found = false;
        if (fs::exists(candidate, ec)) {
            for (int i = 0; i < m_overlayCombo->count(); ++i) {
                QString d = m_overlayCombo->itemData(i).toString();
                if (d.startsWith("file:") &&
                    d.mid(5) == QString::fromStdString(candidate)) {
                    m_overlayCombo->setCurrentIndex(i);
                    found = true;
                    break;
                }
            }
        }
        if (!found && !m_trip.mapVideos.empty())
            m_overlayCombo->setCurrentIndex(
                cams.size());  // first map item after cameras
    }

    // File row (shown only for "External video…")
    auto* fileRowW = new QWidget(cell);
    fileRowW->setStyleSheet("QWidget { background: transparent; border: none; }");
    auto* fileRowL = new QHBoxLayout(fileRowW);
    fileRowL->setContentsMargins(0, 0, 0, 0);
    fileRowL->setSpacing(3);
    m_mapOverlayPath      = new QLineEdit(fileRowW);
    m_mapOverlayPath->setPlaceholderText("video\u2026");
    m_mapOverlayBrowseBtn = new QPushButton("\u2026", fileRowW);
    m_mapOverlayBrowseBtn->setFixedWidth(22);
    connect(m_mapOverlayBrowseBtn, &QPushButton::clicked,
            this, &TripBuildDialog::onBrowseOverlay);
    fileRowL->addWidget(m_mapOverlayPath, 1);
    fileRowL->addWidget(m_mapOverlayBrowseBtn);
    fileRowW->hide();
    m_overlayFileRow = fileRowW;
    vbox->addWidget(fileRowW);

    // Position combo — where the overlay sits in the output frame
    {
        auto* posRow = new QHBoxLayout;
        auto* posLbl = new QLabel("Position:", cell);
        posLbl->setStyleSheet("font-size: 8pt;");
        m_overlayPos = new QComboBox(cell);
        m_overlayPos->addItem("Center",       QString("center"));
        m_overlayPos->addItem("Top-Left",     QString("tl"));
        m_overlayPos->addItem("Top-Right",    QString("tr"));
        m_overlayPos->addItem("Bottom-Left",  QString("bl"));
        m_overlayPos->addItem("Bottom-Right", QString("br"));
        posRow->addWidget(posLbl);
        posRow->addWidget(m_overlayPos, 1);
        vbox->addLayout(posRow);
    }

    auto* hint = new QLabel("960\u00d7540", cell);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet("font-size: 8pt; color: gray;");
    vbox->addWidget(hint);

    // Morph placeholder — visible when overlay combo is set to None
    m_overlayMorph = new LogoMorphWidget(cell);
    m_overlayMorph->hide();
    vbox->addWidget(m_overlayMorph);

    // Show/hide file row and morph based on combo selection
    auto updateOverlayCombo = [=](int) {
        QString d = m_overlayCombo->currentData().toString();
        m_overlayFileRow->setVisible(d == kBrowse);
        m_overlayMorph->setVisible(d == kNone);
    };
    connect(m_overlayCombo, &QComboBox::currentIndexChanged, updateOverlayCombo);
    updateOverlayCombo(m_overlayCombo->currentIndex());

    auto updateOverlay = [=](bool on) {
        m_overlayCombo->setEnabled(on);
        m_overlayPos->setEnabled(on);
        m_overlayFileRow->setEnabled(on);
        m_overlayMorph->setEnabled(on);
    };
    updateOverlay(false);
    connect(m_chkMapOverlay, &QCheckBox::toggled, updateOverlay);

    return cell;
}

// ---------------------------------------------------------------------------
// HUD overlay row — full-screen VP9 alpha composite below the grid
// ---------------------------------------------------------------------------
QWidget* TripBuildDialog::makeHudRow()
{
    auto* row  = new QWidget(this);
    row->setObjectName("hudRow");
    row->setStyleSheet(
        "QWidget#hudRow { background: #141426; border: 1px solid #303058; border-radius: 4px; }"
        "QWidget { background: transparent; border: none; }"
        "QCheckBox { color: #e8e8ff; font-size: 9pt; font-weight: bold; spacing: 6px; }"
        "QCheckBox::indicator { width: 15px; height: 15px; border: 2px solid #6060b0; border-radius: 3px; }"
        "QCheckBox::indicator:checked { border-color: #a0a0ff; background-color: #3838a0; }"
        "QCheckBox::indicator:hover { border-color: #9090cc; }"
        "QLabel    { color: #606090; font-size: 8pt; }"
        "QComboBox { background: #202038; color: #c0c0e0;"
        "  border: 1px solid #404070; border-radius: 3px; padding: 2px 4px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView {"
        "  background: #202038; color: #c0c0e0;"
        "  selection-background-color: #3030a0; selection-color: #e0e0f0;"
        "  border: 1px solid #404070; }"
        "QLineEdit { background: #202038; color: #c0c0e0;"
        "  border: 1px solid #404070; border-radius: 3px; padding: 1px 3px; font-size: 8pt; }"
        "QPushButton { background: #252540; color: #a0a0c8;"
        "  border: 1px solid #404070; border-radius: 3px; font-size: 8pt; }"
    );
    auto* hbox = new QHBoxLayout(row);
    hbox->setContentsMargins(8, 5, 8, 5);
    hbox->setSpacing(8);

    m_chkHudOverlay = new QCheckBox("HUD overlay (full-screen):", row);
    hbox->addWidget(m_chkHudOverlay);

    m_hudCombo = new QComboBox(row);
    m_hudCombo->setMinimumWidth(180);
    for (const auto& p : m_trip.hudVideos) {
        QFileInfo fi(QString::fromStdString(p));
        m_hudCombo->addItem("HUD: " + fi.fileName(),
                            fileData(fi.absoluteFilePath()));
    }
    m_hudCombo->addItem("External video…", QString(kBrowse));
    m_hudCombo->addItem("None", QString(kNone));
    // Pre-select first HUD video if any exist
    if (!m_trip.hudVideos.empty())
        m_hudCombo->setCurrentIndex(0);
    else
        m_hudCombo->setCurrentIndex(m_hudCombo->count() - 1);  // None
    hbox->addWidget(m_hudCombo);

    // External file row
    m_hudFileRow = new QWidget(row);
    m_hudFileRow->setStyleSheet("QWidget { background: transparent; border: none; }");
    auto* fileL = new QHBoxLayout(m_hudFileRow);
    fileL->setContentsMargins(0, 0, 0, 0);
    fileL->setSpacing(3);
    m_hudPath = new QLineEdit(m_hudFileRow);
    m_hudPath->setPlaceholderText("HUD .webm…");
    m_hudBrowseBtn = new QPushButton("…", m_hudFileRow);
    m_hudBrowseBtn->setFixedWidth(22);
    connect(m_hudBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString f = QFileDialog::getOpenFileName(
            this, "Select HUD Video",
            m_hudPath->text().isEmpty() ? QString() : m_hudPath->text(),
            "WebM / Video (*.webm *.mp4 *.mkv);;All Files (*)");
        if (!f.isEmpty()) m_hudPath->setText(f);
    });
    fileL->addWidget(m_hudPath, 1);
    fileL->addWidget(m_hudBrowseBtn);
    m_hudFileRow->hide();
    hbox->addWidget(m_hudFileRow, 1);

    hbox->addStretch();

    // Note label
    auto* note = new QLabel("VP9 alpha · 3840×2160", row);
    hbox->addWidget(note);

    // Combo logic: show file row only for External
    auto updateHudCombo = [=](int) {
        QString d = m_hudCombo->currentData().toString();
        m_hudFileRow->setVisible(d == kBrowse);
    };
    connect(m_hudCombo, &QComboBox::currentIndexChanged, updateHudCombo);
    updateHudCombo(m_hudCombo->currentIndex());

    // Enable/disable controls with checkbox
    auto updateHudEnabled = [this](bool on) {
        m_hudCombo->setEnabled(on);
        m_hudFileRow->setEnabled(on);
    };

    // Checkbox always starts unchecked — user opts in explicitly.
    // If they enable it with no HUD available, warn them.
    updateHudEnabled(false);
    connect(m_chkHudOverlay, &QCheckBox::toggled, this, [this, updateHudEnabled](bool on) {
        if (on && m_trip.hudVideos.empty() &&
            (!m_hudPath || m_hudPath->text().trimmed().isEmpty())) {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle("No HUD Video Available");
            msgBox.setIcon(QMessageBox::Warning);
            msgBox.setText("No HUD video has been generated for this trip yet.");
            msgBox.setInformativeText(
                "Go to Trip Properties → HUD tab to generate one first, "
                "then come back to enable it here.\n\n"
                "You can also select an external WebM file via the combo.");
            QPushButton* btnBuild =
                msgBox.addButton("Build HUD First", QMessageBox::RejectRole);
            msgBox.addButton("Continue Without HUD", QMessageBox::AcceptRole);
            msgBox.setDefaultButton(btnBuild);
            msgBox.exec();
            if (msgBox.clickedButton() == btnBuild) {
                QSignalBlocker blk(m_chkHudOverlay);
                m_chkHudOverlay->setChecked(false);
                updateHudEnabled(false);
                return;
            }
            // "Continue Without HUD" — stays checked, combo at None,
            // buildOptions() will produce an empty hudOverlayPath (no-op).
        }
        updateHudEnabled(on);
    });

    return row;
}

// ---------------------------------------------------------------------------
// Single quadrant cell widget
// ---------------------------------------------------------------------------
QWidget* TripBuildDialog::makeQuadrantCell(int idx,
                                            const std::set<std::string>& cams)
{
    auto* cell  = new QFrame(this);
    cell->setStyleSheet(
        "QFrame { background: #1e1e2e; border: 1px solid #404060;"
        " border-radius: 4px; }"
        "QLabel#posLabel { color: #8888aa; font-size: 8pt; border: none; }"
        "QComboBox { background: #2a2a3e; color: #e0e0f0;"
        "  border: 1px solid #505070; border-radius: 3px; padding: 2px 4px; }"
        "QLineEdit { background: #2a2a3e; color: #e0e0f0;"
        "  border: 1px solid #505070; border-radius: 3px; padding: 2px 4px; }"
        "QPushButton { background: #303048; color: #c0c0d8;"
        "  border: 1px solid #505070; border-radius: 3px; }"
    );
    auto* vbox = new QVBoxLayout(cell);
    vbox->setSpacing(4);
    vbox->setContentsMargins(8, 6, 8, 8);

    // Checkbox doubles as position label — unchecked = black frame in output
    m_quadEnabled[idx] = new QCheckBox(kPos[idx], cell);
    m_quadEnabled[idx]->setChecked(true);
    m_quadEnabled[idx]->setStyleSheet("color: #8888aa; font-size: 8pt;");
    vbox->addWidget(m_quadEnabled[idx]);

    m_quadCombo[idx] = new QComboBox(cell);
    auto* combo      = m_quadCombo[idx];
    vbox->addWidget(combo);

    // All present cameras — any camera can fill any quadrant.
    // Native camera for this quadrant (from profile) is starred.
    const std::string& nativeCam = m_kSlot[idx];
    int nativeItemIdx = -1;
    for (int k = 0; k < 4; ++k) {
        if (m_kSlot[k].empty() || !cams.count(m_kSlot[k])) continue;
        QString label = QString("Camera: %1").arg(m_kCam[k]);
        if (m_kSlot[k] == nativeCam) {
            nativeItemIdx = combo->count();
            label += " \u2605";  // star marks the native/default camera for this position
        }
        combo->addItem(label, camData(m_kSlot[k].c_str()));
    }

    // Map files from manifest
    for (const auto& p : m_trip.mapVideos) {
        QFileInfo fi(QString::fromStdString(p));
        combo->addItem("Map: " + fi.fileName(), fileData(fi.absoluteFilePath()));
    }

    // Dashboard files from manifest
    for (const auto& p : m_trip.dashVideos) {
        QFileInfo fi(QString::fromStdString(p));
        combo->addItem("Dashboard: " + fi.fileName(), fileData(fi.absoluteFilePath()));
    }

    // HUD files from manifest
    for (const auto& p : m_trip.hudVideos) {
        QFileInfo fi(QString::fromStdString(p));
        combo->addItem("HUD: " + fi.fileName(), fileData(fi.absoluteFilePath()));
    }

    // External video (browse)
    combo->addItem("External video\u2026", QString(kBrowse));

    // None — black frame placeholder
    combo->addItem("None (black frame)", QString(kNone));

    // File row (hidden until "External video…" or a manifest file is selected)
    auto* fileRowW = new QWidget(cell);
    auto* fileRow  = new QHBoxLayout(fileRowW);
    fileRow->setContentsMargins(0, 0, 0, 0);
    fileRow->setSpacing(4);

    m_quadPath[idx]   = new QLineEdit(fileRowW);
    m_quadPath[idx]->setPlaceholderText("Path to video file\u2026");
    m_quadBrowse[idx] = new QPushButton("\u2026", fileRowW);
    m_quadBrowse[idx]->setFixedWidth(28);

    fileRow->addWidget(m_quadPath[idx], 1);
    fileRow->addWidget(m_quadBrowse[idx]);
    fileRowW->hide();
    m_quadFileRow[idx] = fileRowW;
    vbox->addWidget(fileRowW);

    // Animated logo placeholder — visible only when "None (black frame)" is selected
    m_quadMorph[idx] = new LogoMorphWidget(cell);
    m_quadMorph[idx]->hide();
    vbox->addWidget(m_quadMorph[idx]);

    // Default: native camera if present, else first available camera
    if (nativeItemIdx >= 0)
        combo->setCurrentIndex(nativeItemIdx);
    else if (combo->count() > 0)
        combo->setCurrentIndex(0);

    // Wire browse button
    connect(m_quadBrowse[idx], &QPushButton::clicked, this, [this, idx]() {
        QString start = m_quadPath[idx]->text().isEmpty()
                        ? QString() : m_quadPath[idx]->text();
        QString file  = QFileDialog::getOpenFileName(
            this, "Select Video File", start,
            "Video Files (*.mp4 *.mkv *.mov *.avi *.ts *.mxf);;All Files (*)");
        if (!file.isEmpty())
            m_quadPath[idx]->setText(file);
    });

    // Show/hide file row when combo changes
    connect(combo, &QComboBox::currentIndexChanged, this, [this, idx](int) {
        updateQuadFileRow(idx);
    });

    // Enabled checkbox disables the combo (and file row) when unchecked;
    // show the morph animation whenever a quadrant is blanked out
    connect(m_quadEnabled[idx], &QCheckBox::toggled, this, [=](bool on) {
        combo->setEnabled(on);
        m_quadFileRow[idx]->setEnabled(on);
        updateQuadFileRow(idx);
    });

    return cell;
}

void TripBuildDialog::updateQuadFileRow(int idx)
{
    QString data     = m_quadCombo[idx]->currentData().toString();
    bool disabled    = m_quadEnabled[idx] && !m_quadEnabled[idx]->isChecked();
    bool showNone    = disabled || (data == kNone);
    m_quadFileRow[idx]->setVisible(!disabled && data == kBrowse);
    if (m_quadMorph[idx]) m_quadMorph[idx]->setVisible(showNone);
}

// ---------------------------------------------------------------------------
// Camera Files tab
// ---------------------------------------------------------------------------
QWidget* TripBuildDialog::makeFilesTab(const std::set<std::string>& cams)
{
    auto* w    = new QWidget(this);
    auto* vbox = new QVBoxLayout(w);
    vbox->setSpacing(8);
    vbox->setContentsMargins(10, 10, 10, 10);

    // ── Individual camera file exports ───────────────────────────────────────
    auto* camGrp  = new QGroupBox("Individual Camera Files", w);
    auto* camGrid = new QGridLayout(camGrp);
    camGrid->setHorizontalSpacing(24);
    camGrid->setVerticalSpacing(6);

    auto makeCheck = [&](const std::string& slot, const QString& label,
                         int row, int col) -> QCheckBox* {
        auto* cb = new QCheckBox(label, camGrp);
        bool present = cams.count(slot);
        cb->setChecked(present);
        cb->setEnabled(present);
        if (!present)
            cb->setToolTip(QString("No %1 camera footage in this trip").arg(label));
        camGrid->addWidget(cb, row, col);
        return cb;
    };

    int row = 0, col = 0;
    for (int i = 0; i < 4; ++i) {
        if (m_kSlot[i].empty()) continue;
        m_chkCam[i] = makeCheck(m_kSlot[i], m_kCam[i], row, col);
        if (++col > 1) { col = 0; ++row; }
    }

    auto* fmtLabel = new QLabel("Container:", camGrp);
    m_container    = new QComboBox(camGrp);
    m_container->addItems({"mp4", "mkv", "mov"});
    auto* fmtRow = new QHBoxLayout;
    fmtRow->setContentsMargins(0, 4, 0, 0);
    fmtRow->addWidget(fmtLabel);
    fmtRow->addWidget(m_container);
    fmtRow->addStretch();
    camGrid->addLayout(fmtRow, 2, 0, 1, 2);

    vbox->addWidget(camGrp);

    // ── Audio extract ─────────────────────────────────────────────────────────
    auto* audGrp  = new QGroupBox("Audio Extract", w);
    auto* audVbox = new QVBoxLayout(audGrp);
    audVbox->setSpacing(6);

    m_chkAudio = new QCheckBox("Extract audio track (independent of camera files)", audGrp);
    audVbox->addWidget(m_chkAudio);

    auto* optRow   = new QHBoxLayout;
    optRow->addSpacing(20);
    auto* camLabel = new QLabel("Camera:", audGrp);
    m_audioCamera  = new QComboBox(audGrp);
    for (int i = 0; i < 4; ++i)
        if (!m_kSlot[i].empty() && cams.count(m_kSlot[i]))
            m_audioCamera->addItem(m_kCam[i], QString::fromStdString(m_kSlot[i]));
    auto* fmtLbl2  = new QLabel("Format:", audGrp);
    m_audioFormat  = new QComboBox(audGrp);
    m_audioFormat->addItems({"m4a", "mp3", "aac"});
    optRow->addWidget(camLabel);
    optRow->addWidget(m_audioCamera);
    optRow->addSpacing(12);
    optRow->addWidget(fmtLbl2);
    optRow->addWidget(m_audioFormat);
    optRow->addStretch();
    audVbox->addLayout(optRow);

    auto updateAudio = [=](bool on) {
        camLabel->setEnabled(on);
        m_audioCamera->setEnabled(on);
        fmtLbl2->setEnabled(on);
        m_audioFormat->setEnabled(on);
    };
    updateAudio(false);
    connect(m_chkAudio, &QCheckBox::toggled, updateAudio);

    vbox->addWidget(audGrp);
    vbox->addStretch();
    return w;
}

// ---------------------------------------------------------------------------
// Output tab
// ---------------------------------------------------------------------------
QWidget* TripBuildDialog::makeOutputTab()
{
    auto* w    = new QWidget(this);
    auto* form = new QFormLayout(w);
    form->setContentsMargins(10, 10, 10, 10);
    form->setVerticalSpacing(8);

    ConfigManager config;
    config.loadSettings();
    std::string defaultDir = config.getDefaultExportDir();

    m_outputDir = new QLineEdit(w);
    m_outputDir->setPlaceholderText("(use footage source directory)");
    if (!defaultDir.empty())
        m_outputDir->setText(QString::fromStdString(defaultDir));

    auto* browseBtn = new QPushButton("\u2026", w);
    browseBtn->setFixedWidth(28);
    connect(browseBtn, &QPushButton::clicked, this, &TripBuildDialog::onBrowseOutput);

    auto* dirRow = new QHBoxLayout;
    dirRow->addWidget(m_outputDir);
    dirRow->addWidget(browseBtn);
    form->addRow("Destination:", dirRow);

    m_basename = new QLineEdit(w);
    m_basename->setPlaceholderText("(auto: date_time prefix)");
    form->addRow("Basename:", m_basename);

    return w;
}

// ---------------------------------------------------------------------------
// Action row
// ---------------------------------------------------------------------------
QWidget* TripBuildDialog::makeActionRow()
{
    auto* w    = new QWidget(this);
    auto* hbox = new QHBoxLayout(w);
    hbox->setContentsMargins(0, 4, 0, 0);

    m_chkVerbose = new QCheckBox("Verbose output", w);
    m_chkVerbose->setToolTip(
        "Show full ffmpeg output in the progress dialog.\n"
        "Useful for diagnosing build failures.");
    hbox->addWidget(m_chkVerbose);

    hbox->addStretch();

    auto* btnBuild = new QPushButton("Build Video ▶", w);
    btnBuild->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    btnBuild->setStyleSheet(
        "QPushButton { background: #0078d4; color: white; border: none;"
        "  border-radius: 4px; padding: 5px 16px; font-weight: bold; }"
        "QPushButton:hover { background: #006cbd; }"
        "QPushButton:pressed { background: #005aa3; }");
    connect(btnBuild, &QPushButton::clicked, this, &QDialog::accept);
    hbox->addWidget(btnBuild);

    return w;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------
void TripBuildDialog::onBrowseOutput()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select Output Directory",
        m_outputDir->text().isEmpty() ? QString() : m_outputDir->text(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty())
        m_outputDir->setText(dir);
}

void TripBuildDialog::onPreviewFrame()
{
    if (m_ffmpegPath.empty()) {
        QMessageBox::warning(this, "Preview", "ffmpeg path not configured.");
        return;
    }

    // ── Resolve a source video path for each quadrant ────────────────────────
    // Returns "" for disabled/None quadrants (placeholder rendered instead).
    auto resolveSource = [&](int i) -> QString {
        if (m_quadEnabled[i] && !m_quadEnabled[i]->isChecked()) return {};
        QString data = m_quadCombo[i] ? m_quadCombo[i]->currentData().toString()
                                      : QString(kNone);
        if (data == kNone) return {};
        if (data.startsWith("file:")) return data.mid(5);
        if (data == kBrowse)
            return m_quadPath[i] ? m_quadPath[i]->text().trimmed() : QString{};
        if (data.startsWith("camera:")) {
            std::string cam = data.mid(7).toStdString();
            for (const auto& seg : m_trip.segments) {
                auto it = seg.cameras.find(cam);
                if (it != seg.cameras.end() &&
                    !it->second.empty() && it->second != "-")
                    return QString::fromStdString(it->second);
            }
        }
        return {};
    };

    std::array<QString, 4> src;
    for (int i = 0; i < 4; ++i) src[i] = resolveSource(i);

    // ── Grab one frame per quadrant via ffmpeg ───────────────────────────────
    // Seek 2 s in to skip any black leader; -ss before -i = fast keyframe seek.
    auto grabFrame = [&](const QString& input, const QString& outFile) -> bool {
        QStringList args = {
            "-y", "-ss", "2",
            "-i", input,
            "-vframes", "1",
            "-f", "image2",
            outFile
        };
        QProcess proc;
        proc.start(QString::fromStdString(m_ffmpegPath), args);
        return proc.waitForFinished(10'000) && proc.exitCode() == 0;
    };

    // ── Composite into a 960×540 preview image ───────────────────────────────
    // Each cell: 480×270 — matches the 16:9 aspect of 1920×1080 source panels.
    constexpr int cellW = 480, cellH = 270;
    constexpr int outW  = cellW * 2, outH = cellH * 2;

    // xstack layout: TL=front TR=rear BL=right BR=left
    const QRect quadRect[4] = {
        { 0,     0,     cellW, cellH },  // [0] TL = front
        { cellW, 0,     cellW, cellH },  // [1] TR = rear
        { 0,     cellH, cellW, cellH },  // [2] BL = right
        { cellW, cellH, cellW, cellH },  // [3] BR = left
    };

    // ARGB32 so QPainter can alpha-blend the HUD overlay onto camera frames.
    QImage compositeImg(outW, outH, QImage::Format_ARGB32_Premultiplied);
    compositeImg.fill(QColor(0x1e, 0x1e, 0x2e));
    QPixmap composite = QPixmap::fromImage(compositeImg);
    QPainter painter(&composite);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    for (int i = 0; i < 4; ++i) {
        const QRect& r = quadRect[i];
        if (src[i].isEmpty()) {
            // Blank quadrant: dark tile + position label
            painter.fillRect(r, QColor(0x14, 0x14, 0x20));
            painter.setPen(QColor(0x40, 0x40, 0x60));
            painter.drawText(r, Qt::AlignCenter,
                             QString("%1\n(none)").arg(kPos[i]));
            continue;
        }

        QString tmp = QString("/tmp/clops_preview_%1.png").arg(i);
        bool ok = grabFrame(src[i], tmp);
        QPixmap frame(ok ? tmp : QString{});

        if (frame.isNull()) {
            painter.fillRect(r, QColor(0x2e, 0x14, 0x14));
            painter.setPen(QColor(0xc0, 0x50, 0x50));
            painter.drawText(r, Qt::AlignCenter, "frame grab failed");
        } else {
            // Fill cell, cropping to exact dimensions (source is already 16:9)
            painter.drawPixmap(r,
                frame.scaled(r.size(), Qt::KeepAspectRatioByExpanding,
                             Qt::SmoothTransformation),
                QRect(QPoint(0,0), r.size()));
        }
    }

    // ── Overlay (if enabled and has a source) ────────────────────────────────
    auto overlayPath = [&]() -> QString {
        if (!m_chkMapOverlay || !m_chkMapOverlay->isChecked()) return {};
        QString d = m_overlayCombo ? m_overlayCombo->currentData().toString()
                                   : QString(kNone);
        if (d == kNone) return {};
        if (d.startsWith("file:")) return d.mid(5);
        if (d == kBrowse && m_mapOverlayPath)
            return m_mapOverlayPath->text().trimmed();
        if (d.startsWith("camera:")) {
            std::string cam = d.mid(7).toStdString();
            for (const auto& seg : m_trip.segments) {
                auto it = seg.cameras.find(cam);
                if (it != seg.cameras.end() &&
                    !it->second.empty() && it->second != "-")
                    return QString::fromStdString(it->second);
            }
        }
        return {};
    }();

    if (!overlayPath.isEmpty()) {
        QString tmpOv = "/tmp/clops_preview_overlay.png";
        if (grabFrame(overlayPath, tmpOv)) {
            QPixmap ovFrame(tmpOv);
            if (!ovFrame.isNull()) {
                // Scale to 960×540 equivalent half (240×135 display overlay)
                int ovW = outW  * 960  / 3840;
                int ovH = outH  * 540  / 2160;
                QPixmap ov = ovFrame.scaled(ovW, ovH,
                                            Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
                QRect ovRect(QPoint(0,0), ov.size());
                ovRect.moveCenter({outW / 2, outH / 2});
                painter.setOpacity(0.9);
                painter.drawPixmap(ovRect, ov);
                painter.setOpacity(1.0);
            }
        }
        QFile::remove(tmpOv);
    }

    // ── HUD preview (full-frame, alpha-preserved) ────────────────────────────
    auto hudPreviewPath = [&]() -> QString {
        if (!m_chkHudOverlay || !m_chkHudOverlay->isChecked()) return {};
        QString d = m_hudCombo ? m_hudCombo->currentData().toString() : QString(kNone);
        if (d == kNone) return {};
        if (d.startsWith("file:")) return d.mid(5);
        if (d == kBrowse && m_hudPath) return m_hudPath->text().trimmed();
        return {};
    }();

    if (!hudPreviewPath.isEmpty()) {
        QString tmpHud = "/tmp/clops_preview_hud.png";
        // Grab with alpha preserved (-pix_fmt rgba keeps the alpha channel in PNG)
        QStringList hudArgs = {
            "-y", "-ss", "2",
            "-c:v", "libvpx-vp9",   // force software VP9 decoder — only one that reads alpha track
            "-i", hudPreviewPath,
            "-vframes", "1",
            "-vf", "format=rgba",
            "-f", "image2",
            tmpHud
        };
        QProcess hudProc;
        hudProc.start(QString::fromStdString(m_ffmpegPath), hudArgs);
        if (hudProc.waitForFinished(10'000) && hudProc.exitCode() == 0) {
            QPixmap hudFrame(tmpHud);
            if (!hudFrame.isNull()) {
                // Scale HUD to match the preview canvas (it's a full-frame overlay)
                QPixmap hud = hudFrame.scaled(outW, outH,
                                              Qt::IgnoreAspectRatio,
                                              Qt::SmoothTransformation);
                painter.drawPixmap(0, 0, hud);  // alpha from PNG is applied automatically
            }
        }
        QFile::remove(tmpHud);
    }

    painter.end();

    // ── Show in a simple dialog ───────────────────────────────────────────────
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle("Collage Preview \u2014 frame grab");
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setFixedSize(outW + 24, outH + 64);

    auto* vbox = new QVBoxLayout(dlg);
    vbox->setContentsMargins(12, 12, 12, 8);
    auto* lbl = new QLabel(dlg);
    lbl->setPixmap(composite);
    lbl->setAlignment(Qt::AlignCenter);
    vbox->addWidget(lbl);

    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(bbox, &QDialogButtonBox::rejected, dlg, &QDialog::accept);
    vbox->addWidget(bbox);

    dlg->exec();

    // Cleanup temp frame files
    for (int i = 0; i < 4; ++i)
        QFile::remove(QString("/tmp/clops_preview_%1.png").arg(i));
}

void TripBuildDialog::onBrowseOverlay()
{
    QString file = QFileDialog::getOpenFileName(
        this, "Select Overlay Video",
        m_mapOverlayPath->text().isEmpty() ? QString() : m_mapOverlayPath->text(),
        "Video Files (*.mp4 *.mkv *.mov);;All Files (*)");
    if (!file.isEmpty())
        m_mapOverlayPath->setText(file);
}

// ---------------------------------------------------------------------------
// Collect results
// ---------------------------------------------------------------------------
VideoOptions TripBuildDialog::buildOptions() const
{
    VideoOptions opts;

    // ── Per-camera files ─────────────────────────────────────────────────────
    // Per-camera files: map dynamic slot names to VideoOptions build flags
    opts.buildFront = opts.buildRear = opts.buildLeft = opts.buildRight = false;
    for (int i = 0; i < 4; ++i) {
        if (!m_chkCam[i] || !m_chkCam[i]->isChecked()) continue;
        const std::string& s = m_kSlot[i];
        if      (s == "front") opts.buildFront = true;
        else if (s == "rear")  opts.buildRear  = true;
        else if (s == "left")  opts.buildLeft  = true;
        else if (s == "right") opts.buildRight = true;
    }
    opts.containerFormat = m_container ? m_container->currentText().toStdString() : "mp4";

    // ── Collage ──────────────────────────────────────────────────────────────
    opts.buildCollage4K   = m_chk4K   && m_chk4K->isChecked();
    opts.buildCollage1080 = m_chk1080 && m_chk1080->isChecked();

    // Quadrant source assignments → external slot replacements / blank slots
    for (int i = 0; i < 4; ++i) {
        if (!m_quadCombo[i]) continue;
        std::string slotName = m_kSlot[i];

        // Disabled checkbox or "None" combo → logo morph loop (falls back to black)
        bool disabled = m_quadEnabled[i] && !m_quadEnabled[i]->isChecked();
        QString data  = m_quadCombo[i]->currentData().toString();
        if (disabled || data == kNone) {
            std::string morph = logoMorphPath();
            if (!morph.empty()) {
                VideoOptions::ExternalSlot es;
                es.path     = morph;
                es.replaces = slotName;
                es.loop     = true;
                opts.externalSlots.push_back(std::move(es));
            } else {
                opts.blankSlots.insert(slotName);
            }
            continue;
        }

        if (data.startsWith("file:")) {
            VideoOptions::ExternalSlot es;
            es.path     = data.mid(5).toStdString();
            es.replaces = slotName;
            opts.externalSlots.push_back(std::move(es));
        } else if (data == kBrowse) {
            QString p = m_quadPath[i] ? m_quadPath[i]->text().trimmed() : QString();
            if (!p.isEmpty()) {
                VideoOptions::ExternalSlot es;
                es.path     = p.toStdString();
                es.replaces = slotName;
                opts.externalSlots.push_back(std::move(es));
            }
        } else if (data.startsWith("camera:")) {
            std::string selectedCam = data.mid(7).toStdString();
            if (selectedCam != slotName)
                opts.cameraRemap[slotName] = selectedCam;
            // identity mapping (selectedCam == slotName) = natural flow, no entry needed
        }
    }

    // Collage audio source: quadrant index → slot name → audioSource string
    if (m_audioQuad) {
        int qi = m_audioQuad->currentIndex();
        if (qi >= 0 && qi < 4)
            opts.audioSource = m_kSlot[qi];
    }

    // ── Audio extract ────────────────────────────────────────────────────────
    opts.buildAudio = m_chkAudio && m_chkAudio->isChecked();
    if (opts.buildAudio && m_audioCamera && m_audioFormat) {
        opts.audioExtractCamera = m_audioCamera->currentData().toString().toStdString();
        opts.audioExtractFormat = m_audioFormat->currentText().toStdString();
    }

    // ── HUD overlay (full-screen, VP9 alpha) ─────────────────────────────────
    if (m_chkHudOverlay && m_chkHudOverlay->isChecked() && m_hudCombo) {
        QString data = m_hudCombo->currentData().toString();
        if (data.startsWith("file:"))
            opts.hudOverlayPath = data.mid(5).toStdString();
        else if (data == kBrowse && m_hudPath)
            opts.hudOverlayPath = m_hudPath->text().trimmed().toStdString();
        // kNone → leave hudOverlayPath empty (no HUD)
    }

    // ── Map overlay ──────────────────────────────────────────────────────────
    if (m_chkMapOverlay && m_chkMapOverlay->isChecked() && m_overlayCombo) {
        QString data = m_overlayCombo->currentData().toString();
        if (data.startsWith("camera:")) {
            opts.overlayCamera = data.mid(7).toStdString();
        } else if (data.startsWith("file:")) {
            opts.mapOverlayPath = data.mid(5).toStdString();
        } else if (data == kBrowse && m_mapOverlayPath) {
            opts.mapOverlayPath = m_mapOverlayPath->text().trimmed().toStdString();
        } else if (data == kNone) {
            // Logo morph as overlay — useful as a looping video watermark
            opts.mapOverlayPath = logoMorphPath();
            opts.mapOverlayLoop = true;
        }
        // VP9/WebM overlays (transparent dashboard) need libvpx-vp9 + yuva420p.
        if (!opts.mapOverlayPath.empty()) {
            QString p = QString::fromStdString(opts.mapOverlayPath);
            opts.mapOverlayAlpha = p.endsWith(".webm", Qt::CaseInsensitive);
        }
        if (m_overlayPos)
            opts.overlayPosition = m_overlayPos->currentData().toString().toStdString();
    }

    // ── Output ───────────────────────────────────────────────────────────────
    opts.outputDir        = m_outputDir ? m_outputDir->text().trimmed().toStdString() : "";
    opts.basenameOverride = m_basename  ? m_basename->text().trimmed().toStdString()  : "";

    // Fall back to footage root if no output dir specified
    if (opts.outputDir.empty()) {
        namespace fs = std::filesystem;
        for (const auto& seg : m_trip.segments) {
            for (const auto& [slot, path] : seg.cameras) {
                if (path.empty() || path == "-") continue;
                std::error_code ec;
                auto p = fs::path(path).parent_path().parent_path();
                if (fs::is_directory(p, ec) && !ec)
                    opts.outputDir = p.string();
                break;
            }
            if (!opts.outputDir.empty()) break;
        }
    }

    opts.verbose         = m_chkVerbose && m_chkVerbose->isChecked();
    opts.ffmpegPath      = m_ffmpegPath;
    opts.encode          = m_encode;
    opts.sourcePath      = m_manifest.path;
    opts.manifestId      = m_manifest.id;
    opts.parallelConcats = true;

    // Pull camera start offsets from the manifest's camera profile.
    // measureCameraOffsets() writes them into the manifest after GPS extraction,
    // so they're available here without a rescan.
    {
        ConfigManager cfg;
        cfg.loadSettings();
        CameraProfile p = cfg.getManifestProfile(m_manifest.path);
        for (const auto& [k, v] : p.cameraStartOffsets)
            opts.cameraStartOffsets[k] = v;
    }

    return opts;
}

bool TripBuildDialog::processNow() const
{
    return true;
}
// SN: 00112
