// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QWidget>
#include "JobQueue.h"

class QLabel;
class QProcess;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWheelEvent;
class JobRowWidget;

// ─── JobQueuePanel ────────────────────────────────────────────────────────────
// Full-width panel showing all queued and completed jobs.
// Designed to live inside a QDockWidget docked at the bottom of MainWindow,
// or floating as a separate window.
class JobQueuePanel : public QWidget {
    Q_OBJECT
public:
    explicit JobQueuePanel(QWidget* parent = nullptr);

    // Called by MainWindow when the dock's floating state changes.
    void onDockStateChanged(bool floating);

    // Zoom — same Ctrl+scroll behaviour as the main window panels.
    // setZoom() is called by MainWindow to synchronize when docked.
    // When detached, Ctrl+scroll zooms the floating window independently.
    void setZoom(double factor);

    static constexpr double ZOOM_MIN  = 0.5;
    static constexpr double ZOOM_MAX  = 3.0;
    static constexpr double ZOOM_STEP = 0.1;

signals:
    void detachRequested();
    void zoomChanged(double factor);

private slots:
    void onJobAdded(Job* job);
    void onMonitorToggled(bool on);

protected:
    void wheelEvent(QWheelEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateTitle();
    void applyZoom();
    void dismissRow(class JobRowWidget* row);

    QLabel*      m_titleLbl     = nullptr;   // counter in status bar
    QPushButton* m_detachBtn    = nullptr;
    QLabel*      m_monitorLabel = nullptr;   // URL shown when monitor is running
    class ToggleSwitch* m_monitorToggle = nullptr;
    QProcess*    m_monitorProc  = nullptr;
    QScrollArea* m_scrollArea   = nullptr;
    QVBoxLayout* m_rowLayout    = nullptr;

    double m_zoomFactor = 1.0;
    bool   m_isDetached = false;
};
// SN: 00113
