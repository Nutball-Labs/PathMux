// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "SetupWizard.h"
#include "version.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QFileDialog>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QPixmap>
#include <QIcon>
#include <QApplication>

using namespace Pathmux;

// ---------------------------------------------------------------------------
// WelcomePage
// ---------------------------------------------------------------------------

WelcomePage::WelcomePage(QWidget* parent)
    : QWizardPage(parent)
{
    setTitle("Welcome to PathMux " APP_VERSION);
    setSubTitle("Let's get you set up in just a few steps.");

    auto* vlay = new QVBoxLayout(this);

    auto* body = new QLabel(
        "<p>PathMux organises your 360° dashcam footage into trips, "
        "extracts GPS tracks, and helps you build video collages.</p>"
        "<p>This wizard will help you configure the critical settings "
        "needed to get started:</p>"
        "<ul>"
        "<li><b>ffmpeg</b> — video processing (required)</li>"
        "<li><b>ExifTool</b> — GPS track extraction (required for GPS features)</li>"
        "<li>Default output directory and hardware encoder preset</li>"
        "</ul>"
        "<p>You can change all settings at any time via "
        "<b>Tools → Settings…</b></p>");
    body->setWordWrap(true);
    vlay->addWidget(body);
    vlay->addStretch();
}

// ---------------------------------------------------------------------------
// ToolsPage — helpers
// ---------------------------------------------------------------------------

// Try to locate a tool on the system PATH.  On macOS also check Homebrew.
static QString detectTool(const QString& name)
{
    // Try QStandardPaths first
    QString found = QStandardPaths::findExecutable(name);
    if (!found.isEmpty()) return found;

#ifdef __APPLE__
    // Homebrew (Intel and Apple Silicon)
    for (const QString& prefix : {"/usr/local/bin", "/opt/homebrew/bin"}) {
        QString candidate = prefix + "/" + name;
        if (QFileInfo::exists(candidate)) return candidate;
    }
#endif
#ifdef _WIN32
    // Common Windows locations for these tools
    for (const QString& dir : {
        "C:/Windows/System32",
        "C:/Program Files/ffmpeg/bin",
        "C:/ffmpeg/bin"}) {
        QString candidate = dir + "/" + name + ".exe";
        if (QFileInfo::exists(candidate)) return candidate;
    }
#endif
    return {};
}

// ---------------------------------------------------------------------------
// ToolsPage
// ---------------------------------------------------------------------------

ToolsPage::ToolsPage(QWidget* parent)
    : QWizardPage(parent)
{
    setTitle("External Tool Paths");
    setSubTitle("PathMux requires ffmpeg for video processing and ExifTool for GPS extraction.");

    auto* form = new QFormLayout(this);
    form->setSpacing(8);

    // -- ffmpeg --
    auto* ffRow = new QHBoxLayout;
    m_ffmpegPath = new QLineEdit;
    m_ffmpegPath->setPlaceholderText("ffmpeg  (leave empty to use system PATH)");
    ffRow->addWidget(m_ffmpegPath, 1);
    auto* ffBrowse = new QPushButton("Browse…");
    ffRow->addWidget(ffBrowse);
    form->addRow("ffmpeg path:", ffRow);

    m_ffmpegStatus = new QLabel;
    form->addRow("", m_ffmpegStatus);

    // -- exiftool --
    auto* etRow = new QHBoxLayout;
    m_exiftoolPath = new QLineEdit;
    m_exiftoolPath->setPlaceholderText("exiftool  (leave empty to use system PATH)");
    etRow->addWidget(m_exiftoolPath, 1);
    auto* etBrowse = new QPushButton("Browse…");
    etRow->addWidget(etBrowse);
    form->addRow("exiftool path:", etRow);

    m_exiftoolStatus = new QLabel;
    form->addRow("", m_exiftoolStatus);

    // -- Auto-detect button --
    m_detectBtn = new QPushButton("Auto-Detect Tools");
    form->addRow(m_detectBtn);

    auto* hint = new QLabel(
        "<small>Leave a field empty to use the system PATH. "
        "On macOS, Homebrew paths are checked automatically.</small>");
    hint->setWordWrap(true);
    form->addRow(hint);

    connect(ffBrowse,    &QPushButton::clicked, this, &ToolsPage::onBrowseFfmpeg);
    connect(etBrowse,    &QPushButton::clicked, this, &ToolsPage::onBrowseExiftool);
    connect(m_detectBtn, &QPushButton::clicked, this, &ToolsPage::onDetect);

    // Register fields so SummaryPage and SetupWizard::applyToConfig() can read them
    registerField("ffmpegPath",   m_ffmpegPath);
    registerField("exiftoolPath", m_exiftoolPath);
}

