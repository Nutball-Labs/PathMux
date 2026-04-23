// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <string>
#include "trip_detection.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;

class TripPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit TripPropertiesDialog(const Pathmux::Trip& trip,
                                  const std::string& sourcePath,
                                  QWidget* parent = nullptr);

    // Returns the note text after the dialog is accepted.
    std::string updatedNote() const;

signals:
    void gpsExtracted();   // emitted immediately when GPS extraction succeeds

private slots:
    void onAccepted();
    void onGenerateMap();
    void onGenerateDashboard();
    void onExtractGps();
    void onExtractGpsProgress(int done, int total);
    void onExtractGpsFinished(bool ok, const QString& error);
    void onExportFormatChanged(int idx);
    void onExportBrowse();
    void onExportGps();

private:
    Pathmux::Trip m_trip;
    std::string   m_sourcePath;

    // General tab
    QLineEdit*    m_noteEdit        = nullptr;

    // GPS tab — extraction
    QLabel*       m_gpsStatusLabel  = nullptr;
    QPushButton*  m_extractGpsBtn   = nullptr;
    QProgressBar* m_extractBar      = nullptr;
    QLabel*       m_extractMsgLabel = nullptr;

    // GPS tab — export
    QComboBox*    m_exportFormatCombo = nullptr;
    QLineEdit*    m_exportPathEdit    = nullptr;
    QPushButton*  m_exportGpsBtn      = nullptr;

    // Map tab
    QLabel*       m_mapWarnLabel    = nullptr;
    QLineEdit*    m_mapOutputEdit   = nullptr;
    QListWidget*  m_mapFileList     = nullptr;
    QPushButton*  m_mapGenerateBtn  = nullptr;

    // Dashboard tab
    QLabel*       m_dashWarnLabel   = nullptr;
    QLineEdit*    m_dashOutputEdit  = nullptr;
    QListWidget*  m_dashFileList    = nullptr;
    QPushButton*  m_dashGenerateBtn = nullptr;

    void populateVideoList(QListWidget* list, const std::vector<std::string>& paths);
    void appendVideoToManifest(const QString& path, const std::string& key,
                               std::vector<std::string>& tripVec);
};
// SN: 00101
