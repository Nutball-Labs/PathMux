// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QWizard>
#include <QWizardPage>
#include "config_manager.hpp"

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QProgressBar;

// ---------------------------------------------------------------------------
// Page IDs — used by setPage() and nextId() to control navigation.
// ---------------------------------------------------------------------------
enum WizardPage { WP_WELCOME = 0, WP_TOOLS, WP_OUTPUT, WP_HARDWARE, WP_SUMMARY };

// ---------------------------------------------------------------------------
// WelcomePage — intro and overview
// ---------------------------------------------------------------------------
class WelcomePage : public QWizardPage {
    Q_OBJECT
public:
    explicit WelcomePage(QWidget* parent = nullptr);
    int nextId() const override { return WP_TOOLS; }
};

// ---------------------------------------------------------------------------
// ToolsPage — detect or manually set ffmpeg and exiftool paths
// ---------------------------------------------------------------------------
class ToolsPage : public QWizardPage {
    Q_OBJECT
public:
    explicit ToolsPage(QWidget* parent = nullptr);
    void initializePage() override;
    int nextId() const override { return WP_OUTPUT; }

private slots:
    void onDetect();
    void onBrowseFfmpeg();
    void onBrowseExiftool();

private:
    void probe(const QString& toolName, QLineEdit* pathField, QLabel* statusLabel);

    QLineEdit* m_ffmpegPath     = nullptr;
    QLabel*    m_ffmpegStatus   = nullptr;
    QLineEdit* m_exiftoolPath   = nullptr;
    QLabel*    m_exiftoolStatus = nullptr;
    QPushButton* m_detectBtn    = nullptr;
};

// ---------------------------------------------------------------------------
// OutputPage — default export directory
// ---------------------------------------------------------------------------
class OutputPage : public QWizardPage {
    Q_OBJECT
public:
    explicit OutputPage(QWidget* parent = nullptr);
    int nextId() const override { return WP_HARDWARE; }

private slots:
    void onBrowse();

private:
    QLineEdit* m_exportDir = nullptr;
};

// ---------------------------------------------------------------------------
// HardwarePage — probe ffmpeg for available hardware encoders and suggest preset
// ---------------------------------------------------------------------------
class HardwarePage : public QWizardPage {
    Q_OBJECT
public:
    explicit HardwarePage(QWidget* parent = nullptr);
    void initializePage() override;
    int nextId() const override { return WP_SUMMARY; }

private slots:
    void onProbe();

private:
    QLabel*    m_probeStatus = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QLabel*    m_probeDetail = nullptr;
};

// ---------------------------------------------------------------------------
// SummaryPage — review and confirm
// ---------------------------------------------------------------------------
class SummaryPage : public QWizardPage {
    Q_OBJECT
public:
    explicit SummaryPage(QWidget* parent = nullptr);
    void initializePage() override;
    int nextId() const override { return -1; }

private:
    QLabel* m_summary = nullptr;
};

// ---------------------------------------------------------------------------
// SetupWizard — first-run configuration wizard
// ---------------------------------------------------------------------------
class SetupWizard : public QWizard {
    Q_OBJECT
public:
    explicit SetupWizard(QWidget* parent = nullptr);

    // Call after accept() to persist the wizard's choices.
    void applyToConfig(CamClops::ConfigManager& config);

protected:
    void accept() override;
};
// SN: 00095
