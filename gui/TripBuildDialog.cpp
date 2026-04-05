// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "TripBuildDialog.h"
#include <filesystem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLabel>
#include <QFileDialog>
#include <QDialogButtonBox>

using namespace Pathmux;

// ---------------------------------------------------------------------------
// Collect camera slot names that have at least one real segment path.
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
    setMinimumWidth(520);
    setStyleSheet(
        "QGroupBox {"
        "  border: 1px solid #909090;"
        "  border-radius: 4px;"
        "  margin-top: 8px;"
        "  font-weight: bold;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  subcontrol-position: top left;"
        "  left: 8px;"
        "  padding: 0 4px;"
        "}"
    );

    ConfigManager config;
    config.loadSettings();
    m_ffmpegPath = config.getFfmpegPath();
    m_encode     = config.getEncodeSettings();

    auto* vbox = new QVBoxLayout(this);
    vbox->setSpacing(8);
    vbox->setContentsMargins(12, 12, 12, 12);

    auto cams = presentCameras(trip);

    vbox->addWidget(makeCameraGroup(cams));
    vbox->addWidget(makeCollageGroup(cams));
    vbox->addWidget(makeAudioGroup(cams));
    vbox->addWidget(makeOutputGroup(config.getDefaultExportDir()));
    vbox->addWidget(makeActionRow());
    vbox->addStretch();

    auto* bbox = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    bbox->button(QDialogButtonBox::Ok)->setText("Build \u25b6");
    connect(bbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    vbox->addWidget(bbox);
}

// ---------------------------------------------------------------------------
// Camera Files section
// ---------------------------------------------------------------------------
QGroupBox* TripBuildDialog::makeCameraGroup(const std::set<std::string>& cams)
{
    auto* box  = new QGroupBox("Camera Files", this);
    auto* grid = new QGridLayout(box);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(6);

    auto makeCheck = [&](const std::string& slot, const QString& label,
                         int row, int col) -> QCheckBox* {
        auto* cb = new QCheckBox(label, box);
        bool present = cams.count(slot);
        cb->setChecked(present);
        cb->setEnabled(present);
        if (!present)
            cb->setToolTip(QString("No %1 camera footage in this trip").arg(label));
        grid->addWidget(cb, row, col);
        return cb;
    };

    m_chkFront = makeCheck("front", "Front", 0, 0);
    m_chkRear  = makeCheck("rear",  "Rear",  0, 1);
    m_chkLeft  = makeCheck("left",  "Left",  1, 0);
    m_chkRight = makeCheck("right", "Right", 1, 1);

    auto* fmtLabel = new QLabel("Container:", box);
    m_container    = new QComboBox(box);
    m_container->addItems({"mp4", "mkv", "mov"});
    auto* fmtRow = new QHBoxLayout;
    fmtRow->setContentsMargins(0, 4, 0, 0);
    fmtRow->addWidget(fmtLabel);
    fmtRow->addWidget(m_container);
    fmtRow->addStretch();
    grid->addLayout(fmtRow, 2, 0, 1, 2);

    return box;
}

// ---------------------------------------------------------------------------
// Collage section
// ---------------------------------------------------------------------------
QGroupBox* TripBuildDialog::makeCollageGroup(const std::set<std::string>& cams)
{
    auto* box  = new QGroupBox("Collage", this);
    auto* vbox = new QVBoxLayout(box);
    vbox->setSpacing(6);

    // Resolution row
    auto* resRow = new QHBoxLayout;
    m_chk4K   = new QCheckBox("4K  (3840\u00d72160 H.265)", box);
    m_chk1080 = new QCheckBox("1080p downscale", box);
    m_chk4K->setChecked(true);
    resRow->addWidget(m_chk4K);
    resRow->addSpacing(24);
    resRow->addWidget(m_chk1080);
    resRow->addStretch();
    vbox->addLayout(resRow);

    // Collage audio source
    auto* audioRow   = new QHBoxLayout;
    auto* audioLabel = new QLabel("Collage audio:", box);
    audioRow->addWidget(audioLabel);
    audioRow->addSpacing(8);

    auto* audioGroup = new QButtonGroup(box);
    auto makeRadio = [&](const std::string& slot, const QString& label) -> QRadioButton* {
        auto* rb = new QRadioButton(label, box);
        rb->setEnabled(cams.count(slot) > 0);
        audioGroup->addButton(rb);
        audioRow->addWidget(rb);
        return rb;
    };
    m_rbLeft  = makeRadio("left",  "Left");
    m_rbRight = makeRadio("right", "Right");
    m_rbFront = makeRadio("front", "Front");
    m_rbRear  = makeRadio("rear",  "Rear");
    audioRow->addStretch();

    // Default selection: left → right → front → rear
    if      (m_rbLeft->isEnabled())  m_rbLeft->setChecked(true);
    else if (m_rbRight->isEnabled()) m_rbRight->setChecked(true);
    else if (m_rbFront->isEnabled()) m_rbFront->setChecked(true);
    else if (m_rbRear->isEnabled())  m_rbRear->setChecked(true);

    vbox->addLayout(audioRow);
    return box;
}

