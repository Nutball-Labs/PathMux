// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "SettingsDialog.h"
#include <QTabWidget>
#include <QScrollArea>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QScrollArea>

using namespace Pathmux;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QWidget* scrollWrap(QWidget* inner)
{
    auto* sa = new QScrollArea;
    sa->setWidget(inner);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    return sa;
}

static int comboFind(QComboBox* cb, const QString& text)
{
    int idx = cb->findText(text);
    return idx >= 0 ? idx : 0;
}

// ---------------------------------------------------------------------------
// SettingsDialog
// ---------------------------------------------------------------------------

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("PathMux Settings");
    setMinimumSize(600, 500);
    resize(700, 550);

    m_config.loadSettings();
    m_locations = m_config.loadLocations();

    auto* tabs = new QTabWidget(this);
    buildGeneralTab(tabs);
    buildEncoderTab(tabs);
    buildHostTab(tabs);
    buildKmlTab(tabs);
    buildLocationsTab(tabs);

    auto* bbox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bbox, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccepted);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* vlay = new QVBoxLayout(this);
    vlay->addWidget(tabs, 1);
    vlay->addWidget(bbox);

    loadFromConfig();
}

// ---------------------------------------------------------------------------
// General tab
// ---------------------------------------------------------------------------

void SettingsDialog::buildGeneralTab(QTabWidget* tabs)
{
    auto* w    = new QWidget;
    auto* form = new QFormLayout(w);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setContentsMargins(12, 12, 12, 12);
    form->setSpacing(8);

    // Trip detection
    m_gapThreshold = new QSpinBox; m_gapThreshold->setRange(30, 86400); m_gapThreshold->setSuffix(" s");
    form->addRow("Trip gap threshold:", m_gapThreshold);
    form->addRow("", new QLabel("<i>Seconds of inactivity before a new trip starts (default 900 = 15 min)</i>"));

    m_fuzzyWindow = new QSpinBox; m_fuzzyWindow->setRange(1, 30); m_fuzzyWindow->setSuffix(" s");
    form->addRow("Camera fuzzy window:", m_fuzzyWindow);
    form->addRow("", new QLabel("<i>Timestamp tolerance for matching side cameras to Front (±N seconds)</i>"));

    m_gpsSkip = new QSpinBox; m_gpsSkip->setRange(0, 300); m_gpsSkip->setSuffix(" s");
    form->addRow("GPS cold-start skip:", m_gpsSkip);
    form->addRow("", new QLabel("<i>Seconds to skip at start of first segment — avoids GPS lock lag</i>"));

    auto* sep1 = new QFrame; sep1->setFrameShape(QFrame::HLine); form->addRow(sep1);

    // Display
    m_timestampFmt = new QComboBox;
    m_timestampFmt->addItems({"YYYYMMDD-HHMMSS", "YYYY-MM-DD_HH-MM-SS", "ISO8601"});
    form->addRow("Timestamp format:", m_timestampFmt);

    m_timeDisplay = new QComboBox;
    m_timeDisplay->addItems({"24-hour", "12-hour"});
    form->addRow("Time display:", m_timeDisplay);

    m_useImperial = new QCheckBox("Use imperial units (mi, mph, ft)");
    form->addRow("Units:", m_useImperial);

    auto* sep2 = new QFrame; sep2->setFrameShape(QFrame::HLine); form->addRow(sep2);

    // Output
    m_videoFormat = new QComboBox;
    m_videoFormat->addItems({"mp4", "mkv", "mov", "avi", "mpg"});
    form->addRow("Video output format:", m_videoFormat);

    m_audioSource = new QComboBox;
    m_audioSource->addItems({"left", "right", "front", "rear"});
    form->addRow("Default audio source:", m_audioSource);
    form->addRow("", new QLabel("<i>left = driver side (LHD), right = driver side (RHD)</i>"));

    auto* sep3 = new QFrame; sep3->setFrameShape(QFrame::HLine); form->addRow(sep3);

    // Misc
    m_logLevel = new QComboBox;
    m_logLevel->addItems({"off", "normal", "debug"});
    form->addRow("Log level:", m_logLevel);

    m_activeProfile = new QLineEdit;
    form->addRow("Camera profile:", m_activeProfile);
    form->addRow("", new QLabel("<i>Profile ID (stem of ~/.config/pathmux/profiles/<id>.json)</i>"));

    tabs->addTab(scrollWrap(w), "General");
}

