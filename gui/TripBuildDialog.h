// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <set>
#include <string>
#include "trip_detection.hpp"
#include "config_manager.hpp"
#include "video_build.hpp"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QPushButton;
class QRadioButton;

class TripBuildDialog : public QDialog {
    Q_OBJECT
public:
    explicit TripBuildDialog(const Pathmux::ManifestEntry& manifest,
                             const Pathmux::Trip&          trip,
                             QWidget*                      parent = nullptr);

    VideoOptions buildOptions() const;
    bool         processNow()  const;

private slots:
    void onBrowse();

private:
    QGroupBox* makeCameraGroup(const std::set<std::string>& cams);
    QGroupBox* makeCollageGroup(const std::set<std::string>& cams);
    QGroupBox* makeAudioGroup(const std::set<std::string>& cams);
    QGroupBox* makeOutputGroup(const std::string& defaultExportDir);
    QWidget*   makeActionRow();

    Pathmux::ManifestEntry  m_manifest;
    Pathmux::Trip           m_trip;
    std::string             m_ffmpegPath;
    Pathmux::EncodeSettings m_encode;

    // Camera files
    QCheckBox* m_chkFront  = nullptr;
    QCheckBox* m_chkRear   = nullptr;
    QCheckBox* m_chkLeft   = nullptr;
    QCheckBox* m_chkRight  = nullptr;
    QComboBox* m_container = nullptr;

    // Collage
    QCheckBox*    m_chk4K   = nullptr;
    QCheckBox*    m_chk1080 = nullptr;
    QRadioButton* m_rbLeft  = nullptr;
    QRadioButton* m_rbRight = nullptr;
    QRadioButton* m_rbFront = nullptr;
    QRadioButton* m_rbRear  = nullptr;

    // Audio extract
    QCheckBox* m_chkAudio    = nullptr;
    QComboBox* m_audioCamera = nullptr;
    QComboBox* m_audioFormat = nullptr;

    // Output
    QLineEdit* m_outputDir = nullptr;
    QLineEdit* m_basename  = nullptr;

    // Action
    QPushButton* m_btnNow   = nullptr;
    QPushButton* m_btnQueue = nullptr;
};
// SN: 00093