// ---------------------------------------------------------------------------
// Audio Extract section
// ---------------------------------------------------------------------------
QGroupBox* TripBuildDialog::makeAudioGroup(const std::set<std::string>& cams)
{
    auto* box  = new QGroupBox("Audio Extract", this);
    auto* vbox = new QVBoxLayout(box);
    vbox->setSpacing(6);

    m_chkAudio = new QCheckBox("Extract audio track (independent of camera files)", box);
    vbox->addWidget(m_chkAudio);

    auto* optRow   = new QHBoxLayout;
    optRow->addSpacing(20);

    auto* camLabel = new QLabel("Camera:", box);
    m_audioCamera  = new QComboBox(box);
    for (const char* s : {"left", "right", "front", "rear"})
        if (cams.count(s)) m_audioCamera->addItem(QString::fromLatin1(s));

    auto* fmtLabel = new QLabel("Format:", box);
    m_audioFormat  = new QComboBox(box);
    m_audioFormat->addItems({"m4a", "mp3", "aac"});

    optRow->addWidget(camLabel);
    optRow->addWidget(m_audioCamera);
    optRow->addSpacing(12);
    optRow->addWidget(fmtLabel);
    optRow->addWidget(m_audioFormat);
    optRow->addStretch();
    vbox->addLayout(optRow);

    // Sub-controls enabled only when checkbox is on
    auto updateAudio = [=](bool on) {
        camLabel->setEnabled(on);
        m_audioCamera->setEnabled(on);
        fmtLabel->setEnabled(on);
        m_audioFormat->setEnabled(on);
    };
    updateAudio(false);
    connect(m_chkAudio, &QCheckBox::toggled, updateAudio);

    return box;
}

// ---------------------------------------------------------------------------
// Output section
// ---------------------------------------------------------------------------
QGroupBox* TripBuildDialog::makeOutputGroup(const std::string& defaultExportDir)
{
    auto* box  = new QGroupBox("Output", this);
    auto* form = new QFormLayout(box);
    form->setContentsMargins(8, 8, 8, 8);
    form->setVerticalSpacing(6);

    m_outputDir = new QLineEdit(box);
    m_outputDir->setPlaceholderText("(use default export directory)");
    if (!defaultExportDir.empty())
        m_outputDir->setText(QString::fromStdString(defaultExportDir));

    auto* browseBtn = new QPushButton("\u2026", box);
    browseBtn->setFixedWidth(28);
    connect(browseBtn, &QPushButton::clicked, this, &TripBuildDialog::onBrowse);

    auto* dirRow = new QHBoxLayout;
    dirRow->addWidget(m_outputDir);
    dirRow->addWidget(browseBtn);
    form->addRow("Destination:", dirRow);

    m_basename = new QLineEdit(box);
    m_basename->setPlaceholderText("(auto: date_time prefix)");
    form->addRow("Basename:", m_basename);

    return box;
}

// ---------------------------------------------------------------------------
// Action toggle row:  [ Process Now | Add to Batch Queue (coming soon) ]
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
    return w;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------
void TripBuildDialog::onBrowse()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select Output Directory",
        m_outputDir->text().isEmpty() ? QString() : m_outputDir->text(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty())
        m_outputDir->setText(dir);
}

// ---------------------------------------------------------------------------
// Collect results
// ---------------------------------------------------------------------------
VideoOptions TripBuildDialog::buildOptions() const
{
    VideoOptions opts;

    opts.buildFront      = m_chkFront && m_chkFront->isChecked();
    opts.buildRear       = m_chkRear  && m_chkRear->isChecked();
    opts.buildLeft       = m_chkLeft  && m_chkLeft->isChecked();
    opts.buildRight      = m_chkRight && m_chkRight->isChecked();
    opts.containerFormat = m_container ? m_container->currentText().toStdString() : "mp4";

    opts.buildCollage4K   = m_chk4K   && m_chk4K->isChecked();
    opts.buildCollage1080 = m_chk1080 && m_chk1080->isChecked();

    if      (m_rbLeft  && m_rbLeft->isChecked())  opts.audioSource = "left";
    else if (m_rbRight && m_rbRight->isChecked()) opts.audioSource = "right";
    else if (m_rbFront && m_rbFront->isChecked()) opts.audioSource = "front";
    else if (m_rbRear  && m_rbRear->isChecked())  opts.audioSource = "rear";

    opts.buildAudio = m_chkAudio && m_chkAudio->isChecked();
    if (opts.buildAudio && m_audioCamera && m_audioFormat) {
        opts.audioExtractCamera = m_audioCamera->currentText().toStdString();
        opts.audioExtractFormat = m_audioFormat->currentText().toStdString();
    }

    opts.outputDir        = m_outputDir ? m_outputDir->text().trimmed().toStdString() : "";
    opts.basenameOverride = m_basename  ? m_basename->text().trimmed().toStdString()  : "";

    // If no output directory was specified, default to the footage source
    // directory so files land next to the input footage instead of in the
    // app's CWD (which may be a non-writable Program Files directory).
    if (opts.outputDir.empty()) {
        namespace fs = std::filesystem;
        for (const auto& seg : m_trip.segments) {
            for (const auto& [slot, path] : seg.cameras) {
                if (path.empty() || path == "-") continue;
                // Segment path: <footage>/Front/20260225_0F.ts
                // Parent of parent = footage root
                std::error_code ec;
                auto p = fs::path(path).parent_path().parent_path();
                if (fs::is_directory(p, ec) && !ec)
                    opts.outputDir = p.string();
                break;
            }
            if (!opts.outputDir.empty()) break;
        }
    }

    opts.ffmpegPath      = m_ffmpegPath;
    opts.encode          = m_encode;
    opts.sourcePath      = m_manifest.path;
    opts.manifestId      = m_manifest.id;
    opts.parallelConcats = true;   // stream-copy is disk-bound; run cameras concurrently

    return opts;
}

bool TripBuildDialog::processNow() const
{
    return m_btnNow && m_btnNow->isChecked();
}
// SN: 00093