// ---------------------------------------------------------------------------
// Encoder tab
// ---------------------------------------------------------------------------

void SettingsDialog::buildEncoderTab(QTabWidget* tabs)
{
    auto* w    = new QWidget;
    auto* form = new QFormLayout(w);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setContentsMargins(12, 12, 12, 12);
    form->setSpacing(8);

    // Preset row
    auto* presetRow = new QHBoxLayout;
    m_preset = new QComboBox;
    m_preset->addItems({"cpu", "nvenc", "qsv", "vaapi"});
    presetRow->addWidget(m_preset, 1);
    auto* applyBtn = new QPushButton("Apply Preset");
    presetRow->addWidget(applyBtn);
    connect(applyBtn, &QPushButton::clicked, this, &SettingsDialog::onApplyPreset);
    form->addRow("Preset:", presetRow);
    form->addRow("", new QLabel("<i>Applying a preset overwrites all fields below with known-good values.</i>"));

    auto* note = new QLabel(
        "<small>These settings are saved to the host-specific file "
        "(pathmux_&lt;hostname&gt;.json) and override the base config on this machine.</small>");
    note->setWordWrap(true);
    form->addRow(note);

    auto* sep1 = new QFrame; sep1->setFrameShape(QFrame::HLine); form->addRow(sep1);

    m_hwDevice = new QLineEdit; m_hwDevice->setPlaceholderText("e.g. cuda  or  qsv:hw");
    form->addRow("HW device:", m_hwDevice);

    m_hwDeviceType = new QComboBox;
    m_hwDeviceType->addItems({"none", "cuda", "qsv", "vaapi"});
    form->addRow("HW device type:", m_hwDeviceType);

    auto* sep2 = new QFrame; sep2->setFrameShape(QFrame::HLine); form->addRow(sep2);

    m_normEncoder = new QLineEdit; m_normEncoder->setPlaceholderText("e.g. libx264  h264_nvenc  h264_qsv");
    form->addRow("Normalise encoder:", m_normEncoder);

    m_collageEncoder = new QLineEdit; m_collageEncoder->setPlaceholderText("e.g. libx265  hevc_nvenc  hevc_qsv");
    form->addRow("Collage encoder:", m_collageEncoder);

    m_downEncoder = new QLineEdit; m_downEncoder->setPlaceholderText("e.g. libx264  h264_nvenc");
    form->addRow("Downscale encoder:", m_downEncoder);

    m_pixFmt = new QComboBox;
    m_pixFmt->addItems({"yuv420p", "nv12", "yuv420p10le"});
    m_pixFmt->setEditable(true);
    form->addRow("Pixel format:", m_pixFmt);

    auto* sep3 = new QFrame; sep3->setFrameShape(QFrame::HLine); form->addRow(sep3);

    m_normQuality = new QSpinBox; m_normQuality->setRange(0, 51);
    form->addRow("Norm quality (CRF/QP):", m_normQuality);

    m_collageQuality = new QSpinBox; m_collageQuality->setRange(0, 51);
    form->addRow("Collage quality (CRF/QP):", m_collageQuality);

    m_downQuality = new QSpinBox; m_downQuality->setRange(0, 51);
    form->addRow("Downscale quality (CRF/QP):", m_downQuality);
    form->addRow("", new QLabel("<i>Lower = better quality, larger file. libx264 default ~23; NVENC constqp ~24.</i>"));

    auto* sep4 = new QFrame; sep4->setFrameShape(QFrame::HLine); form->addRow(sep4);

    m_extraNorm = new QLineEdit; m_extraNorm->setPlaceholderText("-preset fast");
    form->addRow("Extra norm args:", m_extraNorm);

    m_extraCollage = new QLineEdit; m_extraCollage->setPlaceholderText("-preset slow");
    form->addRow("Extra collage args:", m_extraCollage);

    m_extraDown = new QLineEdit; m_extraDown->setPlaceholderText("-preset fast");
    form->addRow("Extra downscale args:", m_extraDown);

    tabs->addTab(scrollWrap(w), "Encoder");
}