void ToolsPage::initializePage()
{
    // Auto-detect on first visit if fields are still empty
    if (m_ffmpegPath->text().isEmpty() && m_exiftoolPath->text().isEmpty())
        onDetect();
}

void ToolsPage::probe(const QString& toolName, QLineEdit* pathField, QLabel* statusLabel)
{
    QString explicit_ = pathField->text().trimmed();
    QString candidate = explicit_.isEmpty() ? detectTool(toolName) : explicit_;
    QString toRun     = candidate.isEmpty() ? toolName : candidate;

    QProcess proc;
    proc.start(toRun, {"--version"});
    bool ok = proc.waitForFinished(3000);
    if (!ok || proc.exitCode() != 0) {
        // Some tools use -ver or -version
        proc.start(toRun, {"-version"});
        ok = proc.waitForFinished(3000);
    }

    if (ok && proc.exitCode() == 0) {
        QString ver = QString::fromLatin1(proc.readAllStandardOutput()).section('\n', 0, 0).trimmed();
        if (ver.isEmpty())
            ver = QString::fromLatin1(proc.readAllStandardError()).section('\n', 0, 0).trimmed();
        statusLabel->setText("<span style='color:#228B22'>&#10003; Found: " + ver + "</span>");
        if (pathField->text().isEmpty() && !candidate.isEmpty())
            pathField->setText(candidate);  // fill in the resolved path
    } else {
        statusLabel->setText("<span style='color:#cc0000'>&#10007; Not found — install or browse to the executable</span>");
    }
}

void ToolsPage::onDetect()
{
    m_detectBtn->setEnabled(false);
    m_detectBtn->setText("Detecting…");
    QApplication::processEvents();
    probe("ffmpeg",   m_ffmpegPath,   m_ffmpegStatus);
    probe("exiftool", m_exiftoolPath, m_exiftoolStatus);
    m_detectBtn->setEnabled(true);
    m_detectBtn->setText("Auto-Detect Tools");
}

void ToolsPage::onBrowseFfmpeg()
{
    QString p = QFileDialog::getOpenFileName(this, "Select ffmpeg executable");
    if (!p.isEmpty()) {
        m_ffmpegPath->setText(p);
        probe("ffmpeg", m_ffmpegPath, m_ffmpegStatus);
    }
}

void ToolsPage::onBrowseExiftool()
{
    QString p = QFileDialog::getOpenFileName(this, "Select exiftool executable");
    if (!p.isEmpty()) {
        m_exiftoolPath->setText(p);
        probe("exiftool", m_exiftoolPath, m_exiftoolStatus);
    }
}

// ---------------------------------------------------------------------------
// OutputPage
// ---------------------------------------------------------------------------

OutputPage::OutputPage(QWidget* parent)
    : QWizardPage(parent)
{
    setTitle("Default Output Directory");
    setSubTitle("Where should PathMux save exported video files and GPS tracks by default?");

    auto* vlay = new QVBoxLayout(this);

    auto* form = new QFormLayout;
    auto* row  = new QHBoxLayout;
    m_exportDir = new QLineEdit;
    m_exportDir->setPlaceholderText("(current working directory)");
    row->addWidget(m_exportDir, 1);
    auto* browseBtn = new QPushButton("Browse…");
    row->addWidget(browseBtn);
    form->addRow("Export directory:", row);
    vlay->addLayout(form);

    auto* hint = new QLabel(
        "<small>Leave empty to save alongside the source footage or in "
        "the current directory. You can change this per-host in Settings.</small>");
    hint->setWordWrap(true);
    vlay->addWidget(hint);
    vlay->addStretch();

    connect(browseBtn, &QPushButton::clicked, this, &OutputPage::onBrowse);
    registerField("exportDir", m_exportDir);
}

