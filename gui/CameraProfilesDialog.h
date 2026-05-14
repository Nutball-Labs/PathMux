// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <vector>
#include "config_manager.hpp"

class QListWidget;
class QLabel;
class QPushButton;

// ---------------------------------------------------------------------------
// CameraProfilesDialog — Tools > Camera Profiles
//
// Lists all built-in and user-created camera profiles.
// User JSON profiles (~/.config/camclops/profiles/<id>.json) can be deleted.
// Built-in profiles are read-only.
// ---------------------------------------------------------------------------
class CameraProfilesDialog : public QDialog {
    Q_OBJECT
public:
    explicit CameraProfilesDialog(QWidget* parent = nullptr);

private slots:
    void onSelectionChanged();
    void onFork();        // create editable user copy of a built-in
    void onOpenEditor();  // open user profile JSON in system default editor
    void onSetActive();   // set this profile as the default for new scans
    void onDelete();      // delete user profile file

private:
    void loadProfiles();
    void showDetails(int index);

    CamClops::ConfigManager              m_config;
    std::vector<CamClops::CameraProfile> m_profiles;
    std::vector<bool>                   m_hasUserFile; // true = deletable user JSON exists

    QListWidget*  m_list          = nullptr;
    QLabel*       m_details       = nullptr;
    QPushButton*  m_forkBtn       = nullptr;
    QPushButton*  m_openEditorBtn = nullptr;
    QPushButton*  m_setActiveBtn  = nullptr;
    QPushButton*  m_deleteBtn     = nullptr;
};
// SN: 00104