// ---------------------------------------------------------------------------
// Host tab
// ---------------------------------------------------------------------------

void SettingsDialog::buildHostTab(QTabWidget* tabs)
{
    auto* w    = new QWidget;
    auto* form = new QFormLayout(w);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setContentsMargins(12, 12, 12, 12);
    form->setSpacing(8);

    m_hostFileLabel = new QLabel;
    m_hostFileLabel->setWordWrap(true);
    form->addRow(m_hostFileLabel);

    auto* intro = new QLabel(
        "<i>These fields override the base config for this machine only. "
        "Leave a field empty to fall back to the base setting.</i>");
    intro->setWordWrap(true);
    form->addRow(intro);

    auto* sep0 = new QFrame; sep0->setFrameShape(QFrame::HLine); form->addRow(sep0);

    // ffmpeg path row
    auto* ffRow = new QHBoxLayout;
    m_hostFfmpeg = new QLineEdit; m_hostFfmpeg->setPlaceholderText("ffmpeg  (system PATH)");
    ffRow->addWidget(m_hostFfmpeg, 1);
    auto* ffBrowse = new QPushButton("Browse…");
    ffRow->addWidget(ffBrowse);
    connect(ffBrowse, &QPushButton::clicked, this, &SettingsDialog::onBrowseHostFfmpeg);
    form->addRow("ffmpeg path:", ffRow);

    // exiftool path row
    auto* etRow = new QHBoxLayout;
    m_hostExiftool = new QLineEdit; m_hostExiftool->setPlaceholderText("exiftool  (system PATH)");
    etRow->addWidget(m_hostExiftool, 1);
    auto* etBrowse = new QPushButton("Browse…");
    etRow->addWidget(etBrowse);
    connect(etBrowse, &QPushButton::clicked, this, &SettingsDialog::onBrowseHostExiftool);
    form->addRow("exiftool path:", etRow);

    m_hostExiftoolOpts = new QLineEdit;
    m_hostExiftoolOpts->setPlaceholderText("-ee3 -p '$GPSDateTime ...'  (default)");
    form->addRow("exiftool options:", m_hostExiftoolOpts);

    auto* sep1 = new QFrame; sep1->setFrameShape(QFrame::HLine); form->addRow(sep1);

    // Export dir row
    auto* exRow = new QHBoxLayout;
    m_hostExportDir = new QLineEdit; m_hostExportDir->setPlaceholderText("(current directory)");
    exRow->addWidget(m_hostExportDir, 1);
    auto* exBrowse = new QPushButton("Browse…");
    exRow->addWidget(exBrowse);
    connect(exBrowse, &QPushButton::clicked, this, &SettingsDialog::onBrowseHostExportDir);
    form->addRow("Default export dir:", exRow);

    // Temp dir row
    auto* tmpRow = new QHBoxLayout;
    m_hostTmpDir = new QLineEdit; m_hostTmpDir->setPlaceholderText("(auto: <export dir>/pm_tmp)");
    tmpRow->addWidget(m_hostTmpDir, 1);
    auto* tmpBrowse = new QPushButton("Browse…");
    tmpRow->addWidget(tmpBrowse);
    connect(tmpBrowse, &QPushButton::clicked, this, &SettingsDialog::onBrowseHostTmpDir);
    form->addRow("Temp directory:", tmpRow);

    auto* sep2 = new QFrame; sep2->setFrameShape(QFrame::HLine); form->addRow(sep2);

    m_hostLogLevel = new QComboBox;
    m_hostLogLevel->addItems({"off", "normal", "debug"});
    form->addRow("Log level (this host):", m_hostLogLevel);

    tabs->addTab(scrollWrap(w), "Host");
}

// ---------------------------------------------------------------------------
// KML tab
// ---------------------------------------------------------------------------

