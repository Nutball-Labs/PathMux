// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "ScanProgressDialog.h"
#include "camclops.hpp"
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QDialogButtonBox>

using namespace CamClops;

// ---------------------------------------------------------------------------
// ScanWorker — runs detectTrips() + saveTripCache() on a background thread.
// Defined here; Q_OBJECT in a .cpp requires the .moc include at EOF.
// ---------------------------------------------------------------------------
class ScanWorker : public QObject {
    Q_OBJECT
public:
    QString profileId;   // hint from caller (from getManifestProfileId); used only when no manifest exists yet
public slots:
    void startScan(const QString& path) {
        try {
            ConfigManager config;
            config.loadSettings();
            std::string stdPath = path.toStdString();

            // If a manifest already exists, use its embedded profile so the same
            // camera layout is applied consistently on every rescan.  On first scan
            // (no manifest yet) fall back to the profileId hint from the caller.
            CameraProfile profile;
            if (!config.lookupManifestFilePath(stdPath).empty()) {
                profile = config.getManifestProfile(stdPath);
            } else {
                if (!profileId.isEmpty()) {
                    auto s = config.getSettings();
                    s.activeProfileId = profileId.toStdString();
                    config.applySettings(s);
                }
                profile = config.getCameraProfile();
            }

            TripDetection td;
            auto trips = td.detectTrips(
                stdPath, profile,
                config.getGapThreshold(),
                config.getFuzzyWindow(),
                config.getFfprobePath());
            config.saveTripCache(stdPath, trips, profile);
            emit finished(true, "");
        } catch (const std::exception& ex) {
            emit finished(false, QString::fromLatin1(ex.what()));
        } catch (...) {
            emit finished(false, "Unknown error during scan");
        }
    }
signals:
    void finished(bool ok, const QString& error);
};

// ---------------------------------------------------------------------------
// ScanProgressDialog
// ---------------------------------------------------------------------------
ScanProgressDialog::ScanProgressDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Scanning Source Directory");
    setModal(true);
    setMinimumWidth(420);
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);

    auto* vlay = new QVBoxLayout(this);
    vlay->setSpacing(10);
    vlay->setContentsMargins(16, 16, 16, 16);

    m_pathLabel = new QLabel;
    m_pathLabel->setWordWrap(true);
    vlay->addWidget(m_pathLabel);

    m_statusLabel = new QLabel("Preparing scan...");
    vlay->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 0);   // indeterminate until we know trip count
    vlay->addWidget(m_progressBar);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setEnabled(false);  // can't cancel mid-scan yet
    auto* bbox = new QDialogButtonBox;
    bbox->addButton(m_cancelBtn, QDialogButtonBox::RejectRole);
    vlay->addWidget(bbox);
}

void ScanProgressDialog::startScan(const QString& sourcePath, const QString& profileId)
{
    m_scanPath = sourcePath;
    m_pathLabel->setText(sourcePath);
    m_statusLabel->setText("Scanning for trips — this may take a moment...");

    QThread*     thread = new QThread(this);
    ScanWorker*  worker = new ScanWorker;
    worker->profileId = profileId;
    worker->moveToThread(thread);

    connect(thread, &QThread::started,
            worker, [worker, sourcePath]() { worker->startScan(sourcePath); });
    connect(worker, &ScanWorker::finished,
            this,   &ScanProgressDialog::onScanFinished);
    connect(worker, &ScanWorker::finished,
            thread, &QThread::quit);
    connect(thread, &QThread::finished,
            worker, &QObject::deleteLater);

    thread->start();
}

void ScanProgressDialog::onScanFinished(bool ok, const QString& error)
{
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(ok ? 100 : 0);

    if (ok) {
        m_statusLabel->setText("Scan complete.");
        // Find the ManifestEntry for the scanned path to emit
        ConfigManager config;
        config.loadSettings();
        auto index = config.loadManifestIndex();
        for (const auto& e : index) {
            if (QString::fromStdString(e.path) == m_scanPath) {
                emit scanComplete(e);
                break;
            }
        }
        accept();
    } else {
        m_statusLabel->setText("Error: " + error);
        m_cancelBtn->setText("Close");
        m_cancelBtn->setEnabled(true);
        connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    }
}

// Required: Q_OBJECT in a .cpp file needs its own moc output included here
#include "ScanProgressDialog.moc"
// SN: 00104
