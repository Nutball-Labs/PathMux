// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <string>
#include <vector>
#include "trip_detection.hpp"

class QTableWidget;
class QTableWidgetItem;
class QPushButton;
class QLabel;

// DangerousDialog — segment-level delete / archive for a single trip.
//
// Displays a table of trip segments, one row per segment, one column per
// active camera slot.  User checks rows and clicks "Permanently Delete" or
// "Move to Archive".
//
// Archive destination: <sourcePath>/Archive/<original-relative-path>
// The Archive directory is excluded from trip detection automatically because
// scanSlot only iterates the slot-specific subdirectory (e.g. Front/), not
// the Archive subtree.
//
// Emits manifestChanged() after any operation so the caller can reload the
// manifest panel.

class DangerousDialog : public QDialog {
    Q_OBJECT
public:
    explicit DangerousDialog(const CamClops::Trip& trip,
                             const std::string& sourcePath,
                             QWidget* parent = nullptr);

signals:
    void manifestChanged();

private slots:
    void onPermanentDelete();
    void onMoveToArchive();
    void onTableItemChanged(QTableWidgetItem* item);

private:
    // Returns the original segment indices (stored via Qt::UserRole) of all
    // checked rows.
    std::vector<int> checkedSegmentIndices() const;

    // Perform file operations on the given segment indices.
    //   archive=false → delete;  archive=true → move to Archive/
    void performOps(const std::vector<int>& segIndices, bool archive);

    // Rewrite the manifest, removing the given segment indices from m_trip.
    // Trips left with 0 segments are purged entirely.
    void updateManifest(const std::vector<int>& removedSegIdx);

    // Build the archive destination path for a file, or "" if the file is not
    // under m_sourcePath (cannot archive safely).
    std::string archiveDestPath(const std::string& absPath) const;

    // Remove table rows whose UserRole segment index is in the given list.
    void removeProcessedRows(const std::vector<int>& segIndices);

    // Update status label and enable/disable action buttons.
    void refreshActionState();

    CamClops::Trip            m_trip;
    std::string              m_sourcePath;
    std::vector<std::string> m_slots;   // active camera slot names, canonical order

    QTableWidget* m_table;
    QPushButton*  m_deleteBtn;
    QPushButton*  m_archiveBtn;
    QLabel*       m_statusLabel;
};
// SN: 00097