void SettingsDialog::buildKmlTab(QTabWidget* tabs)
{
    auto* w    = new QWidget;
    auto* form = new QFormLayout(w);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setContentsMargins(12, 12, 12, 12);
    form->setSpacing(8);

    form->addRow(new QLabel("<i>Colors are KML AABBGGRR hex (AA=alpha, BB=blue, GG=green, RR=red, ff=opaque).</i>"));

    auto* sep0 = new QFrame; sep0->setFrameShape(QFrame::HLine); form->addRow(sep0);

    m_trackAheadColor = new QLineEdit; m_trackAheadColor->setMaxLength(8); m_trackAheadColor->setPlaceholderText("ff00ff00");
    form->addRow("Track ahead color:", m_trackAheadColor);

    m_trackBehindColor = new QLineEdit; m_trackBehindColor->setMaxLength(8); m_trackBehindColor->setPlaceholderText("ff0000ff");
    form->addRow("Track behind color:", m_trackBehindColor);

    m_trackLineWidth = new QSpinBox; m_trackLineWidth->setRange(1, 10); m_trackLineWidth->setSuffix(" px");
    form->addRow("Track line width:", m_trackLineWidth);

    m_waypointColor = new QLineEdit; m_waypointColor->setMaxLength(8); m_waypointColor->setPlaceholderText("ffff0000");
    form->addRow("Waypoint color:", m_waypointColor);

    auto* sep1 = new QFrame; sep1->setFrameShape(QFrame::HLine); form->addRow(sep1);

    m_startPinUrl = new QLineEdit;
    form->addRow("Start pin URL:", m_startPinUrl);

    m_endPinUrl = new QLineEdit;
    form->addRow("End pin URL:", m_endPinUrl);

    form->addRow(new QLabel(
        "<small>Google KML icon examples:<br>"
        "  http://maps.google.com/mapfiles/kml/pushpin/ylw-pushpin.png<br>"
        "  http://maps.google.com/mapfiles/kml/paddle/wht-blank.png</small>"));

    auto* sep2 = new QFrame; sep2->setFrameShape(QFrame::HLine); form->addRow(sep2);

    m_showKnownLocs = new QCheckBox("Overlay named locations on KML export");
    form->addRow(m_showKnownLocs);

    tabs->addTab(scrollWrap(w), "KML");
}

// ---------------------------------------------------------------------------
// Locations tab
// ---------------------------------------------------------------------------

void SettingsDialog::buildLocationsTab(QTabWidget* tabs)
{
    auto* w    = new QWidget;
    auto* vlay = new QVBoxLayout(w);
    vlay->setContentsMargins(8, 8, 8, 8);
    vlay->setSpacing(6);

    vlay->addWidget(new QLabel("Named locations used for KML export overlays and proximity tagging."));

    m_locTable = new QTableWidget(0, 5);
    m_locTable->setHorizontalHeaderLabels({"Name", "Latitude", "Longitude", "Radius (m)", "Icon"});
    m_locTable->horizontalHeader()->setStretchLastSection(false);
    m_locTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_locTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_locTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_locTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_locTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_locTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_locTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_locTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    vlay->addWidget(m_locTable, 1);

    auto* btnRow = new QHBoxLayout;
    auto* addBtn = new QPushButton("Add Location");
    auto* delBtn = new QPushButton("Delete Selected");
    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch();
    vlay->addLayout(btnRow);

    connect(addBtn, &QPushButton::clicked, this, &SettingsDialog::onAddLocation);
    connect(delBtn, &QPushButton::clicked, this, &SettingsDialog::onDeleteLocation);

    tabs->addTab(w, "Locations");
}

// ---------------------------------------------------------------------------
// Load / save
// ---------------------------------------------------------------------------