void OutputPage::onBrowse()
{
    QString d = QFileDialog::getExistingDirectory(
        this, "Select default output directory", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!d.isEmpty()) m_exportDir->setText(d);
}

// ---------------------------------------------------------------------------
// HardwarePage
// ---------------------------------------------------------------------------

HardwarePage::HardwarePage(QWidget* parent)
    : QWizardPage(parent)
{
    setTitle("Encoder Hardware");
    setSubTitle("PathMux will probe your ffmpeg installation for available hardware encoders.");

    auto* vlay = new QVBoxLayout(this);

    auto* probeBtn = new QPushButton("Probe Hardware Encoders");
    vlay->addWidget(probeBtn);

    m_probeStatus = new QLabel("Click Probe to check for available hardware encoders.");
    m_probeStatus->setWordWrap(true);
    vlay->addWidget(m_probeStatus);

    m_probeDetail = new QLabel;
    m_probeDetail->setWordWrap(true);
    vlay->addWidget(m_probeDetail);

    auto* form = new QFormLayout;
    m_presetCombo = new QComboBox;
    m_presetCombo->addItems({"cpu", "nvenc", "qsv", "vaapi"});
    form->addRow("Encoder preset:", m_presetCombo);
    vlay->addLayout(form);

    auto* presetNote = new QLabel(
        "<small><b>cpu</b> — software encode (libx264/libx265); works on any machine.<br>"
        "<b>nvenc</b> — NVIDIA GPU (RTX/GTX). <b>qsv</b> — Intel Quick Sync. "
        "<b>vaapi</b> — AMD/Intel VAAPI.<br>"
        "When in doubt, leave as <b>cpu</b>. You can change this anytime in Settings → Encoder.</small>");
    presetNote->setWordWrap(true);
    vlay->addWidget(presetNote);
    vlay->addStretch();

    connect(probeBtn, &QPushButton::clicked, this, &HardwarePage::onProbe);
    registerField("encoderPreset", m_presetCombo, "currentText", "currentTextChanged");
}

void HardwarePage::initializePage()
{
    // Inherit ffmpeg path from ToolsPage field
}

void HardwarePage::onProbe()
{
    QString ffmpeg = field("ffmpegPath").toString().trimmed();
    if (ffmpeg.isEmpty()) ffmpeg = "ffmpeg";

    m_probeStatus->setText("Probing…");
    m_probeDetail->clear();
    QApplication::processEvents();

    QProcess proc;
    proc.start(ffmpeg, {"-encoders"});
    bool ok = proc.waitForFinished(8000);
    if (!ok) {
        m_probeStatus->setText(
            "<span style='color:#cc0000'>ffmpeg timed out or could not be found. "
            "Check the path on the Tools page.</span>");
        return;
    }

    QString output = QString::fromLatin1(proc.readAllStandardOutput())
                   + QString::fromLatin1(proc.readAllStandardError());

    // Detect encoder families
    bool hasNvenc   = output.contains("h264_nvenc")  || output.contains("hevc_nvenc");
    bool hasQsv     = output.contains("h264_qsv")    || output.contains("hevc_qsv");
    bool hasVaapi   = output.contains("h264_vaapi")  || output.contains("hevc_vaapi");
    bool hasVt      = output.contains("h264_videotoolbox");  // macOS VideoToolbox

    QStringList found;
    if (hasNvenc) found << "NVENC (NVIDIA GPU)";
    if (hasQsv)   found << "QSV (Intel Quick Sync)";
    if (hasVaapi) found << "VAAPI (AMD/Intel)";
    if (hasVt)    found << "VideoToolbox (macOS)";
    if (found.isEmpty()) found << "CPU software only";

    m_probeStatus->setText("<b>Detected:</b> " + found.join(", "));

    // Suggest the best preset
    QString suggested = "cpu";
    QString detail;
    if (hasNvenc) {
        suggested = "nvenc";
        detail = "NVIDIA NVENC detected. Hardware encode will be significantly faster than CPU.";
    } else if (hasQsv) {
        suggested = "qsv";
        detail = "Intel Quick Sync detected. Good performance for H.264/HEVC encode.";
    } else if (hasVaapi) {
        suggested = "vaapi";
        detail = "VAAPI detected. Check /dev/dri/renderD128 is accessible.";
    } else {
        detail = "No hardware encoder found. CPU (libx264/libx265) will be used — slower but universally compatible.";
    }

    m_presetCombo->setCurrentText(suggested);
    m_probeDetail->setText("<i>" + detail + "</i>");
}

