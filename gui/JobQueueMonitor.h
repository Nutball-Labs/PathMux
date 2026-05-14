// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QObject>
#include <QString>

// Writes /tmp/camclops_jobs.json whenever the JobQueue changes so that
// clops_monitor.py can serve live status to any browser on the local network.
//
// Debounced: rapid progressChanged signals are coalesced into a single write
// every DEBOUNCE_MS milliseconds to avoid thrashing the filesystem.
//
// Instantiate once in main() after the QApplication exists.
class JobQueueMonitor : public QObject {
    Q_OBJECT
public:
    explicit JobQueueMonitor(const QString& statusFile
                                 = "/tmp/camclops_jobs.json",
                             QObject* parent = nullptr);

private slots:
    void scheduleWrite();   // called by any queue/job signal
    void doWrite();         // actual JSON write (fires after debounce timer)

private:
    QString  m_path;
    class QTimer* m_timer = nullptr;

    static constexpr int DEBOUNCE_MS = 250;
};
// SN: 00113
