// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <QProcess>
#include <string>
#include "trip_detection.hpp"

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class InactiveAwareTabBar;
class QThread;

class TripPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit TripPropertiesDialog(const CamClops::Trip& trip,
                                  const std::string& sourcePath,
                                  const std::string& mid,
                                  QWidget* parent = nullptr);

    // Returns the note text after the dialog is accepted.
    std::string updatedNote() const;

signals:
    void gpsExtracted();   // emitted immediately when GPS extraction succeeds
    void videosChanged();  // emitted when mapVideos/dashVideos/hudVideos are updated
    void syncAnalyzed();   // emitted when camera sync analysis completes successfully

protected:
    void reject() override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onAccepted();
    void onBuildAll();
    void onGenerateMap();
    void onGenerateDashboard();
    void onGenerateHud();
    void onExtractGps();
    void onExportFormatChanged(int idx);
    void onExportBrowse();
    void onExportGps();
    void onRunSyncAnalysis();
    void onSyncFinished(int exitCode, QProcess::ExitStatus status);

private:
    CamClops::Trip m_trip;
    std::string   m_sourcePath;
    std::string   m_mid;

    // Single tab bar
    InactiveAwareTabBar* m_topTabBar  = nullptr;
    QStackedWidget*      m_tabStack   = nullptr;

    // General tab
    QLineEdit*    m_noteEdit        = nullptr;

    // GPS tab — extraction
    QLabel*           m_gpsStatusLabel  = nullptr;
    QPushButton*      m_extractGpsBtn   = nullptr;
    QLabel*           m_extractMsgLabel = nullptr;

    // Bottom button box (Ok / Cancel)
    QDialogButtonBox* m_buttonBox       = nullptr;

    // GPS tab — export
    QComboBox*    m_exportFormatCombo = nullptr;
    QLineEdit*    m_exportPathEdit    = nullptr;
    QPushButton*  m_exportGpsBtn      = nullptr;
    QPushButton*  m_buildAllBtn       = nullptr;

    // Map tab
    QLabel*       m_mapWarnLabel    = nullptr;
    QLineEdit*    m_mapOutputEdit   = nullptr;
    QListWidget*  m_mapFileList     = nullptr;
    QPushButton*  m_mapGenerateBtn  = nullptr;

    // Dashboard tab
    QLabel*       m_dashWarnLabel        = nullptr;
    QLineEdit*    m_dashOutputEdit       = nullptr;
    QCheckBox*    m_dashTransparentCheck = nullptr;
    QComboBox*    m_dashLayoutCombo      = nullptr;
    QWidget*      m_dashLayoutRow        = nullptr;   // custom JSON file row
    QLineEdit*    m_dashLayoutPath       = nullptr;
    QListWidget*  m_dashFileList         = nullptr;
    QPushButton*  m_dashGenerateBtn      = nullptr;

    // HUD tab
    QLabel*         m_hudWarnLabel     = nullptr;
    QLineEdit*      m_hudOutputEdit    = nullptr;
    QSpinBox*       m_hudWidth         = nullptr;   // render resolution
    QSpinBox*       m_hudHeight        = nullptr;
    // Style
    QDoubleSpinBox* m_hudFontScale     = nullptr;
    QDoubleSpinBox* m_hudLineScale     = nullptr;
    QLineEdit*      m_hudColorHex      = nullptr;
    // Speed tapes
    QSpinBox*       m_hudTapeWidth     = nullptr;   // 0 = auto
    QSpinBox*       m_hudVisibleRange  = nullptr;   // units in tape height
    QSpinBox*       m_hudLsX           = nullptr;   // left tape position
    QSpinBox*       m_hudLsY           = nullptr;
    QSpinBox*       m_hudRsX           = nullptr;   // right tape position
    QSpinBox*       m_hudRsY           = nullptr;
    // Compass rose
    QSpinBox*       m_hudCompassRadius = nullptr;   // 0 = auto
    QSpinBox*       m_hudCompassCrop   = nullptr;   // % of diameter below frame, 0-50
    QSpinBox*       m_hudCompassX      = nullptr;   // -1 = center
    QSpinBox*       m_hudCompassY      = nullptr;   // -1 = bottom
    // File list + button
    QListWidget*    m_hudFileList      = nullptr;
    QPushButton*    m_hudGenerateBtn   = nullptr;

    // Sync Values tab
    int             m_syncTabIdx   = -1;
    QPushButton*    m_syncRunBtn   = nullptr;
    QPlainTextEdit* m_syncOutput   = nullptr;
    QProcess*       m_syncProcess  = nullptr;

    void populateVideoList(QListWidget* list, const std::vector<std::string>& paths);
    void appendVideoToManifest(const QString& path, const std::string& key,
                               std::vector<std::string>& tripVec);
    QWidget* buildSyncWidget();
    void     refreshSyncTab();
};
// SN: 00113
