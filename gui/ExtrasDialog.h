// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <string>
#include "trip_detection.hpp"

class QCheckBox;
class QPushButton;

// Lightweight dialog for queuing per-trip build jobs.
// Tree layout:
//   GPS Extract  ⓘ     ← GPS + camera sync (two-stage); red/green; disabled if no GPS hw
//   ├─ Map       ⓘ     ← greyed until GPS ready, then red/green
//   ├─ Dashboard ⓘ
//   └─ HUD       ⓘ
//   ──────────────
//   Sync Cameras ⓘ     ← standalone sync; disabled/greyed when GPS Extract is checked
class ExtrasDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExtrasDialog(const CamClops::Trip& trip,
                          const std::string& sourcePath,
                          const std::string& mid,
                          QWidget* parent = nullptr);

private slots:
    void onOutputChecked();   // Map/Dash/HUD checked — auto-check GPS if needed
    void updateState();       // recompute all colours + button enable
    void onQueue();

private:
    CamClops::Trip m_trip;
    std::string   m_sourcePath;
    std::string   m_mid;

    QCheckBox*   m_gpsCheck  = nullptr;
    QCheckBox*   m_mapCheck  = nullptr;
    QCheckBox*   m_dashCheck = nullptr;
    QCheckBox*   m_hudCheck  = nullptr;
    QCheckBox*   m_syncCheck = nullptr;
    QPushButton* m_queueBtn  = nullptr;
};
// SN: 00113
