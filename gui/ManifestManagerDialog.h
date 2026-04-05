// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <vector>
#include "config_manager.hpp"

class QTableWidget;
class QPushButton;

// ---------------------------------------------------------------------------
// ManifestManagerDialog — view and manage all known manifests.
// Operations:
//   Refresh  — rescan the source directory (updates trips, timestamps)
//   Rewrite  — force full rescan ignoring any cached state
//   Delete   — remove manifest from index (and optionally delete the file)
//
// Emits manifestsChanged() when any manifest is modified or removed so the
// ManifestPanel in the main window can refresh its list.
// ---------------------------------------------------------------------------
class ManifestManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ManifestManagerDialog(QWidget* parent = nullptr);

signals:
    void manifestsChanged();

private slots:
    void onRefresh();
    void onRewrite();
    void onDelete();
    void onEditNickname();
    void onSelectionChanged();
    void onScanComplete(const Pathmux::ManifestEntry& entry);

private:
    void loadManifests();
    void updateButtons();
    int  selectedRow() const;

    QTableWidget* m_table      = nullptr;
    QPushButton*  m_refreshBtn = nullptr;
    QPushButton*  m_rewriteBtn = nullptr;
    QPushButton*  m_deleteBtn  = nullptr;
    QPushButton*  m_nickBtn    = nullptr;

    std::vector<Pathmux::ManifestEntry> m_entries;
};
// SN: 00095
