// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "MainWindow.h"
#include "ManifestPanel.h"
#include "TripGridPanel.h"
#include "ScanProgressDialog.h"
#include "AboutDialog.h"
#include "SettingsDialog.h"
#include "ManifestManagerDialog.h"
#include <QSplitter>
#include <QFileDialog>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QKeySequence>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("PathMux Dashcam Explorer");
    resize(1200, 700);

    m_splitter      = new QSplitter(Qt::Horizontal, this);
    m_manifestPanel = new ManifestPanel(m_splitter);
    m_tripGridPanel = new TripGridPanel(m_splitter);

    m_splitter->addWidget(m_manifestPanel);
    m_splitter->addWidget(m_tripGridPanel);
    m_splitter->setStretchFactor(0, 0);   // left pane: fixed
    m_splitter->setStretchFactor(1, 1);   // right pane: expands
    m_splitter->setSizes({280, 920});

    setCentralWidget(m_splitter);
    buildMenuBar();

    connect(m_manifestPanel, &ManifestPanel::manifestSelected,
            m_tripGridPanel, &TripGridPanel::loadManifest);
    connect(m_manifestPanel, &ManifestPanel::scanRequested,
            this,            &MainWindow::onScanRequested);
    connect(m_manifestPanel, &ManifestPanel::rebuildRequested,
            this,            &MainWindow::onRebuildRequested);
    connect(m_tripGridPanel, &TripGridPanel::scanRequested,
            this,            &MainWindow::onScanRequested);

    // Ctrl+scroll zoom: whichever panel the user is hovering drives the zoom,
    // MainWindow distributes to both so they stay in sync.
    connect(m_tripGridPanel, &TripGridPanel::zoomChanged,
            this,            &MainWindow::onZoomChanged);
    connect(m_manifestPanel, &ManifestPanel::zoomChanged,
            this,            &MainWindow::onZoomChanged);
}

void MainWindow::buildMenuBar()
{
    // ---- File ----
    QMenu* fileMenu = menuBar()->addMenu("&File");

    QAction* quitAct = fileMenu->addAction("&Quit");
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &MainWindow::close);

    // ---- View ----
    QMenu* viewMenu = menuBar()->addMenu("&View");

    QAction* zoomInAct = viewMenu->addAction("Zoom &In");
    zoomInAct->setShortcut(QKeySequence::ZoomIn);
    zoomInAct->setEnabled(false);   // driven by Ctrl+scroll for now

    QAction* zoomOutAct = viewMenu->addAction("Zoom &Out");
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    zoomOutAct->setEnabled(false);

    QAction* zoomResetAct = viewMenu->addAction("&Reset Zoom");
    zoomResetAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    zoomResetAct->setEnabled(false);

    // ---- Manifests ----
    QMenu* manifestsMenu = menuBar()->addMenu("&Manifests");

    QAction* scanAct = manifestsMenu->addAction("&Scan Source Directory\u2026");
    scanAct->setShortcut(QKeySequence::Open);
    connect(scanAct, &QAction::triggered, this, &MainWindow::onScanRequested);

    manifestsMenu->addSeparator();

    QAction* manageAct = manifestsMenu->addAction("&Manage Manifests\u2026");
    connect(manageAct, &QAction::triggered, this, &MainWindow::onManageManifests);

    // ---- Trips ----
    QMenu* tripsMenu = menuBar()->addMenu("&Trips");

    QAction* exportGpsAct = tripsMenu->addAction("&Export GPS Track\u2026");
    exportGpsAct->setEnabled(false);   // placeholder

    QAction* buildVideoAct = tripsMenu->addAction("&Build Video\u2026");
    buildVideoAct->setEnabled(false);  // placeholder

    tripsMenu->addSeparator();

    QAction* validateAct = tripsMenu->addAction("&Validate Manifest");
    validateAct->setEnabled(false);    // placeholder

    // ---- Tools ----
    QMenu* toolsMenu = menuBar()->addMenu("&Tools");

    QAction* settingsAct = toolsMenu->addAction("&Settings\u2026");
    settingsAct->setShortcut(QKeySequence::Preferences);
    connect(settingsAct, &QAction::triggered, this, &MainWindow::onSettings);

    toolsMenu->addSeparator();

    QAction* probeAct = toolsMenu->addAction("&Probe SD Card\u2026");
    probeAct->setEnabled(false);       // placeholder

    // ---- Help ----
    QMenu* helpMenu = menuBar()->addMenu("&Help");

    QAction* aboutAct = helpMenu->addAction("&About PathMux");
    connect(aboutAct, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::onAbout()
{
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::onSettings()
{
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        // Reload in case display prefs changed
        m_manifestPanel->refresh();
        m_tripGridPanel->refreshPageState();
    }
}

void MainWindow::onManageManifests()
{
    ManifestManagerDialog dlg(this);
    connect(&dlg, &ManifestManagerDialog::manifestsChanged,
            m_manifestPanel, &ManifestPanel::refresh);
    connect(&dlg, &ManifestManagerDialog::manifestsChanged,
            m_tripGridPanel, &TripGridPanel::refreshPageState);
    dlg.exec();
}

void MainWindow::onScanRequested()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select Dashcam Source Directory", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;

    ScanProgressDialog dlg(this);
    connect(&dlg, &ScanProgressDialog::scanComplete,
            this, &MainWindow::onScanComplete);
    dlg.startScan(dir);
    dlg.exec();
}

void MainWindow::onZoomChanged(double factor)
{
    m_tripGridPanel->setZoom(factor);
    m_manifestPanel->setZoom(factor);
}

void MainWindow::onRebuildRequested(const Pathmux::ManifestEntry& entry)
{
    // Rescan the known path — no directory picker needed.
    ScanProgressDialog dlg(this);
    connect(&dlg, &ScanProgressDialog::scanComplete,
            this, &MainWindow::onScanComplete);
    dlg.startScan(QString::fromStdString(entry.path));
    dlg.exec();
}

void MainWindow::onScanComplete(const Pathmux::ManifestEntry& entry)
{
    m_manifestPanel->refresh();
    m_tripGridPanel->refreshPageState();
    // Auto-select the just-scanned manifest so trips appear immediately
    m_tripGridPanel->loadManifest(entry);
    m_manifestPanel->selectEntry(entry);
}
// SN: 00095
