// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "MainWindow.h"
#include "ManifestPanel.h"
#include "TripGridPanel.h"
#include "TripSearchDialog.h"
#include "JobQueue.h"
#include "JobQueuePanel.h"
#include "AboutDialog.h"
#include "SettingsDialog.h"
#include "ManifestManagerDialog.h"
#include "SetupWizard.h"
#include "HelpDialog.h"
#include "CameraProfilesDialog.h"
#include "profile_detector.hpp"
#include <QDockWidget>
#include <QCloseEvent>
#include <QEvent>
#include <QFrame>
#include <QSplitter>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QKeySequence>

#include <QProcess>
#include <QCoreApplication>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QMessageBox>
#include <QInputDialog>
#include <QClipboard>
#include <QGuiApplication>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("CamClops Dashcam Explorer");
    resize(1200, 700);

    m_splitter = new QSplitter(Qt::Horizontal, this);

    // Wrap each panel in a named QFrame so the paneFrame stylesheet border renders.
    auto* manifestFrame = new QFrame(m_splitter);
    manifestFrame->setObjectName("paneFrame");
    {
        auto* lay = new QVBoxLayout(manifestFrame);
        lay->setContentsMargins(0, 0, 0, 0);
        m_manifestPanel = new ManifestPanel(manifestFrame);
        lay->addWidget(m_manifestPanel);
    }

    auto* tripFrame = new QFrame(m_splitter);
    tripFrame->setObjectName("paneFrame");
    {
        auto* lay = new QVBoxLayout(tripFrame);
        lay->setContentsMargins(0, 0, 0, 0);
        m_tripGridPanel = new TripGridPanel(tripFrame);
        lay->addWidget(m_tripGridPanel);
    }

    m_splitter->addWidget(manifestFrame);
    m_splitter->addWidget(tripFrame);
    m_splitter->setStretchFactor(0, 0);   // left pane: fixed
    m_splitter->setStretchFactor(1, 1);   // right pane: expands
    m_splitter->setSizes({280, 920});

    setCentralWidget(m_splitter);

    // --- Job Queue dock ---
    m_jobPanel = new JobQueuePanel(this);
    m_jobDock  = new QDockWidget("Job Queue", this);
    m_jobDock->setWidget(m_jobPanel);
    m_jobDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    m_jobDock->setMinimumHeight(120);
    addDockWidget(Qt::BottomDockWidgetArea, m_jobDock);
    m_jobDock->hide();

    // Apply initial placement from settings
    {
        CamClops::ConfigManager cfg;
        cfg.loadSettings();
        if (cfg.getSettings().jobQueueMode == "window")
            m_jobDock->setFloating(true);
    }

    // Auto-show when a job is submitted — but only if not currently detached.
    connect(&JobQueue::instance(), &JobQueue::jobAdded,
            this, [this](Job*) {
        if (!m_jobFloatWin) {
            m_jobDock->show();
            m_jobDock->raise();
        }
    });

    // Keep Detach button in sync when dock is dragged to float/re-dock.
    connect(m_jobDock, &QDockWidget::topLevelChanged,
            m_jobPanel, &JobQueuePanel::onDockStateChanged);

    // Detach button: create a real Qt::Window widget so the OS always provides
    // full window decorations (title bar, min/max/close).  QDockWidget::setFloating
    // uses Qt::Tool which some WMs don't decorate fully.
    connect(m_jobPanel, &JobQueuePanel::detachRequested, this, [this]() {
        if (m_jobFloatWin) { m_jobFloatWin->raise(); m_jobFloatWin->activateWindow(); return; }

        m_jobFloatWin = new QWidget(nullptr, Qt::Window);
        m_jobFloatWin->setObjectName("jobFloatWin");
        m_jobFloatWin->setWindowTitle("Job Queue");
        m_jobFloatWin->setMinimumSize(500, 160);
        m_jobFloatWin->resize(800, 240);
        m_jobFloatWin->setAttribute(Qt::WA_QuitOnClose, false);
        m_jobFloatWin->installEventFilter(this);

        auto* lay = new QVBoxLayout(m_jobFloatWin);
        lay->setContentsMargins(0, 0, 0, 0);
        m_jobDock->setWidget(nullptr);   // clear dock ref before reparenting
        m_jobPanel->setParent(m_jobFloatWin);
        lay->addWidget(m_jobPanel);
        m_jobPanel->show();              // setParent() + setWidget(nullptr) both hide; re-show
        m_jobPanel->onDockStateChanged(true);   // hide Detach button, mark detached

        m_jobDock->hide();
        m_jobFloatWin->show();
    });

    // When docked, Ctrl+scroll over the job panel participates in the shared zoom.
    connect(m_jobPanel, &JobQueuePanel::zoomChanged,
            this, &MainWindow::onZoomChanged);

    buildMenuBar();

    connect(m_manifestPanel, &ManifestPanel::manifestSelected,
            this,            &MainWindow::onManifestSelected);
    connect(m_manifestPanel, &ManifestPanel::virtualEntryRemoved,
            this, [this](const QString& id) {
        m_searchResultsMap.remove(id);
        m_tripGridPanel->refreshPageState();
    });
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

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_jobFloatWin && event->type() == QEvent::Close) {
        // Rescue the panel before the window destroys its children,
        // return it to the dock (hidden). Don't show the dock — just go away.
        m_jobPanel->setParent(nullptr);
        m_jobDock->setWidget(m_jobPanel);
        m_jobPanel->onDockStateChanged(false);  // restore Detach button, mark docked
        m_jobFloatWin = nullptr;
        // Let default close handling delete the window.
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    const auto& jobs = JobQueue::instance().jobs();
    int running = 0, queued = 0;
    for (const auto* job : jobs) {
        if (job->state() == Job::State::Running) ++running;
        if (job->state() == Job::State::Queued)  ++queued;
    }

    if (running + queued > 0) {
        QString detail;
        if (running && queued)
            detail = QString("%1 running, %2 queued").arg(running).arg(queued);
        else if (running)
            detail = QString("%1 running").arg(running);
        else
            detail = QString("%1 queued").arg(queued);

        QMessageBox dlg(this);
        dlg.setWindowTitle("Jobs Active");
        dlg.setText(QString("There are jobs still active (%1).\n\nKill all jobs and close?").arg(detail));
        dlg.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        dlg.setDefaultButton(QMessageBox::Cancel);
        dlg.button(QMessageBox::Yes)->setText("Kill && Close");

        if (dlg.exec() != QMessageBox::Yes) {
            event->ignore();
            return;
        }

        for (auto* job : jobs)
            if (job->state() == Job::State::Running || job->state() == Job::State::Queued)
                job->cancel();
    }

    event->accept();
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

    viewMenu->addSeparator();

    QAction* queueAct = viewMenu->addAction("&Job Queue");
    queueAct->setCheckable(true);
    queueAct->setChecked(false);
    queueAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    connect(queueAct, &QAction::triggered, this, [this](bool checked) {
        if (checked) { m_jobDock->show(); m_jobDock->raise(); }
        else           m_jobDock->hide();
    });
    connect(m_jobDock, &QDockWidget::visibilityChanged,
            queueAct,  &QAction::setChecked);

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

    QAction* searchAct = tripsMenu->addAction("&Search Trips\u2026");
    searchAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
    connect(searchAct, &QAction::triggered, this, &MainWindow::onSearchTrips);

    tripsMenu->addSeparator();

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

    toolsMenu->addSeparator();

    QAction* tlAct = toolsMenu->addAction("&Timelapse Editor\u2026");
    tlAct->setStatusTip("Open camclops-tl \u2014 create timelapse sections from a collage MP4");
    connect(tlAct, &QAction::triggered, this, [this]{
        QString appDir = QCoreApplication::applicationDirPath();
        // camclops-tl lives alongside camclops-gui in the same bin directory.
        QStringList candidates = {
            appDir + "/camclops-tl",
            appDir + "/../bin/camclops-tl",
            appDir + "/camclops-tl.exe",     // Windows
        };
        QString exe;
        for (const QString& c : candidates) {
            if (QFile::exists(c)) { exe = c; break; }
        }
        if (exe.isEmpty()) {
            QMessageBox::warning(this, "camclops-tl not found",
                "Could not find camclops-tl alongside camclops-gui.\n"
                "Build it with: cmake --build build-linux --target camclops-tl");
            return;
        }
        QProcess::startDetached(exe, {});
    });

    // ---- Help ----
    QMenu* helpMenu = menuBar()->addMenu("&Help");

    QAction* helpAct = helpMenu->addAction("&Contents");
    helpAct->setShortcut(QKeySequence::HelpContents);
    connect(helpAct, &QAction::triggered, this, &MainWindow::onHelp);

    helpMenu->addSeparator();

    QAction* aboutAct = helpMenu->addAction("&About CamClops");
    connect(aboutAct, &QAction::triggered, this, &MainWindow::onAbout);

    helpMenu->addSeparator();

    QAction* uninstallAct = helpMenu->addAction("Uninstall CamClops\u2026");
    connect(uninstallAct, &QAction::triggered, this, &MainWindow::onUninstall);
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

void MainWindow::onUninstall()
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Uninstall CamClops");
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText("<b>Uninstall CamClops</b>");

#ifdef Q_OS_MACOS
    msgBox.setInformativeText(
        "To uninstall CamClops, open Terminal and run:\n\n"
        "    sudo camclops-uninstall\n\n"
        "This removes both GUI apps, all CLI tools, and the shared library.\n"
        "Your footage files and manifests are never deleted.\n"
        "The script will ask whether to also remove your configuration.");

    QPushButton* copyBtn = msgBox.addButton("Copy Command", QMessageBox::ActionRole);
    msgBox.addButton(QMessageBox::Cancel);
    msgBox.exec();

    if (msgBox.clickedButton() == copyBtn)
        QGuiApplication::clipboard()->setText("sudo camclops-uninstall");
#else
    msgBox.setInformativeText(
        "To uninstall CamClops, use your system package manager:\n\n"
        "  RPM-based:  sudo rpm -e camclops\n"
        "  DEB-based:  sudo dpkg -r camclops");
    msgBox.addButton(QMessageBox::Ok);
    msgBox.exec();
#endif
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
    CamClops::ConfigManager config;
    config.loadSettings();
    const CamClops::AppSettings& s = config.getSettings();

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
    CamClops::ConfigManager config;
    config.loadSettings();

    // If this directory already has a manifest with a recorded profile, use it silently.
    std::string stored = config.getManifestProfileId(dir.toStdString());
    if (!stored.empty())
        return QString::fromStdString(stored);

    // Auto-detect from the directory contents.
    auto allProfiles = config.loadAllProfiles();
    auto match = CamClops::detectProfile(dir.toStdString(), allProfiles);

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

    auto* job = new ManifestScanJob(dir, profileId);
    connect(job, &Job::finished, this, [this, job](bool ok) {
        if (ok) onScanComplete(job->completedEntry());
    });
    JobQueue::instance().enqueue(job);
}