// ---------------------------------------------------------------------------
// SummaryPage
// ---------------------------------------------------------------------------

SummaryPage::SummaryPage(QWidget* parent)
    : QWizardPage(parent)
{
    setTitle("Summary");
    setSubTitle("Review your configuration. Click Finish to save.");

    auto* vlay = new QVBoxLayout(this);
    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    vlay->addWidget(m_summary);
    vlay->addStretch();
}

void SummaryPage::initializePage()
{
    QString ffmpeg   = field("ffmpegPath").toString().trimmed();
    QString exiftool = field("exiftoolPath").toString().trimmed();
    QString exportDir= field("exportDir").toString().trimmed();
    QString preset   = field("encoderPreset").toString();

    QString text =
        "<b>ffmpeg:</b> "    + (ffmpeg.isEmpty()    ? "system PATH (ffmpeg)"   : ffmpeg)   + "<br>" +
        "<b>ExifTool:</b> "  + (exiftool.isEmpty()  ? "system PATH (exiftool)" : exiftool) + "<br>" +
        "<b>Export dir:</b> "+ (exportDir.isEmpty() ? "(current directory)"    : exportDir)+ "<br>" +
        "<b>Encoder preset:</b> " + preset + "<br><br>"
        "<i>Settings saved to <tt>~/.config/pathmux/pathmux.json</tt> and "
        "the host-specific overlay file.</i>";

    m_summary->setText(text);
}

// ---------------------------------------------------------------------------
// SetupWizard
// ---------------------------------------------------------------------------

SetupWizard::SetupWizard(QWidget* parent)
    : QWizard(parent)
{
    setWindowTitle("PathMux Setup Wizard");
    setWizardStyle(QWizard::ModernStyle);
    setMinimumSize(540, 480);
    resize(600, 500);

    setPage(WP_WELCOME,  new WelcomePage(this));
    setPage(WP_TOOLS,    new ToolsPage(this));
    setPage(WP_OUTPUT,   new OutputPage(this));
    setPage(WP_HARDWARE, new HardwarePage(this));
    setPage(WP_SUMMARY,  new SummaryPage(this));

    setStartId(WP_WELCOME);

    setButtonText(QWizard::FinishButton, "Save && Finish");
    setOption(QWizard::NoBackButtonOnStartPage);
}

void SetupWizard::accept()
{
    // Save wizard choices to config when Finish is clicked.
    ConfigManager config;
    config.loadSettings();
    applyToConfig(config);
    QWizard::accept();
}

void SetupWizard::applyToConfig(ConfigManager& config)
{
    AppSettings s = config.getSettings();

    QString ffmpeg    = field("ffmpegPath").toString().trimmed();
    QString exiftool  = field("exiftoolPath").toString().trimmed();
    QString exportDir = field("exportDir").toString().trimmed();
    QString preset    = field("encoderPreset").toString();

    s.ffmpegPath       = ffmpeg.toStdString();
    s.exiftoolPath     = exiftool.toStdString();
    s.defaultExportDir = exportDir.toStdString();

    config.applySettings(s);
    config.applyEncodePreset(preset.toStdString());
    config.saveSettings();
    config.saveHostSettings();
}
// SN: 00095