void SettingsDialog::loadFromConfig()
{
    const AppSettings& s = m_config.getSettings();

    // General
    m_gapThreshold->setValue(s.gapThresholdSeconds);
    m_fuzzyWindow->setValue(s.fuzzyWindowSeconds);
    m_gpsSkip->setValue(s.gpsColStartSkip);
    m_timestampFmt->setCurrentIndex(comboFind(m_timestampFmt, QString::fromStdString(s.timestampFormat)));
    m_timeDisplay->setCurrentIndex(comboFind(m_timeDisplay, QString::fromStdString(s.timeDisplay)));
    m_useImperial->setChecked(s.useImperial);
    m_videoFormat->setCurrentIndex(comboFind(m_videoFormat, QString::fromStdString(s.videoFormat)));
    m_audioSource->setCurrentIndex(comboFind(m_audioSource, QString::fromStdString(s.defaultAudioSource)));
    m_logLevel->setCurrentIndex(comboFind(m_logLevel, QString::fromStdString(s.logLevel)));
    m_activeProfile->setText(QString::fromStdString(s.activeProfileId));

    // Encoder
    m_preset->setCurrentIndex(comboFind(m_preset, QString::fromStdString(s.encode.preset)));
    m_hwDevice->setText(QString::fromStdString(s.encode.hwDevice));
    m_hwDeviceType->setCurrentIndex(comboFind(m_hwDeviceType, QString::fromStdString(s.encode.hwDeviceType)));
    m_normEncoder->setText(QString::fromStdString(s.encode.normEncoder));
    m_collageEncoder->setText(QString::fromStdString(s.encode.collageEncoder));
    m_downEncoder->setText(QString::fromStdString(s.encode.downEncoder));
    m_pixFmt->setCurrentIndex(comboFind(m_pixFmt, QString::fromStdString(s.encode.pixFmt)));
    if (m_pixFmt->currentIndex() < 0) m_pixFmt->setEditText(QString::fromStdString(s.encode.pixFmt));
    {
        auto safeInt = [](const std::string& v, int def) -> int {
            try { return std::stoi(v); } catch (...) { return def; }
        };
        m_normQuality->setValue(safeInt(s.encode.normQuality, 23));
        m_collageQuality->setValue(safeInt(s.encode.collageQuality, 18));
        m_downQuality->setValue(safeInt(s.encode.downQuality, 20));
    }
    m_extraNorm->setText(QString::fromStdString(s.encode.extraNormArgs));
    m_extraCollage->setText(QString::fromStdString(s.encode.extraCollageArgs));
    m_extraDown->setText(QString::fromStdString(s.encode.extraDownArgs));

    // Host
    m_hostFileLabel->setText(
        "<b>Host file:</b> " + QString::fromStdString(m_config.getHostSettingsFile()));
    m_hostFfmpeg->setText(QString::fromStdString(s.ffmpegPath));
    m_hostExiftool->setText(QString::fromStdString(s.exiftoolPath));
    m_hostExiftoolOpts->setText(QString::fromStdString(s.exiftoolOptions));
    m_hostExportDir->setText(QString::fromStdString(s.defaultExportDir));
    m_hostTmpDir->setText(QString::fromStdString(s.tmpDir));
    m_hostLogLevel->setCurrentIndex(comboFind(m_hostLogLevel, QString::fromStdString(s.logLevel)));

    // KML
    const KmlSettings& k = s.kml;
    m_trackAheadColor->setText(QString::fromStdString(k.trackAheadColor));
    m_trackBehindColor->setText(QString::fromStdString(k.trackBehindColor));
    m_trackLineWidth->setValue(k.trackLineWidth);
    m_waypointColor->setText(QString::fromStdString(k.waypointColor));
    m_startPinUrl->setText(QString::fromStdString(k.startPinUrl));
    m_endPinUrl->setText(QString::fromStdString(k.endPinUrl));
    m_showKnownLocs->setChecked(k.showKnownLocations);

    // Locations
    m_locTable->setRowCount(0);
    for (const auto& loc : m_locations) {
        int row = m_locTable->rowCount();
        m_locTable->insertRow(row);
        m_locTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(loc.name)));
        m_locTable->setItem(row, 1, new QTableWidgetItem(QString::number(loc.lat, 'f', 6)));
        m_locTable->setItem(row, 2, new QTableWidgetItem(QString::number(loc.lon, 'f', 6)));
        m_locTable->setItem(row, 3, new QTableWidgetItem(QString::number(loc.radiusMetres)));
        m_locTable->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(loc.icon)));
    }
}

