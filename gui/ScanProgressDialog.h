// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include "config_manager.hpp"

class QLabel;
class QProgressBar;
class QPushButton;

class ScanProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScanProgressDialog(QWidget* parent = nullptr);

    // Call before exec() — starts the background scan thread
    void startScan(const QString& sourcePath);

signals:
    void scanComplete(const Pathmux::ManifestEntry& entry);

private slots:
    void onScanFinished(bool ok, const QString& error);

private:
    QLabel*       m_pathLabel;
    QLabel*       m_statusLabel;
    QProgressBar* m_progressBar;
    QPushButton*  m_cancelBtn;
    QString       m_scanPath;
};
// SN: 00090
