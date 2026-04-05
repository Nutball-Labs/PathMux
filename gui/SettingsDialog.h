// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <vector>
#include "config_manager.hpp"

class QTabWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QTableWidget;
class QLabel;
class QPushButton;

// ---------------------------------------------------------------------------
// SettingsDialog — tabbed settings for all PathMux preferences.
// Tabs mirror the CLI settings hierarchy:
//   General  → pathmux.json (trip, display, output, format)
//   Encoder  → pathmux_<hostname>.json  (encode preset + all fields)
//   Host     → pathmux_<hostname>.json  (path overrides for this machine)
//   KML      → pathmux.json  (kml export styling)
//   Locations → locations.json
// ---------------------------------------------------------------------------
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void onApplyPreset();
    void onBrowseFfmpeg();
    void onBrowseExiftool();
    void onBrowseExportDir();
    void onBrowseTmpDir();
    void onBrowseHostFfmpeg();
    void onBrowseHostExiftool();
    void onBrowseHostExportDir();
    void onBrowseHostTmpDir();
    void onAddLocation();
    void onDeleteLocation();
    void onAccepted();

private:
    void buildGeneralTab(QTabWidget*);
    void buildEncoderTab(QTabWidget*);
    void buildHostTab(QTabWidget*);
    void buildKmlTab(QTabWidget*);
    void buildLocationsTab(QTabWidget*);

    void loadFromConfig();
    void saveToConfig();

    Pathmux::ConfigManager m_config;
    std::vector<Pathmux::NamedLocation> m_locations;

    // -- General tab --
    QSpinBox*  m_gapThreshold   = nullptr;
    QSpinBox*  m_fuzzyWindow    = nullptr;
    QSpinBox*  m_gpsSkip        = nullptr;
    QComboBox* m_timestampFmt   = nullptr;
    QComboBox* m_timeDisplay    = nullptr;
    QCheckBox* m_useImperial    = nullptr;
    QComboBox* m_videoFormat    = nullptr;
    QComboBox* m_audioSource    = nullptr;
    QComboBox* m_logLevel       = nullptr;
    QLineEdit* m_activeProfile  = nullptr;

    // -- Encoder tab --
    QComboBox* m_preset          = nullptr;
    QLineEdit* m_hwDevice        = nullptr;
    QComboBox* m_hwDeviceType    = nullptr;
    QLineEdit* m_normEncoder     = nullptr;
    QLineEdit* m_collageEncoder  = nullptr;
    QLineEdit* m_downEncoder     = nullptr;
    QComboBox* m_pixFmt          = nullptr;
    QSpinBox*  m_normQuality     = nullptr;
    QSpinBox*  m_collageQuality  = nullptr;
    QSpinBox*  m_downQuality     = nullptr;
    QLineEdit* m_extraNorm       = nullptr;
    QLineEdit* m_extraCollage    = nullptr;
    QLineEdit* m_extraDown       = nullptr;

    // -- Host tab --
    QLabel*    m_hostFileLabel   = nullptr;
    QLineEdit* m_hostFfmpeg      = nullptr;
    QLineEdit* m_hostExiftool    = nullptr;
    QLineEdit* m_hostExiftoolOpts = nullptr;
    QLineEdit* m_hostExportDir   = nullptr;
    QLineEdit* m_hostTmpDir      = nullptr;
    QComboBox* m_hostLogLevel    = nullptr;

    // -- KML tab --
    QLineEdit* m_trackAheadColor   = nullptr;
    QLineEdit* m_trackBehindColor  = nullptr;
    QSpinBox*  m_trackLineWidth    = nullptr;
    QLineEdit* m_waypointColor     = nullptr;
    QLineEdit* m_startPinUrl       = nullptr;
    QLineEdit* m_endPinUrl         = nullptr;
    QCheckBox* m_showKnownLocs     = nullptr;

    // -- Locations tab --
    QTableWidget* m_locTable       = nullptr;
};
// SN: 00095
