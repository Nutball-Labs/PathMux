// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QMainWindow>
#include <QList>
#include <QMap>
#include <QString>
#include "config_manager.hpp"
#include "TripSearchDialog.h"

class QDockWidget;
class QSplitter;
class ManifestPanel;
class TripGridPanel;
class JobQueuePanel;
class HelpDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onScanRequested();
    void onRebuildRequested(const CamClops::ManifestEntry& entry);
    void onScanComplete(const CamClops::ManifestEntry& entry);
    void onManifestSelected(const CamClops::ManifestEntry& entry);
    void onSearchTrips();
    void onSearchCompleted(QList<SearchResult> results, QString label);
    void onZoomChanged(double factor);
    void onAbout();
    void onHelp();
    void onUninstall();
    void onSettings();
    void onManageManifests();
    void onSetupWizard();
    void onCameraProfiles();

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildMenuBar();

    QSplitter*     m_splitter;
    ManifestPanel* m_manifestPanel;
    TripGridPanel* m_tripGridPanel;
    HelpDialog*    m_helpDialog   = nullptr;
    QDockWidget*   m_jobDock      = nullptr;
    JobQueuePanel* m_jobPanel     = nullptr;
    QWidget*       m_jobFloatWin  = nullptr;

    // Virtual search result manifests: id → result list
    QMap<QString, QList<SearchResult>> m_searchResultsMap;
};
// SN: 00119

