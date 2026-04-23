// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "TripPropertiesDialog.h"
#include "MapProgressDialog.h"
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidget>
#include <QMessageBox>
#include <QDesktopServices>
#include <QMutex>
#include <QThread>
#include <QUrl>
#include <QString>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include "config_manager.hpp"
#include "gps_export.hpp"
#include "json.hpp"

using namespace Pathmux;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Per-manifest mutex registry — ensures only one extraction at a time per
// manifest file.  Workers on different manifests run concurrently.
// s_registryMutex protects the map itself; each entry is a per-file mutex.
// ---------------------------------------------------------------------------
static QMutex s_registryMutex;
static std::map<std::string, std::shared_ptr<QMutex>> s_manifestLocks;

static std::shared_ptr<QMutex> manifestLockFor(const std::string& path)
{
    QMutexLocker reg(&s_registryMutex);
    auto& ptr = s_manifestLocks[path];
    if (!ptr) ptr = std::make_shared<QMutex>();
    return ptr;
}

// ---------------------------------------------------------------------------
// GpsExtractWorker — runs extractGps() on a background QThread.
// Q_OBJECT in a .cpp requires the moc include at EOF.
// ---------------------------------------------------------------------------
class GpsExtractWorker : public QObject {
    Q_OBJECT
public:
    GpsExtractWorker(const std::string& tripId,
                     const std::string& manifestFile,
                     const std::string& exiftoolPath,
                     const std::string& exiftoolOptions)
        : m_tripId(tripId), m_manifestFile(manifestFile),
          m_exiftoolPath(exiftoolPath), m_exiftoolOptions(exiftoolOptions) {}

public slots:
    void start() {
        // Acquire the per-manifest lock.  Blocks if another extraction is
        // already running against the same manifest file.
        auto lock = manifestLockFor(m_manifestFile);
        QMutexLocker manifestLocker(lock.get());

        std::ifstream ifs(m_manifestFile);
        if (!ifs.is_open()) {
            emit finished(false,
                QString("Cannot open manifest: %1")
                    .arg(QString::fromStdString(m_manifestFile)));
            return;
        }
        json root;
        try { ifs >> root; } catch (...) {
            emit finished(false, "Manifest JSON parse error");
            return;
        }
        ifs.close();

        if (!root.contains("trips") || !root["trips"].is_array()) {
            emit finished(false, "No trips array in manifest");
            return;
        }
        int tripIdx = -1;
        for (int i = 0; i < (int)root["trips"].size(); ++i) {
            if (root["trips"][i].value("id", "") == m_tripId) {
                tripIdx = i;
                break;
            }
        }
        if (tripIdx < 0) {
            emit finished(false,
                QString("Trip %1 not found in manifest")
                    .arg(QString::fromStdString(m_tripId)));
            return;
        }

        bool ok = Pathmux::extractGps(root, tripIdx, m_manifestFile,
                                      m_exiftoolPath, m_exiftoolOptions,
                                      /*verbose=*/false,
                                      [this](int done, int total) {
                                          emit progress(done, total);
                                      });
        emit finished(ok,
            ok ? "" : "GPS extraction failed — verify exiftool is installed "
                      "and the camera profile is compatible");
    }

signals:
    void progress(int done, int total);
    void finished(bool ok, const QString& error);

private:
    std::string m_tripId;
    std::string m_manifestFile;
    std::string m_exiftoolPath;
    std::string m_exiftoolOptions;
};

