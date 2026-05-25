// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "ScanProgressDialog.h"
#include "camclops.hpp"
#include <filesystem>
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QDialogButtonBox>

using namespace CamClops;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// ScanWorker — runs detectTrips() + saveTripCache() on a background thread.
// Defined here; Q_OBJECT in a .cpp requires the .moc include at EOF.
// ---------------------------------------------------------------------------
class ScanWorker : public QObject {
    Q_OBJECT
public:
    QString profileId;
public slots:
    void startScan(const QString& path) {
        try {
            ConfigManager config;
            config.loadSettings();
            std::string stdPath = path.toStdString();

            // If a manifest already exists, use its embedded profile so the same
            // camera layout is applied consistently on every rescan.
            config.getManifestFilePath(stdPath); // triggers ensureManifestId → adopts
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

            // --- Main driving-trips scan ---
            emit statusUpdate("Scanning driving footage...");
            TripDetection td;
            auto trips = td.detectTrips(
                stdPath, profile,
                config.getGapThreshold(),
                config.getFuzzyWindow(),
                config.getFfprobePath());
            config.saveTripCache(stdPath, trips, profile);

            // --- Parking scan (motion-triggered + G-sensor / RO) ---
            // Both folder types produce trips with footageType = "parking".
            // Pre-count primary files so progress is accurate before detectTrips starts.
            CameraProfile pkMotion = profile.parkingProfile();
            CameraProfile pkRo     = profile.roProfile();

            auto countPrimary = [&](const CameraProfile& pk) -> int {
                if (!pk.isValid()) return 0;
                std::string scanDir;
                for (const auto& slot : pk.cameraSlots)
                    if (slot.isPrimary) { scanDir = stdPath + "/" + slot.scanSubdir; break; }
                std::error_code ec;
                if (!fs::exists(scanDir, ec)) return 0;
                std::regex pat(pk.filenameRegex);
                int n = 0;
                for (const auto& de : fs::directory_iterator(scanDir, ec)) {
                    if (de.is_directory()) continue;
                    std::smatch m;
                    std::string fname = de.path().filename().string();
                    if (!std::regex_search(fname, m, pat)) continue;
                    int tkGrp = pk.tokenCaptureGroup;
                    if ((int)m.size() <= tkGrp) continue;
                    std::string tok = m[tkGrp].str();
                    for (const auto& slot : pk.cameraSlots)
                        if (slot.isPrimary && tok == slot.filenameToken) { ++n; break; }
                }
                return n;
            };

            int motionCount = countPrimary(pkMotion);
            int roCount     = countPrimary(pkRo);
            int parkTotal   = motionCount + roCount;

            std::vector<Trip> parkingTrips;
            int parkOffset = 0;

            auto runParkingScan = [&](const CameraProfile& pkProfile,
                                      const std::string&   triggerType,
                                      bool                 readOnly,
                                      int                  thisScanTotal) {
                if (!pkProfile.isValid() || thisScanTotal == 0) return;
                std::string primarySlot = pkProfile.primarySlot();
                if (primarySlot.empty()) return;
                std::string scanDir;
                for (const auto& slot : pkProfile.cameraSlots)
                    if (slot.isPrimary) { scanDir = stdPath + "/" + slot.scanSubdir; break; }
                std::error_code ec;
                if (!fs::exists(scanDir, ec)) return;

                // Skip ffprobe and exiftool for parking — fixed-length clips,
                // parked-location GPS adds no value, and we'd otherwise spawn
                // thousands of subprocesses for large parking datasets.
                CameraProfile noProbe = pkProfile;
                noProbe.gpsMethod = "none";
                int offset = parkOffset;
                emit statusUpdate(QString("Parking events — %1 / %2")
                                  .arg(offset).arg(parkTotal));
                auto pTrips = td.detectTrips(
                    stdPath, noProbe,
                    1,
                    config.getFuzzyWindow(),
                    "",   // skip ffprobe
                    "",   // skip exiftool
                    [this, offset, parkTotal](int done, int) {
                        emit statusUpdate(QString("Parking events — %1 / %2")
                                          .arg(offset + done).arg(parkTotal));
                    });
                parkOffset += thisScanTotal;

                for (auto& t : pTrips) {
                    t.footageType = "parking";
                    t.triggerType = triggerType;
                    t.readOnly    = readOnly;
                }
                parkingTrips.insert(parkingTrips.end(), pTrips.begin(), pTrips.end());
            };

            runParkingScan(pkMotion, "motion",   false, motionCount);
            runParkingScan(pkRo,     "g_sensor", true,  roCount);

            if (!parkingTrips.empty()) {
                std::sort(parkingTrips.begin(), parkingTrips.end(),
                    [](const Trip& a, const Trip& b) { return a.startEpoch < b.startEpoch; });
                std::string parkKey = "park:" + stdPath;
                CameraProfile parkProfile = pkMotion.isValid() ? pkMotion : pkRo;
                config.saveTripCache(parkKey, parkingTrips, parkProfile);
            }

            emit finished(true, "");
        } catch (const std::exception& ex) {
            emit finished(false, QString::fromLatin1(ex.what()));
        } catch (...) {
            emit finished(false, "Unknown error during scan");
        }
    }
signals:
    void finished(bool ok, const QString& error);
    void statusUpdate(const QString& msg);
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
    connect(worker, &ScanWorker::statusUpdate,
            m_statusLabel, &QLabel::setText);
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
// SN: 00122
