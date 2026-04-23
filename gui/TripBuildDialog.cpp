// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "TripBuildDialog.h"
#include "LogoMorphWidget.h"
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
#include <QButtonGroup>
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

using namespace Pathmux;

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
    QString tmp = QDir::temp().filePath("pm_logo_morph.mp4");
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
    setWindowTitle(QString("Build Trip \u2014 %1  %2")
        .arg(QString::fromStdString(trip.date))
        .arg(QString::fromStdString(trip.startTime)));
    setMinimumWidth(560);

    ConfigManager config;
    config.loadSettings();
    m_ffmpegPath = config.getFfmpegPath();
    m_encode     = config.getEncodeSettings();

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

    auto* bbox = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    bbox->button(QDialogButtonBox::Ok)->setText("Build \u25b6");
    connect(bbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
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

    // ── Audio source ─────────────────────────────────────────────────────────
    auto* audioRow   = new QHBoxLayout;
    auto* audioLabel = new QLabel("Collage audio from:", w);
    m_audioQuad      = new QComboBox(w);
    for (int i = 0; i < 4; ++i)
        m_audioQuad->addItem(
            QString("%1 (%2)").arg(kPos[i]).arg(kCam[i]));
    m_audioQuad->setCurrentIndex(3);
    if (!cams.count("left") && cams.count("front"))
        m_audioQuad->setCurrentIndex(0);
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
        "QCheckBox { color: #a0a0d0; font-size: 8pt; border: none; }"
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
    vbox->addWidget(title);

    m_chkMapOverlay = new QCheckBox("Enable", cell);
    vbox->addWidget(m_chkMapOverlay);

    // Source combo — same options as quadrants: cameras, map/dash files, external, none
    m_overlayCombo = new QComboBox(cell);
    vbox->addWidget(m_overlayCombo);

    auto cams = presentCameras(m_trip);
    for (int k = 0; k < 4; ++k) {
        if (!cams.count(kSlot[k])) continue;
        m_overlayCombo->addItem(QString("Camera: %1").arg(kCam[k]),
                                camData(kSlot[k]));
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
    m_overlayCombo->addItem("External video\u2026", QString(kBrowse));
    m_overlayCombo->addItem("None", QString(kNone));

    // Pre-select first map video if one exists, else default to first item
    {
        namespace fs = std::filesystem;
        std::string candidate = m_manifest.path
            + "/pm_trip_" + m_trip.id + "_map.mp4";
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

    auto* hint = new QLabel("960\u00d7540 \u00b7 centred", cell);
    hint->setAlignment(Qt::AlignCenter);
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
        m_overlayFileRow->setEnabled(on);
        m_overlayMorph->setEnabled(on);
    };
    updateOverlay(false);
    connect(m_chkMapOverlay, &QCheckBox::toggled, updateOverlay);

    return cell;
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

    // All present cameras — any camera can fill any quadrant
    // kSlot order: front, rear, right, left (matches kCam display names)
    const char* nativeCam = kSlot[idx];
    int nativeItemIdx = -1;
    for (int k = 0; k < 4; ++k) {
        if (!cams.count(kSlot[k])) continue;
        QString label = QString("Camera: %1").arg(kCam[k]);
        if (strcmp(kSlot[k], nativeCam) == 0) {
            nativeItemIdx = combo->count();
            label += " \u2605";  // star marks the native/default camera for this position
        }
        combo->addItem(label, camData(kSlot[k]));
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

    m_chkFront = makeCheck("front", "Front", 0, 0);
    m_chkRear  = makeCheck("rear",  "Rear",  0, 1);
    m_chkLeft  = makeCheck("left",  "Left",  1, 0);
    m_chkRight = makeCheck("right", "Right", 1, 1);

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
    for (const char* s : {"left", "right", "front", "rear"})
        if (cams.count(s)) m_audioCamera->addItem(QString::fromLatin1(s));
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

    hbox->addWidget(new QLabel("Action:", w));
    hbox->addSpacing(8);

    m_btnNow   = new QPushButton("Process Now", w);
    m_btnQueue = new QPushButton("Add to Batch Queue", w);

    m_btnNow->setCheckable(true);
    m_btnNow->setChecked(true);
    m_btnQueue->setCheckable(true);
    m_btnQueue->setEnabled(false);
    m_btnQueue->setToolTip("Batch queue — coming in a future release");

    auto* grp = new QButtonGroup(w);
    grp->addButton(m_btnNow);
    grp->addButton(m_btnQueue);
    grp->setExclusive(true);

    m_btnNow->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #0078d4; border-right: none;"
        "  border-radius: 4px 0 0 4px; padding: 4px 14px; min-width: 100px; }"
        "QPushButton:checked { background: #0078d4; color: white; }"
        "QPushButton:!checked { background: white; color: #0078d4; }"
    );
    m_btnQueue->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #c8c8c8;"
        "  border-radius: 0 4px 4px 0; padding: 4px 14px; min-width: 140px;"
        "  background: #f0f0f0; color: #a0a0a0; }"
    );

    hbox->addWidget(m_btnNow);
    hbox->addWidget(m_btnQueue);
    hbox->addStretch();

    m_chkVerbose = new QCheckBox("Verbose output", w);
    m_chkVerbose->setToolTip(
        "Show full ffmpeg output in the progress dialog.\n"
        "Useful for diagnosing build failures.");
    hbox->addWidget(m_chkVerbose);

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

    QPixmap composite(outW, outH);
    composite.fill(QColor(0x1e, 0x1e, 0x2e));
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

        QString tmp = QString("/tmp/pm_preview_%1.png").arg(i);
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
        QString tmpOv = "/tmp/pm_preview_overlay.png";
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
        QFile::remove(QString("/tmp/pm_preview_%1.png").arg(i));
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
    opts.buildFront      = m_chkFront && m_chkFront->isChecked();
    opts.buildRear       = m_chkRear  && m_chkRear->isChecked();
    opts.buildLeft       = m_chkLeft  && m_chkLeft->isChecked();
    opts.buildRight      = m_chkRight && m_chkRight->isChecked();
    opts.containerFormat = m_container ? m_container->currentText().toStdString() : "mp4";

    // ── Collage ──────────────────────────────────────────────────────────────
    opts.buildCollage4K   = m_chk4K   && m_chk4K->isChecked();
    opts.buildCollage1080 = m_chk1080 && m_chk1080->isChecked();

    // Quadrant source assignments → external slot replacements / blank slots
    for (int i = 0; i < 4; ++i) {
        if (!m_quadCombo[i]) continue;
        std::string slotName = kSlot[i];

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
            opts.audioSource = kSlot[qi];
    }

    // ── Audio extract ────────────────────────────────────────────────────────
    opts.buildAudio = m_chkAudio && m_chkAudio->isChecked();
    if (opts.buildAudio && m_audioCamera && m_audioFormat) {
        opts.audioExtractCamera = m_audioCamera->currentText().toStdString();
        opts.audioExtractFormat = m_audioFormat->currentText().toStdString();
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

    return opts;
}

bool TripBuildDialog::processNow() const
{
    return m_btnNow && m_btnNow->isChecked();
}
// SN: 00101
