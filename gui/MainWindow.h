// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QMainWindow>
#include "config_manager.hpp"

class QSplitter;
class ManifestPanel;
class TripGridPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onScanRequested();
    void onScanComplete(const Pathmux::ManifestEntry& entry);
    void onZoomChanged(double factor);
    void onAbout();

private:
    void buildMenuBar();

    QSplitter*     m_splitter;
    ManifestPanel* m_manifestPanel;
    TripGridPanel* m_tripGridPanel;
};
// SN: 00090