void SettingsDialog::saveToConfig()
{
    AppSettings s = m_config.getSettings();

    // General
    s.gapThresholdSeconds = m_gapThreshold->value();
    s.fuzzyWindowSeconds  = m_fuzzyWindow->value();
    s.gpsColStartSkip     = m_gpsSkip->value();
    s.timestampFormat     = m_timestampFmt->currentText().toStdString();
    s.timeDisplay         = m_timeDisplay->currentText().toStdString();
    s.useImperial         = m_useImperial->isChecked();
    s.videoFormat         = m_videoFormat->currentText().toStdString();
    s.defaultAudioSource  = m_audioSource->currentText().toStdString();
    s.logLevel            = m_logLevel->currentText().toStdString();
    s.activeProfileId     = m_activeProfile->text().toStdString();

    // Encoder
    s.encode.preset         = m_preset->currentText().toStdString();
    s.encode.hwDevice       = m_hwDevice->text().toStdString();
    s.encode.hwDeviceType   = m_hwDeviceType->currentText().toStdString();
    s.encode.normEncoder    = m_normEncoder->text().toStdString();
    s.encode.collageEncoder = m_collageEncoder->text().toStdString();
    s.encode.downEncoder    = m_downEncoder->text().toStdString();
    s.encode.pixFmt         = m_pixFmt->currentText().toStdString();
    s.encode.normQuality    = std::to_string(m_normQuality->value());
    s.encode.collageQuality = std::to_string(m_collageQuality->value());
    s.encode.downQuality    = std::to_string(m_downQuality->value());
    s.encode.extraNormArgs    = m_extraNorm->text().toStdString();
    s.encode.extraCollageArgs = m_extraCollage->text().toStdString();
    s.encode.extraDownArgs    = m_extraDown->text().toStdString();

    // Host fields (written to host file AND base file for host-specific paths)
    s.ffmpegPath       = m_hostFfmpeg->text().toStdString();
    s.exiftoolPath     = m_hostExiftool->text().toStdString();
    s.exiftoolOptions  = m_hostExiftoolOpts->text().toStdString();
    s.defaultExportDir = m_hostExportDir->text().toStdString();
    s.tmpDir           = m_hostTmpDir->text().toStdString();
    // logLevel: host tab wins if different from general tab
    s.logLevel         = m_hostLogLevel->currentText().toStdString();

    // KML
    s.kml.trackAheadColor  = m_trackAheadColor->text().toStdString();
    s.kml.trackBehindColor = m_trackBehindColor->text().toStdString();
    s.kml.trackLineWidth   = m_trackLineWidth->value();
    s.kml.waypointColor    = m_waypointColor->text().toStdString();
    s.kml.startPinUrl      = m_startPinUrl->text().toStdString();
    s.kml.endPinUrl        = m_endPinUrl->text().toStdString();
    s.kml.showKnownLocations = m_showKnownLocs->isChecked();

    m_config.applySettings(s);
    m_config.saveSettings();      // writes pathmux.json
    m_config.saveHostSettings();  // writes pathmux_<hostname>.json

    // Locations
    std::vector<NamedLocation> locs;
    for (int row = 0; row < m_locTable->rowCount(); ++row) {
        NamedLocation loc;
        loc.name         = m_locTable->item(row, 0)->text().toStdString();
        try { loc.lat    = std::stod(m_locTable->item(row, 1)->text().toStdString()); } catch (...) {}
        try { loc.lon    = std::stod(m_locTable->item(row, 2)->text().toStdString()); } catch (...) {}
        try { loc.radiusMetres = std::stoi(m_locTable->item(row, 3)->text().toStdString()); } catch (...) { loc.radiusMetres = 50; }
        loc.icon         = m_locTable->item(row, 4)->text().toStdString();
        locs.push_back(loc);
    }
    m_config.saveLocations(locs);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void SettingsDialog::onApplyPreset()
{
    // Apply the chosen preset to a copy of settings, then populate encoder fields.
    AppSettings tmp = m_config.getSettings();
    tmp.encode.preset = m_preset->currentText().toStdString();
    ConfigManager tmpCfg;
    tmpCfg.applySettings(tmp);
    tmpCfg.applyEncodePreset(tmp.encode.preset);
    const EncodeSettings& e = tmpCfg.getSettings().encode;

    m_hwDevice->setText(QString::fromStdString(e.hwDevice));
    m_hwDeviceType->setCurrentIndex(comboFind(m_hwDeviceType, QString::fromStdString(e.hwDeviceType)));
    m_normEncoder->setText(QString::fromStdString(e.normEncoder));
    m_collageEncoder->setText(QString::fromStdString(e.collageEncoder));
    m_downEncoder->setText(QString::fromStdString(e.downEncoder));
    m_pixFmt->setCurrentIndex(comboFind(m_pixFmt, QString::fromStdString(e.pixFmt)));
    try { m_normQuality->setValue(std::stoi(e.normQuality)); } catch (...) {}
    try { m_collageQuality->setValue(std::stoi(e.collageQuality)); } catch (...) {}
    try { m_downQuality->setValue(std::stoi(e.downQuality)); } catch (...) {}
    m_extraNorm->setText(QString::fromStdString(e.extraNormArgs));
    m_extraCollage->setText(QString::fromStdString(e.extraCollageArgs));
    m_extraDown->setText(QString::fromStdString(e.extraDownArgs));
}

static QString browseFile(QWidget* parent, const QString& title, const QString& filter)
{
    return QFileDialog::getOpenFileName(parent, title, QString(), filter);
}

static QString browseDir(QWidget* parent, const QString& title)
{
    return QFileDialog::getExistingDirectory(parent, title, QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
}

void SettingsDialog::onBrowseFfmpeg()
{
    QString p = browseFile(this, "Select ffmpeg executable", "Executables (ffmpeg ffmpeg.exe *)");
    if (!p.isEmpty()) m_hostFfmpeg->setText(p);
}
void SettingsDialog::onBrowseExiftool()
{
    QString p = browseFile(this, "Select exiftool executable", "Executables (exiftool exiftool.exe *)");
    if (!p.isEmpty()) m_hostExiftool->setText(p);
}
void SettingsDialog::onBrowseExportDir()
{
    QString d = browseDir(this, "Select default export directory");
    if (!d.isEmpty()) m_hostExportDir->setText(d);
}
void SettingsDialog::onBrowseTmpDir()
{
    QString d = browseDir(this, "Select temp directory");
    if (!d.isEmpty()) m_hostTmpDir->setText(d);
}
void SettingsDialog::onBrowseHostFfmpeg()   { onBrowseFfmpeg(); }
void SettingsDialog::onBrowseHostExiftool() { onBrowseExiftool(); }
void SettingsDialog::onBrowseHostExportDir(){ onBrowseExportDir(); }
void SettingsDialog::onBrowseHostTmpDir()   { onBrowseTmpDir(); }

void SettingsDialog::onAddLocation()
{
    bool ok;
    QString name = QInputDialog::getText(this, "Add Location", "Location name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QString latStr = QInputDialog::getText(this, "Latitude", "Latitude (decimal degrees):", QLineEdit::Normal, "", &ok);
    if (!ok) return;
    QString lonStr = QInputDialog::getText(this, "Longitude", "Longitude (decimal degrees):", QLineEdit::Normal, "", &ok);
    if (!ok) return;

    double lat = 0, lon = 0;
    try { lat = std::stod(latStr.toStdString()); } catch (...) {
        QMessageBox::warning(this, "Invalid", "Invalid latitude value."); return;
    }
    try { lon = std::stod(lonStr.toStdString()); } catch (...) {
        QMessageBox::warning(this, "Invalid", "Invalid longitude value."); return;
    }

    int row = m_locTable->rowCount();
    m_locTable->insertRow(row);
    m_locTable->setItem(row, 0, new QTableWidgetItem(name.trimmed()));
    m_locTable->setItem(row, 1, new QTableWidgetItem(QString::number(lat, 'f', 6)));
    m_locTable->setItem(row, 2, new QTableWidgetItem(QString::number(lon, 'f', 6)));
    m_locTable->setItem(row, 3, new QTableWidgetItem("50"));
    m_locTable->setItem(row, 4, new QTableWidgetItem("pin"));
}

void SettingsDialog::onDeleteLocation()
{
    int row = m_locTable->currentRow();
    if (row < 0) return;
    QString name = m_locTable->item(row, 0) ? m_locTable->item(row, 0)->text() : "(unnamed)";
    if (QMessageBox::question(this, "Delete Location",
            "Delete location \"" + name + "\"?",
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
        m_locTable->removeRow(row);
}

void SettingsDialog::onAccepted()
{
    saveToConfig();
    accept();
}
// SN: 00095