void MainWindow::onZoomChanged(double factor)
{
    m_tripGridPanel->setZoom(factor);
    m_manifestPanel->setZoom(factor);
    if (!m_jobFloatWin)          // only sync when docked; float window zooms independently
        m_jobPanel->setZoom(factor);
}

void MainWindow::onRebuildRequested(const CamClops::ManifestEntry& entry)
{
    // Rescan using the profile stored in the existing manifest — no prompt needed.
    CamClops::ConfigManager config;
    config.loadSettings();
    QString profileId = QString::fromStdString(
        config.getManifestProfileId(entry.path));

    auto* job = new ManifestScanJob(
        QString::fromStdString(entry.path), profileId);
    connect(job, &Job::finished, this, [this, job](bool ok) {
        if (ok) onScanComplete(job->completedEntry());
    });
    JobQueue::instance().enqueue(job);
}

void MainWindow::onScanComplete(const CamClops::ManifestEntry& entry)
{
    m_manifestPanel->refresh();
    m_tripGridPanel->refreshPageState();
    m_tripGridPanel->loadManifest(entry);
    m_manifestPanel->selectEntry(entry);
}

void MainWindow::onManifestSelected(const CamClops::ManifestEntry& entry)
{
    if (entry.isVirtual) {
        auto it = m_searchResultsMap.find(QString::fromStdString(entry.id));
        if (it != m_searchResultsMap.end())
            m_tripGridPanel->loadSearchResults(it.value(), entry);
    } else {
        m_tripGridPanel->loadManifest(entry);
    }
}

void MainWindow::onSearchTrips()
{
    auto* dlg = new TripSearchDialog(this);
    connect(dlg, &TripSearchDialog::searchCompleted,
            this, &MainWindow::onSearchCompleted);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::onSearchCompleted(QList<SearchResult> results, QString label)
{
    // "SR" is a reserved manifest ID — never generated by the base36 random ID
    // generator (which only produces digit+letter pairs from 0-9 and A-Z).
    static constexpr const char* kVirtualId = "SR";

    CamClops::ManifestEntry ve;
    ve.id        = kVirtualId;
    ve.isVirtual = true;
    ve.tripCount = results.size();
    ve.nickname  = label.toStdString();
    ve.path      = "(search results)";

    m_searchResultsMap[kVirtualId] = results;
    m_manifestPanel->addVirtualEntry(ve);
    m_manifestPanel->selectEntry(ve);
    m_tripGridPanel->loadSearchResults(results, ve);
}
// SN: 00122