// ---------------------------------------------------------------------------
// Humanize a byte count: B / KB / MB / GB
// ---------------------------------------------------------------------------
static QString humanizeSize(qint64 bytes)
{
    if (bytes < 0)               return "?";
    if (bytes < 1024)            return QString("%1 B").arg(bytes);
    if (bytes < 1024LL * 1024)   return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
                                 return QString("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 1);
    return                              QString("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
}

// ---------------------------------------------------------------------------
// Build a read-only value label for the form layout
// ---------------------------------------------------------------------------
static QLabel* valueLabel(const QString& text, QWidget* parent)
{
    auto* lbl = new QLabel(text, parent);
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return lbl;
}

// ---------------------------------------------------------------------------
// General tab — first Front segment basename
// ---------------------------------------------------------------------------
static QString firstFrontBasename(const Trip& t)
{
    for (const auto& seg : t.segments) {
        auto it = seg.cameras.find("front");
        if (it != seg.cameras.end() && !it->second.empty() && it->second != "-")
            return QFileInfo(QString::fromStdString(it->second)).fileName();
    }
    return QString();
}

// ---------------------------------------------------------------------------
// Per-camera tab: table of segments (path | size)
// ---------------------------------------------------------------------------
static QWidget* makeCameraTab(const Trip& t, const std::string& slot, QWidget* parent)
{
    auto* w     = new QWidget(parent);
    auto* vbox  = new QVBoxLayout(w);
    vbox->setContentsMargins(0, 0, 0, 0);

    auto* table = new QTableWidget(0, 2, w);
    table->setHorizontalHeaderLabels({"Path", "Size"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table->setColumnWidth(1, 80);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);

    for (const auto& seg : t.segments) {
        auto it = seg.cameras.find(slot);
        QString path = (it != seg.cameras.end() && !it->second.empty() && it->second != "-")
                       ? QString::fromStdString(it->second)
                       : QString();

        int row = table->rowCount();
        table->insertRow(row);

        if (path.isEmpty()) {
            auto* pathItem = new QTableWidgetItem("\u2014");
            pathItem->setForeground(Qt::gray);
            auto* sizeItem = new QTableWidgetItem("\u2014");
            sizeItem->setForeground(Qt::gray);
            sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(row, 0, pathItem);
            table->setItem(row, 1, sizeItem);
        } else {
            QFileInfo fi(path);
            QString sizeStr = fi.exists() ? humanizeSize(fi.size()) : "?";
            auto* pathItem = new QTableWidgetItem(path);
            auto* sizeItem = new QTableWidgetItem(sizeStr);
            sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(row, 0, pathItem);
            table->setItem(row, 1, sizeItem);
        }
    }

    QObject::connect(table, &QTableWidget::itemDoubleClicked,
                     [table](QTableWidgetItem* item) {
        if (!item) return;
        QString path = table->item(item->row(), 0)->text();
        if (path == "\u2014") return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    vbox->addWidget(table);
    return w;
}

// ---------------------------------------------------------------------------
// TripPropertiesDialog
// ---------------------------------------------------------------------------
TripPropertiesDialog::TripPropertiesDialog(const Trip& trip,
                                           const std::string& sourcePath,
                                           QWidget* parent)
    : QDialog(parent), m_trip(trip), m_sourcePath(sourcePath)
{
    setWindowTitle(QString("Trip Properties \u2014 %1  %2")
        .arg(QString::fromStdString(trip.date))
        .arg(QString::fromStdString(trip.startTime)));
    resize(640, 500);

    auto* vbox = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);

    bool hasGps = (trip.gpsTrackStatus == "complete");

    // -----------------------------------------------------------------------
    // General tab
    // -----------------------------------------------------------------------
    {
        auto* w    = new QWidget(tabs);
        auto* form = new QFormLayout(w);
        form->setContentsMargins(12, 12, 12, 12);
        form->setVerticalSpacing(6);
        form->setHorizontalSpacing(16);

        m_noteEdit = new QLineEdit(w);
        m_noteEdit->setText(QString::fromStdString(trip.note));
        m_noteEdit->setPlaceholderText("No note set");
        form->addRow("Note:", m_noteEdit);

        QString basename = firstFrontBasename(trip);
        if (!basename.isEmpty())
            form->addRow("Basename:", valueLabel(basename, w));

        auto* sep = new QFrame(w);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        form->addRow(sep);

        form->addRow("Trip ID:",   valueLabel(QString::fromStdString(trip.id), w));
        form->addRow("Date:",      valueLabel(QString::fromStdString(trip.date), w));
        form->addRow("Start:",     valueLabel(QString::fromStdString(trip.startTime), w));
        form->addRow("Duration:",  valueLabel(QString::fromStdString(trip.duration), w));
        form->addRow("Segments:",  valueLabel(QString::number(trip.segments.size()), w));

        QString gpsText = QString::fromStdString(trip.gpsTrackStatus);
        if (gpsText.isEmpty()) gpsText = "none";
        form->addRow("GPS Status:", valueLabel(gpsText, w));

        QString lockText;
        if (trip.gpsLockSeconds < 0)       lockText = "Not scanned";
        else if (trip.gpsLockSeconds == 0) lockText = "Immediate";
        else                               lockText = QString("%1 s").arg(trip.gpsLockSeconds);
        form->addRow("GPS Lock:", valueLabel(lockText, w));

        auto fmtCoord = [](double lat, double lon) -> QString {
            if (lat == 0.0 && lon == 0.0) return "\u2014";
            return QString("%1\u00b0, %2\u00b0").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6);
        };
        form->addRow("Start Coords:", valueLabel(fmtCoord(trip.startLat, trip.startLon), w));
        form->addRow("End Coords:",   valueLabel(fmtCoord(trip.endLat,   trip.endLon),   w));

        QString vp = QString("%1\u00d7%2  %3  %4 fps")
            .arg(trip.videoProfile.width)
            .arg(trip.videoProfile.height)
            .arg(QString::fromStdString(trip.videoProfile.pixFmt))
            .arg(QString::fromStdString(trip.videoProfile.frameRate));
        form->addRow("Video:", valueLabel(vp, w));

        tabs->addTab(w, "General");
    }

    // -----------------------------------------------------------------------
    // GPS tab
    // -----------------------------------------------------------------------
    {
        auto* w    = new QWidget(tabs);
        auto* vlay = new QVBoxLayout(w);
        vlay->setContentsMargins(12, 12, 12, 12);
        vlay->setSpacing(10);

        // --- Extraction group ---
        auto* extractGrp  = new QGroupBox("Extract GPS Track to Manifest", w);
        auto* extractVlay = new QVBoxLayout(extractGrp);
        extractVlay->setSpacing(6);

        m_gpsStatusLabel = new QLabel(extractGrp);
        m_gpsStatusLabel->setText(hasGps ? "Status: complete" : "Status: not extracted");
        m_gpsStatusLabel->setStyleSheet(hasGps
            ? "color: green; font-weight: bold;"
            : "color: #cc6600;");
        extractVlay->addWidget(m_gpsStatusLabel);

        m_extractGpsBtn = new QPushButton("Extract GPS Track", extractGrp);
        connect(m_extractGpsBtn, &QPushButton::clicked,
                this, &TripPropertiesDialog::onExtractGps);
        extractVlay->addWidget(m_extractGpsBtn);

        m_extractBar = new QProgressBar(extractGrp);
        m_extractBar->setRange(0, 0);   // indeterminate until first progress signal
        m_extractBar->hide();
        extractVlay->addWidget(m_extractBar);

        m_extractMsgLabel = new QLabel(extractGrp);
        m_extractMsgLabel->setStyleSheet("font-size: 8pt; color: gray;");
        m_extractMsgLabel->setWordWrap(true);
        extractVlay->addWidget(m_extractMsgLabel);

        vlay->addWidget(extractGrp);

        // --- Export group ---
        auto* exportGrp  = new QGroupBox("Export Track", w);
        auto* exportForm = new QFormLayout(exportGrp);
        exportForm->setContentsMargins(8, 8, 8, 8);
        exportForm->setVerticalSpacing(6);

        m_exportFormatCombo = new QComboBox(exportGrp);
        m_exportFormatCombo->addItems({"GPX", "KML", "GeoJSON"});
        exportForm->addRow("Format:", m_exportFormatCombo);

        // Default export path: next to the footage source
        QString sourceDir = QString::fromStdString(m_sourcePath);
        QString tripId    = QString::fromStdString(trip.id);
        m_exportPathEdit  = new QLineEdit(exportGrp);
        m_exportPathEdit->setText(sourceDir + "/pm_trip_" + tripId + "_track.gpx");

        auto* browseBtn = new QPushButton("\u2026", exportGrp);
        browseBtn->setFixedWidth(28);
        connect(browseBtn, &QPushButton::clicked,
                this, &TripPropertiesDialog::onExportBrowse);

        auto* pathRow = new QHBoxLayout;
        pathRow->addWidget(m_exportPathEdit);
        pathRow->addWidget(browseBtn);
        exportForm->addRow("Output:", pathRow);

        m_exportGpsBtn = new QPushButton("Export", exportGrp);
        m_exportGpsBtn->setEnabled(hasGps);
        exportForm->addRow("", m_exportGpsBtn);

        connect(m_exportFormatCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TripPropertiesDialog::onExportFormatChanged);
        connect(m_exportGpsBtn, &QPushButton::clicked,
                this, &TripPropertiesDialog::onExportGps);

        vlay->addWidget(exportGrp);
        vlay->addStretch();

        tabs->addTab(w, "GPS");
    }

    // -----------------------------------------------------------------------
    // Map tab
    // -----------------------------------------------------------------------
    {
        auto* w    = new QWidget(tabs);
        auto* vlay = new QVBoxLayout(w);
        vlay->setContentsMargins(12, 12, 12, 12);
        vlay->setSpacing(8);

        m_mapWarnLabel = new QLabel(
            "GPS track not yet extracted for this trip.\n"
            "Use the GPS tab to extract the track first.", w);
        m_mapWarnLabel->setWordWrap(true);
        m_mapWarnLabel->setStyleSheet("color: #cc6600;");
        m_mapWarnLabel->setVisible(!hasGps);
        vlay->addWidget(m_mapWarnLabel);

        auto* outRow  = new QHBoxLayout;
        auto* outLbl  = new QLabel("Output:", w);
        outLbl->setFixedWidth(54);
        m_mapOutputEdit = new QLineEdit(w);
        QString sourceDir = QString::fromStdString(m_sourcePath);
        m_mapOutputEdit->setText(
            sourceDir + "/pm_trip_" +
            QString::fromStdString(trip.id) + "_map.mp4");
        auto* mapBrowseBtn = new QPushButton("\u2026", w);
        mapBrowseBtn->setFixedWidth(28);
        connect(mapBrowseBtn, &QPushButton::clicked, [this]() {
            QString path = QFileDialog::getSaveFileName(
                this, "Map Output", m_mapOutputEdit->text(),
                "Video (*.mp4)");
            if (!path.isEmpty())
                m_mapOutputEdit->setText(path);
        });
        outRow->addWidget(outLbl);
        outRow->addWidget(m_mapOutputEdit);
        outRow->addWidget(mapBrowseBtn);
        vlay->addLayout(outRow);

        auto* resLbl = new QLabel(
            QString("Resolution: %1\u00d7%2  (trip video profile)")
                .arg(trip.videoProfile.width)
                .arg(trip.videoProfile.height), w);
        resLbl->setStyleSheet("font-size: 8pt; color: gray;");
        vlay->addWidget(resLbl);

        auto* mp4Label = new QLabel("Generated map files (double-click to open):", w);
        mp4Label->setStyleSheet("font-size: 8pt; color: gray;");
        vlay->addWidget(mp4Label);

        m_mapFileList = new QListWidget(w);
        m_mapFileList->setMaximumHeight(100);
        m_mapFileList->setAlternatingRowColors(true);
        connect(m_mapFileList, &QListWidget::itemDoubleClicked,
                [](QListWidgetItem* item) {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(item->data(Qt::UserRole).toString()));
        });
        vlay->addWidget(m_mapFileList);

        vlay->addStretch();

        m_mapGenerateBtn = new QPushButton("Generate Map\u2026", w);
        m_mapGenerateBtn->setEnabled(hasGps);
        connect(m_mapGenerateBtn, &QPushButton::clicked,
                this, &TripPropertiesDialog::onGenerateMap);
        vlay->addWidget(m_mapGenerateBtn);

        tabs->addTab(w, "Map");
    }

    // -----------------------------------------------------------------------
    // Dashboard tab
    // -----------------------------------------------------------------------
    {
        auto* w    = new QWidget(tabs);
        auto* vlay = new QVBoxLayout(w);
        vlay->setContentsMargins(12, 12, 12, 12);
        vlay->setSpacing(8);

        m_dashWarnLabel = new QLabel(
            "GPS track not yet extracted for this trip.\n"
            "Use the GPS tab to extract the track first.", w);
        m_dashWarnLabel->setWordWrap(true);
        m_dashWarnLabel->setStyleSheet("color: #cc6600;");
        m_dashWarnLabel->setVisible(!hasGps);
        vlay->addWidget(m_dashWarnLabel);

        auto* outRow    = new QHBoxLayout;
        auto* outLbl    = new QLabel("Output:", w);
        outLbl->setFixedWidth(54);
        m_dashOutputEdit = new QLineEdit(w);
        QString sourceDir = QString::fromStdString(m_sourcePath);
        m_dashOutputEdit->setText(
            sourceDir + "/pm_trip_" +
            QString::fromStdString(trip.id) + "_dash.mp4");
        auto* dashBrowseBtn = new QPushButton("\u2026", w);
        dashBrowseBtn->setFixedWidth(28);
        connect(dashBrowseBtn, &QPushButton::clicked, [this]() {
            QString path = QFileDialog::getSaveFileName(
                this, "Dashboard Output", m_dashOutputEdit->text(),
                "Video (*.mp4)");
            if (!path.isEmpty())
                m_dashOutputEdit->setText(path);
        });
        outRow->addWidget(outLbl);
        outRow->addWidget(m_dashOutputEdit);
        outRow->addWidget(dashBrowseBtn);
        vlay->addLayout(outRow);

        auto* infoLbl = new QLabel(
            "Resolution: 960\u00d7540  (collage quadrant slot)", w);
        infoLbl->setStyleSheet("font-size: 8pt; color: gray;");
        vlay->addWidget(infoLbl);

        auto* mp4Label = new QLabel("Generated dashboard files (double-click to open):", w);
        mp4Label->setStyleSheet("font-size: 8pt; color: gray;");
        vlay->addWidget(mp4Label);

        m_dashFileList = new QListWidget(w);
        m_dashFileList->setMaximumHeight(100);
        m_dashFileList->setAlternatingRowColors(true);
        connect(m_dashFileList, &QListWidget::itemDoubleClicked,
                [](QListWidgetItem* item) {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(item->data(Qt::UserRole).toString()));
        });
        vlay->addWidget(m_dashFileList);

        vlay->addStretch();

        m_dashGenerateBtn = new QPushButton("Generate Dashboard\u2026", w);
        connect(m_dashGenerateBtn, &QPushButton::clicked,
                this, &TripPropertiesDialog::onGenerateDashboard);
        vlay->addWidget(m_dashGenerateBtn);

        tabs->addTab(w, "Dashboard");
    }

    // Populate video lists from manifest data.
    populateVideoList(m_mapFileList,  m_trip.mapVideos);
    populateVideoList(m_dashFileList, m_trip.dashVideos);

    // -----------------------------------------------------------------------
    // Per-camera tabs
    // -----------------------------------------------------------------------
    // Note: "slots" is a Qt macro — use cameraSlots instead.
    std::vector<std::string> cameraSlots;
    for (const auto& seg : trip.segments)
        for (const auto& kv : seg.cameras) {
            const std::string& k = kv.first;
            if (std::find(cameraSlots.begin(), cameraSlots.end(), k) == cameraSlots.end())
                cameraSlots.push_back(k);
        }
    std::sort(cameraSlots.begin(), cameraSlots.end());

    for (const auto& camSlot : cameraSlots) {
        bool hasAny = false;
        for (const auto& seg : trip.segments) {
            auto it = seg.cameras.find(camSlot);
            if (it != seg.cameras.end() && !it->second.empty() && it->second != "-") {
                hasAny = true; break;
            }
        }
        if (!hasAny) continue;
        QString tabName = QString::fromStdString(camSlot);
        tabName[0] = tabName[0].toUpper();
        tabs->addTab(makeCameraTab(trip, camSlot, tabs), tabName);
    }

    vbox->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &TripPropertiesDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    vbox->addWidget(buttons);
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
std::string TripPropertiesDialog::updatedNote() const
{
    return m_noteEdit ? m_noteEdit->text().trimmed().toStdString() : m_trip.note;
}

// ---------------------------------------------------------------------------
// GPS — extract
// ---------------------------------------------------------------------------
void TripPropertiesDialog::onExtractGps()
{
    ConfigManager config;
    config.loadSettings();
    std::string manifestFile = config.lookupManifestFilePath(m_sourcePath);
    if (manifestFile.empty()) {
        m_extractMsgLabel->setText(
            "Error: manifest file not found. Try rescanning this source directory.");
        return;
    }

    m_extractGpsBtn->setEnabled(false);
    m_exportGpsBtn->setEnabled(false);
    m_extractBar->setRange(0, 0);   // indeterminate until first callback
    m_extractBar->setValue(0);
    m_extractBar->show();
    m_extractMsgLabel->setText("Running exiftool on segments\u2026");

    auto* worker = new GpsExtractWorker(
        m_trip.id, manifestFile,
        config.getExiftoolPath(),
        config.getExiftoolOptions());
    auto* thread = new QThread(this);
    worker->moveToThread(thread);

    connect(thread, &QThread::started,    worker, &GpsExtractWorker::start);
    connect(worker, &GpsExtractWorker::progress,
            this,   &TripPropertiesDialog::onExtractGpsProgress);
    connect(worker, &GpsExtractWorker::finished,
            this,   &TripPropertiesDialog::onExtractGpsFinished);
    connect(worker, &GpsExtractWorker::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished,   worker, &QObject::deleteLater);

    thread->start();
}

void TripPropertiesDialog::onExtractGpsProgress(int done, int total)
{
    if (m_extractBar->maximum() == 0)
        m_extractBar->setRange(0, total);   // switch from indeterminate on first callback
    m_extractBar->setValue(done);
    m_extractMsgLabel->setText(
        QString("Segment %1 of %2").arg(done).arg(total));
}

void TripPropertiesDialog::onExtractGpsFinished(bool ok, const QString& error)
{
    m_extractBar->hide();
    m_extractGpsBtn->setEnabled(true);

    if (ok) {
        m_trip.gpsTrackStatus = "complete";
        m_gpsStatusLabel->setText("Status: complete");
        m_gpsStatusLabel->setStyleSheet("color: green; font-weight: bold;");
        m_extractMsgLabel->setText("Extraction complete.");
        m_exportGpsBtn->setEnabled(true);
        m_mapGenerateBtn->setEnabled(true);
        if (m_mapWarnLabel) m_mapWarnLabel->hide();
        emit gpsExtracted();
    } else {
        m_extractMsgLabel->setText(error.isEmpty() ? "Extraction failed." : error);
        m_extractMsgLabel->setStyleSheet("font-size: 8pt; color: red;");
    }
}

// ---------------------------------------------------------------------------
// GPS — export format / browse / export
// ---------------------------------------------------------------------------
void TripPropertiesDialog::onExportFormatChanged(int idx)
{
    if (!m_exportPathEdit) return;
    static const char* exts[] = {".gpx", ".kml", ".geojson"};
    static const char* oldExts[] = {".gpx", ".kml", ".geojson"};
    QString path = m_exportPathEdit->text();
    for (const char* ext : oldExts) {
        if (path.endsWith(ext, Qt::CaseInsensitive)) {
            path.chop(static_cast<int>(strlen(ext)));
            break;
        }
    }
    if (idx >= 0 && idx < 3)
        path += exts[idx];
    m_exportPathEdit->setText(path);
    // Reset export button label in case it showed "Exported"
    if (m_exportGpsBtn) m_exportGpsBtn->setText("Export");
}

void TripPropertiesDialog::onExportBrowse()
{
    if (!m_exportPathEdit || !m_exportFormatCombo) return;
    static const char* filters[] = {
        "GPX Files (*.gpx)",
        "KML Files (*.kml)",
        "GeoJSON Files (*.geojson)"
    };
    int idx = m_exportFormatCombo->currentIndex();
    QString filter = (idx >= 0 && idx < 3) ? filters[idx] : "All Files (*)";
    QString path = QFileDialog::getSaveFileName(
        this, "Export GPS Track",
        m_exportPathEdit->text(),
        QString(filter) + ";;All Files (*)");
    if (!path.isEmpty())
        m_exportPathEdit->setText(path);
}

void TripPropertiesDialog::onExportGps()
{
    if (!m_exportPathEdit || !m_exportFormatCombo) return;
    std::string outPath = m_exportPathEdit->text().trimmed().toStdString();
    if (outPath.empty()) return;

    ConfigManager config;
    config.loadSettings();
    std::string manifestFile = config.lookupManifestFilePath(m_sourcePath);
    if (manifestFile.empty()) {
        m_extractMsgLabel->setText("Error: manifest file not found.");
        return;
    }

    std::ifstream ifs(manifestFile);
    if (!ifs.is_open()) return;
    json root;
    try { ifs >> root; } catch (...) { return; }
    ifs.close();

    int tripIdx = -1;
    if (root.contains("trips") && root["trips"].is_array()) {
        for (int i = 0; i < (int)root["trips"].size(); ++i) {
            if (root["trips"][i].value("id", "") == m_trip.id) {
                tripIdx = i; break;
            }
        }
    }
    if (tripIdx < 0) return;

    int fmt = m_exportFormatCombo->currentIndex();
    std::string result;
    if      (fmt == 0) result = Pathmux::writeGpx    (root, tripIdx, outPath);
    else if (fmt == 1) result = Pathmux::writeKml    (root, tripIdx, outPath);
    else               result = Pathmux::writeGeoJson(root, tripIdx, outPath);

    if (result.empty()) {
        m_exportGpsBtn->setText("Export \u2717");
        m_exportGpsBtn->setStyleSheet("color: red;");
    } else {
        m_exportGpsBtn->setText("Exported \u2713");
        m_exportGpsBtn->setStyleSheet("color: green;");
    }
}

// ---------------------------------------------------------------------------
// Map
// ---------------------------------------------------------------------------
void TripPropertiesDialog::onGenerateMap()
{
    QString output = m_mapOutputEdit ? m_mapOutputEdit->text().trimmed() : QString();
    if (output.isEmpty()) return;

    if (QFileInfo::exists(output)) {
        auto btn = QMessageBox::question(
            this, "Overwrite existing file?",
            QString("The file already exists:\n%1\n\nOverwrite it?").arg(output),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (btn != QMessageBox::Yes) return;
    }

    ConfigManager config;
    config.loadSettings();
    std::string manifestFile = config.lookupManifestFilePath(m_sourcePath);
    if (manifestFile.empty()) {
        if (m_mapWarnLabel) {
            m_mapWarnLabel->setText("Error: manifest file not found. Try rescanning.");
            m_mapWarnLabel->setVisible(true);
        }
        return;
    }

    auto* dlg = new MapProgressDialog(
        m_trip,
        QString::fromStdString(manifestFile),
        output,
        m_trip.videoProfile.width,
        m_trip.videoProfile.height,
        this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &MapProgressDialog::renderComplete,
            this, [this, output](bool ok) {
        if (ok) appendVideoToManifest(output, "mapVideos", m_trip.mapVideos);
    });
    dlg->show();
    dlg->startRender();
}

// ---------------------------------------------------------------------------
// Dashboard
// ---------------------------------------------------------------------------
void TripPropertiesDialog::onGenerateDashboard()
{
    if (m_trip.gpsTrackStatus != "complete") {
        QMessageBox::information(
            this,
            "GPS Track Required",
            "This trip does not have a GPS track in the manifest yet.\n\n"
            "Go to the GPS tab, click \u201cExtract GPS Track\u201d, then return\n"
            "to the Dashboard tab and generate the dashboard.");
        return;
    }

    QString output = m_dashOutputEdit ? m_dashOutputEdit->text().trimmed() : QString();
    if (output.isEmpty()) return;

    if (QFileInfo::exists(output)) {
        auto btn = QMessageBox::question(
            this, "Overwrite existing file?",
            QString("The file already exists:\n%1\n\nOverwrite it?").arg(output),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (btn != QMessageBox::Yes) return;
    }

    ConfigManager config;
    config.loadSettings();
    std::string manifestFile = config.lookupManifestFilePath(m_sourcePath);
    if (manifestFile.empty()) {
        if (m_dashWarnLabel) {
            m_dashWarnLabel->setText("Error: manifest file not found. Try rescanning.");
            m_dashWarnLabel->setVisible(true);
        }
        return;
    }

    QString units = config.getUseImperial() ? "imperial" : "metric";
    auto* dlg = new MapProgressDialog(
        m_trip,
        QString::fromStdString(manifestFile),
        output,
        960, 540,
        this,
        "pm_dashboard.py",
        "Generating Dashboard",
        QStringList{"--units", units});
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &MapProgressDialog::renderComplete,
            this, [this, output](bool ok) {
        if (ok) appendVideoToManifest(output, "dashVideos", m_trip.dashVideos);
    });
    dlg->show();
    dlg->startRender();
}

// ---------------------------------------------------------------------------
// Accept — save note
// ---------------------------------------------------------------------------
void TripPropertiesDialog::onAccepted()
{
    std::string newNote = updatedNote();
    if (newNote != m_trip.note && !m_sourcePath.empty()) {
        Pathmux::ConfigManager config;
        config.loadSettings();
        auto trips = config.loadTripCache(m_sourcePath);
        for (auto& t : trips) {
            if (t.id == m_trip.id) { t.note = newNote; break; }
        }
        config.saveTripCache(m_sourcePath, trips);
    }
    accept();
}

// ---------------------------------------------------------------------------
// populateVideoList — fill a list widget from a manifest-backed path vector.
// Missing files are shown in gray with a "(missing)" tag.
// Items store the absolute path as Qt::UserRole for double-click open.
// ---------------------------------------------------------------------------
void TripPropertiesDialog::populateVideoList(QListWidget* list,
                                             const std::vector<std::string>& paths)
{
    if (!list) return;
    list->clear();

    for (const auto& p : paths) {
        QFileInfo fi(QString::fromStdString(p));
        QString label;
        bool exists = fi.exists();
        if (exists)
            label = QString("%1  (%2)").arg(fi.fileName()).arg(humanizeSize(fi.size()));
        else
            label = QString("%1  (missing)").arg(fi.fileName());
        auto* item = new QListWidgetItem(label, list);
        item->setData(Qt::UserRole, fi.absoluteFilePath());
        if (!exists)
            item->setForeground(Qt::gray);
    }

    if (list->count() == 0) {
        auto* item = new QListWidgetItem("(none built yet)", list);
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
}

// ---------------------------------------------------------------------------
// appendVideoToManifest — add a new path to mapVideos/dashVideos in the
// manifest JSON and in the in-memory trip struct, then refresh the list.
// ---------------------------------------------------------------------------
void TripPropertiesDialog::appendVideoToManifest(const QString& path,
                                                  const std::string& key,
                                                  std::vector<std::string>& tripVec)
{
    std::string p = path.trimmed().toStdString();
    if (p.empty()) return;

    // Avoid duplicates.
    if (std::find(tripVec.begin(), tripVec.end(), p) != tripVec.end()) return;

    ConfigManager config;
    config.loadSettings();
    std::string manifestFile = config.lookupManifestFilePath(m_sourcePath);
    if (manifestFile.empty()) return;

    std::ifstream ifs(manifestFile);
    if (!ifs.is_open()) return;
    json root;
    try { ifs >> root; } catch (...) { return; }
    ifs.close();

    if (!root.contains("trips") || !root["trips"].is_array()) return;
    for (auto& jt : root["trips"]) {
        if (jt.value("id", "") != m_trip.id) continue;
        auto& arr = jt[key];
        if (!arr.is_array()) arr = json::array();
        // Avoid duplicates in JSON too.
        bool found = false;
        for (const auto& v : arr) if (v.get<std::string>() == p) { found = true; break; }
        if (!found) arr.push_back(p);
        break;
    }

    std::ofstream ofs(manifestFile);
    if (!ofs.is_open()) return;
    ofs << root.dump(2);
    ofs.close();

    // Update in-memory trip and refresh the appropriate list.
    tripVec.push_back(p);
    if (key == "mapVideos")
        populateVideoList(m_mapFileList,  m_trip.mapVideos);
    else
        populateVideoList(m_dashFileList, m_trip.dashVideos);
}

#include "TripPropertiesDialog.moc"
// SN: 00101
