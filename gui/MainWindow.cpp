// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "MainWindow.h"
#include "ManifestPanel.h"
#include "TripGridPanel.h"
#include "ScanProgressDialog.h"
#include "AboutDialog.h"
#include "SettingsDialog.h"
#include "ManifestManagerDialog.h"
#include "SetupWizard.h"
#include "HelpDialog.h"
#include "CameraProfilesDialog.h"
#include "profile_detector.hpp"
#include <QSplitter>
#include <QFileDialog>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QKeySequence>
#include <QSizeGrip>
#include <QAbstractButton>
#include <QPushButton>
#include <QMessageBox>
#include <QInputDialog>

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

    // Add a visible size grip so the bottom-right corner is easy to grab on
    // HiDPI displays where the WM resize border is physically tiny.
    // QSizeGrip auto-positions itself at the bottom-right corner of its parent
    // top-level window and tracks resizes — no manual placement needed.
    new QSizeGrip(this);

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

    QAction* wizardAct = toolsMenu->addAction("Setup &Wizard\u2026");
    connect(wizardAct, &QAction::triggered, this, &MainWindow::onSetupWizard);

    QAction* profilesAct = toolsMenu->addAction("Camera &Profiles\u2026");
    connect(profilesAct, &QAction::triggered, this, &MainWindow::onCameraProfiles);

    toolsMenu->addSeparator();

    QAction* probeAct = toolsMenu->addAction("&Probe SD Card\u2026");
    probeAct->setEnabled(false);       // placeholder

    // ---- Help ----
    QMenu* helpMenu = menuBar()->addMenu("&Help");

    QAction* helpAct = helpMenu->addAction("&Contents");
    helpAct->setShortcut(QKeySequence::HelpContents);
    connect(helpAct, &QAction::triggered, this, &MainWindow::onHelp);

    helpMenu->addSeparator();

    QAction* aboutAct = helpMenu->addAction("&About PathMux");
    connect(aboutAct, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::onAbout()
{
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::onHelp()
{
    if (!m_helpDialog) {
        m_helpDialog = new HelpDialog(this);
        m_helpDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_helpDialog, &QObject::destroyed, this, [this]() {
            m_helpDialog = nullptr;
        });
    }
    m_helpDialog->show();
    m_helpDialog->raise();
    m_helpDialog->activateWindow();
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

void MainWindow::onCameraProfiles()
{
    CameraProfilesDialog dlg(this);
    dlg.exec();
}

void MainWindow::onSetupWizard()
{
    // Pre-populate wizard with current settings so existing users see their
    // configuration and can update specific parts (e.g. swap encoder preset
    // after adding a better GPU) without having to hunt for each setting.
    Pathmux::ConfigManager config;
    config.loadSettings();
    const Pathmux::AppSettings& s = config.getSettings();

    SetupWizard wiz(this);
    wiz.setField("ffmpegPath",    QString::fromStdString(s.ffmpegPath));
    wiz.setField("exiftoolPath",  QString::fromStdString(s.exiftoolPath));
    wiz.setField("exportDir",     QString::fromStdString(s.defaultExportDir));
    wiz.setField("encoderPreset", QString::fromStdString(s.encode.preset));

    if (wiz.exec() == QDialog::Accepted) {
        m_manifestPanel->refresh();
        m_tripGridPanel->refreshPageState();
    }
}

// Resolve which camera profile to use for a fresh scan of dir.
// Checks for a stored profile_id in an existing manifest first;
// if absent, runs auto-detection and prompts the user to confirm.
// Returns the chosen profile_id, or "" to cancel.
static QString resolveProfileForScan(const QString& dir, QWidget* parent)
{
    Pathmux::ConfigManager config;
    config.loadSettings();

    // If this directory already has a manifest with a recorded profile, use it silently.
    std::string stored = config.getManifestProfileId(dir.toStdString());
    if (!stored.empty())
        return QString::fromStdString(stored);

    // Auto-detect from the directory contents.
    auto allProfiles = config.loadAllProfiles();
    auto match = Pathmux::detectProfile(dir.toStdString(), allProfiles);

    // Mixed-content warning: multiple camera profiles found in the same path.
    if (match.isMixedContent) {
        QString names;
        for (const auto& p : match.mixedProfiles)
            names += QString("• %1 (%2)\n")
                     .arg(QString::fromStdString(p.name))
                     .arg(QString::fromStdString(p.profileId));
        QMessageBox warn(parent);
        warn.setIcon(QMessageBox::Warning);
        warn.setWindowTitle("Mixed Footage Warning");
        warn.setText(
            "Multiple camera profiles were detected in this directory:\n\n"
            + names +
            "\nScanning a path with mixed dashcam footage will produce "
            "incorrect trip groupings.\n\n"
            "It is strongly recommended to scan each camera’s "
            "footage directory separately.");
        auto* continueBtn = warn.addButton("Continue Anyway", QMessageBox::DestructiveRole);
        warn.addButton("Cancel", QMessageBox::RejectRole);
        warn.setDefaultButton(
            qobject_cast<QPushButton*>(warn.button(QMessageBox::Cancel)));
        warn.exec();
        if (warn.clickedButton() != static_cast<QAbstractButton*>(continueBtn))
            return {};
    }

    if (match.hasMatch()) {
        QString name    = QString::fromStdString(match.profile.name);
        QString pid     = QString::fromStdString(match.profile.profileId);
        QString ambig   = match.isAmbiguous ? " (best guess)" : "";
        QString msg     = QString("Detected camera profile:\n\n  %1 (%2)%3\n\n"
                                  "%4 primary file(s) matched.\n\n"
                                  "Use this profile for the scan?")
                          .arg(name).arg(pid).arg(ambig)
                          .arg(match.primaryFiles);

        QMessageBox box(parent);
        box.setWindowTitle("Camera Profile");
        box.setText(msg);
        auto* useBtn    = box.addButton("Use This Profile", QMessageBox::AcceptRole);
        auto* chooseBtn = box.addButton("Choose Different...", QMessageBox::ActionRole);
        box.addButton("Cancel", QMessageBox::RejectRole);
        box.exec();

        if (box.clickedButton() == static_cast<QAbstractButton*>(useBtn))
            return pid;
        if (box.clickedButton() != static_cast<QAbstractButton*>(chooseBtn))
            return {};   // cancelled
        // fall through to "Choose Different" list
    } else {
        QMessageBox box(parent);
        box.setWindowTitle("Camera Profile");
        box.setText("No known camera profile matched this directory.\n\n"
                    "You can choose a profile manually, or run the\n"
                    "Setup Wizard to create one for this camera.");
        auto* chooseBtn = box.addButton("Choose Profile...", QMessageBox::ActionRole);
        box.addButton("Cancel", QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != static_cast<QAbstractButton*>(chooseBtn))
            return {};
        // fall through to list
    }

    // Build a list of all known profiles for the user to pick from.
    QStringList items;
    for (const auto& p : allProfiles)
        items << QString("%1 (%2)").arg(
                   QString::fromStdString(p.name),
                   QString::fromStdString(p.profileId));

    bool ok = false;
    QString chosen = QInputDialog::getItem(
        parent, "Select Camera Profile", "Profile:", items, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return {};

    // Extract profile_id from "Name (id)" format.
    int lp = chosen.lastIndexOf('(');
    int rp = chosen.lastIndexOf(')');
    if (lp >= 0 && rp > lp)
        return chosen.mid(lp + 1, rp - lp - 1);
    return {};
}

void MainWindow::onScanRequested()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select Dashcam Source Directory", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;

    QString profileId = resolveProfileForScan(dir, this);
    if (profileId.isNull()) return;   // user cancelled

    ScanProgressDialog dlg(this);
    connect(&dlg, &ScanProgressDialog::scanComplete,
            this, &MainWindow::onScanComplete);
    dlg.startScan(dir, profileId);
    dlg.exec();
}

void MainWindow::onZoomChanged(double factor)
{
    m_tripGridPanel->setZoom(factor);
    m_manifestPanel->setZoom(factor);
}

void MainWindow::onRebuildRequested(const Pathmux::ManifestEntry& entry)
{
    // Rescan using the profile stored in the existing manifest — no prompt needed.
    Pathmux::ConfigManager config;
    config.loadSettings();
    QString profileId = QString::fromStdString(
        config.getManifestProfileId(entry.path));

    ScanProgressDialog dlg(this);
    connect(&dlg, &ScanProgressDialog::scanComplete,
            this, &MainWindow::onScanComplete);
    dlg.startScan(QString::fromStdString(entry.path), profileId);
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
// SN: 00104
