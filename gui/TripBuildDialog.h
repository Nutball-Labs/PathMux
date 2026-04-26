// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <QString>
#include <array>
#include <set>
#include <string>
#include "trip_detection.hpp"
#include "config_manager.hpp"
#include "video_build.hpp"

class LogoMorphWidget;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTabWidget;
class QWidget;

class TripBuildDialog : public QDialog {
    Q_OBJECT
public:
    explicit TripBuildDialog(const Pathmux::ManifestEntry& manifest,
                             const Pathmux::Trip&          trip,
                             QWidget*                      parent = nullptr);

    VideoOptions buildOptions() const;
    bool         processNow()  const;

private slots:
    void onBrowseOutput();
    void onBrowseOverlay();
    void onPreviewFrame();

private:
    QWidget* makeOverlayCell();
    QWidget* makeHudRow();

    QWidget*   makeCollageTab(const std::set<std::string>& cams);
    QWidget*   makeFilesTab  (const std::set<std::string>& cams);
    QWidget*   makeOutputTab ();
    QWidget*   makeActionRow ();

    QWidget*   makeQuadrantCell(int idx, const std::set<std::string>& cams);
    void       updateQuadFileRow(int idx);

    // Quadrant → slot mapping built from the embedded camera profile at construct time.
    // Indices 0-3 map to TL, TR, BL, BR.  Empty string = no camera assigned to that quad.
    static constexpr std::array<const char*, 4> kPos =
        {"Top Left", "Top Right", "Bottom Left", "Bottom Right"};
    std::array<std::string, 4> m_kSlot = {};  // backend slot name (e.g. "front")
    std::array<QString,     4> m_kCam  = {};  // display name      (e.g. "Front")

    Pathmux::ManifestEntry  m_manifest;
    Pathmux::Trip           m_trip;
    std::string             m_ffmpegPath;
    Pathmux::EncodeSettings m_encode;

    // ── Collage tab ──────────────────────────────────────────────────────────
    QCheckBox*      m_quadEnabled[4]  = {};   // enable/disable each quadrant
    QComboBox*      m_quadCombo[4]    = {};   // source selector per quadrant
    QLineEdit*      m_quadPath[4]     = {};   // external file path per quadrant
    QPushButton*    m_quadBrowse[4]   = {};   // browse button per quadrant
    QWidget*        m_quadFileRow[4]  = {};   // browse row (shown only for external)
    LogoMorphWidget* m_quadMorph[4]   = {};   // animated placeholder for None
    QComboBox*   m_audioQuad       = nullptr;  // which quadrant provides audio
    QPushButton* m_btnPreview      = nullptr;
    QCheckBox*   m_chk4K           = nullptr;
    QCheckBox*   m_chk1080         = nullptr;
    QCheckBox*       m_chkMapOverlay       = nullptr;
    QComboBox*       m_overlayCombo        = nullptr;
    QLineEdit*       m_mapOverlayPath      = nullptr;
    QPushButton*     m_mapOverlayBrowseBtn = nullptr;
    QWidget*         m_overlayFileRow      = nullptr;
    LogoMorphWidget* m_overlayMorph        = nullptr;

    // HUD overlay (full-screen, VP9 alpha)
    QCheckBox*   m_chkHudOverlay   = nullptr;
    QComboBox*   m_hudCombo        = nullptr;
    QLineEdit*   m_hudPath         = nullptr;
    QPushButton* m_hudBrowseBtn    = nullptr;
    QWidget*     m_hudFileRow      = nullptr;

    // ── Camera Files tab ─────────────────────────────────────────────────────
    QCheckBox*   m_chkCam[4]   = {}; // indexed by quadrant, matches m_kSlot
    QComboBox*   m_container   = nullptr;
    QCheckBox*   m_chkAudio    = nullptr;
    QComboBox*   m_audioCamera = nullptr;
    QComboBox*   m_audioFormat = nullptr;

    // ── Output tab ───────────────────────────────────────────────────────────
    QLineEdit*   m_outputDir = nullptr;
    QLineEdit*   m_basename  = nullptr;

    // ── Action row ───────────────────────────────────────────────────────────
    QPushButton* m_btnNow     = nullptr;
    QPushButton* m_btnQueue   = nullptr;
    QCheckBox*   m_chkVerbose = nullptr;
};
// SN: 00104
