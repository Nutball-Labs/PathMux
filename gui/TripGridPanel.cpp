// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "TripGridPanel.h"
#include "TripTile.h"
#include "TripBuildDialog.h"
#include "JobQueue.h"
#include "EmptyManifestWidget.h"
#include <QStackedWidget>
#include <QScrollArea>
#include <QLabel>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QTimer>
#include <QProcess>
#include <QPointer>
#include <QRandomGenerator>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <fstream>
#include <algorithm>
#include "json.hpp"
using json = nlohmann::json;

using namespace CamClops;

// Pick the video path and in-segment offset for a sidecar-less thumbnail grab.
// For normal trips the target falls randomly in [90s, 300s] from trip start.
// parking=true uses [15s, 30s] for parking mode events (future use).
static std::pair<std::string, double>
pickThumbSource(const Trip& t, const std::string& slot, bool parking = false)
{
    if (t.segments.empty()) return {{}, 0.0};

    int totalS = t.durationFFProbed > 0 ? t.durationFFProbed : t.segDetectedDuration;
    if (totalS <= 0) totalS = 60;

    int lo, hi;
    if (parking) {
        lo = 15;
        hi = std::min(30, std::max(lo + 1, totalS - 2));
    } else {
        lo = std::min(90,  std::max(5,  totalS / 2 - 30));
        hi = std::min(300, std::max(lo + 1, totalS - 5));
    }
    double targetS = lo + QRandomGenerator::global()->bounded(std::max(1, hi - lo));

    // Walk segments using approximate uniform duration to find the right file.
    double approxSeg = double(totalS) / double(t.segments.size());
    if (approxSeg < 1.0) approxSeg = 60.0;
    int idx = std::clamp((int)(targetS / approxSeg), 0, (int)t.segments.size() - 1);

    double offsetInSeg = std::max(targetS - idx * approxSeg, 2.0);

    auto it = t.segments[idx].cameras.find(slot);
    if (it == t.segments[idx].cameras.end() || it->second.empty() || it->second == "-")
        return {{}, 0.0};
    return {it->second, offsetInSeg};
}

TripGridPanel::TripGridPanel(QWidget* parent)
    : QWidget(parent)
{
    ConfigManager cfg;
    m_ffmpegPath = cfg.getFfmpegPath();

    auto* vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);

    // Sticky manifest header — visible only when a manifest is loaded (PAGE_GRID).
    // Format: "MR -- Z:/ex01/ -- 19 trips"
    m_manifestHeader = new QLabel(this);
    m_manifestHeader->setAlignment(Qt::AlignCenter);
    m_manifestHeader->setStyleSheet(
        "QLabel {"
        "  background: #e8edf2;"
        "  border-bottom: 1px solid #b0bbc8;"
        "  padding: 4px 8px;"
        "  font-size: 8pt;"
        "  color: #2a3a4a;"
        "}");
    m_manifestHeader->hide();
    vlay->addWidget(m_manifestHeader);

    m_stack = new QStackedWidget(this);

    // Page 0: no manifests exist — camera+? prompt
    m_emptyWidget = new EmptyManifestWidget;
    connect(m_emptyWidget, &EmptyManifestWidget::scanRequested,
            this,          &TripGridPanel::scanRequested);

    // Page 1: manifests exist but none selected
    m_noSelLabel = new QLabel("Select a manifest on the left to view trip list.");
    m_noSelLabel->setAlignment(Qt::AlignCenter);
    m_noSelLabel->setEnabled(false);

    // Page 2: trip tile grid
    m_scrollArea = new QScrollArea;
    m_scrollArea->setWidgetResizable(false);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gridContainer = new QWidget;
    m_scrollArea->setWidget(m_gridContainer);

    m_stack->addWidget(m_emptyWidget);  // PAGE_EMPTY = 0
    m_stack->addWidget(m_noSelLabel);   // PAGE_NONE  = 1
    m_stack->addWidget(m_scrollArea);   // PAGE_GRID  = 2

    vlay->addWidget(m_stack);

    m_scrollArea->installEventFilter(this);
    m_scrollArea->viewport()->installEventFilter(this);

    // Make the scroll area and container transparent so the panel's watermark
    // logo shows through behind the tiles.  Tiles paint their own card backgrounds
    // so they remain fully opaque above the logo.
    m_scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    m_scrollArea->viewport()->setAutoFillBackground(false);
    m_gridContainer->setAutoFillBackground(false);
    m_stack->setAutoFillBackground(false);

    m_bgLogo = QPixmap(":/images/camclops_512.png");

    // Determine initial page
    ConfigManager config;
    config.loadSettings();
    m_imperial = config.getUseImperial();
    auto index = config.loadManifestIndex();
    m_stack->setCurrentIndex(index.empty() ? PAGE_EMPTY : PAGE_NONE);

    // Refresh tile indicators whenever any job finishes — GPS/map/dash/hud data
    // is written to the manifest by the job; tiles need a refreshFrom() so
    // indicator dots reflect the current state without requiring the user to
    // open TripPropertiesDialog first.
    connect(&JobQueue::instance(), &JobQueue::jobFinished,
            this, &TripGridPanel::onJobFinished);
}

void TripGridPanel::paintEvent(QPaintEvent* ev)
{
    QWidget::paintEvent(ev);
    if (m_bgLogo.isNull()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(0.25);
    // Scale to fit within the panel — whichever dimension fills first.
    QPixmap sc = m_bgLogo.scaled(width(), height(),
                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
    p.drawPixmap((width()  - sc.width())  / 2,
                 (height() - sc.height()) / 2, sc);
}

void TripGridPanel::refreshPageState()
{
    if (m_stack->currentIndex() == PAGE_GRID) return;
    ConfigManager config;
    config.loadSettings();
    auto index = config.loadManifestIndex();
    m_stack->setCurrentIndex(index.empty() ? PAGE_EMPTY : PAGE_NONE);
}

void TripGridPanel::loadManifest(const ManifestEntry& entry)
{
    m_currentManifest = entry;
    clearTiles();
    m_thumbQueue.clear();
    m_procQueue.clear();

    ConfigManager config;
    config.loadSettings();
    m_imperial = config.getUseImperial();
    auto trips = config.loadTripCache(entry.manifestFile);

    // Build sticky header using actual loaded count, not cached entry.tripCount,
    // so it stays accurate after trip deletions/archives.
    {
        QString id      = QString::fromStdString(entry.id).toUpper();
        bool    isPark  = (entry.manifestType == "park_events");
        QString rawPath = QString::fromStdString(entry.path);
        QString path    = isPark && rawPath.startsWith("park:") ? rawPath.mid(5) : rawPath;
        int     n       = (int)trips.size();
        QString word    = isPark ? "parking event" : "trip";
        m_manifestHeader->setText(
            QString("%1 \u2014\u2014 %2 \u2014\u2014 %3 %4%5")
                .arg(id).arg(path).arg(n).arg(word).arg(n == 1 ? "" : "s"));
        m_manifestHeader->show();
    }

    for (const auto& t : trips) {
        auto* tile = new TripTile(t, m_imperial, entry.path, entry.id, m_gridContainer);
        tile->show();
        connect(tile, &TripTile::doubleClicked,
                [this, t]() { emit tripDoubleClicked(m_currentManifest, t); });
        connect(tile, &TripTile::buildRequested,
                [this, t]() { onBuildRequested(t, m_currentManifest); });
        connect(tile, &TripTile::tripChanged,
                [this]() { loadManifest(m_currentManifest); });
        tile->setZoom(m_zoomFactor);
        m_tiles.push_back(tile);

        // Queue thumbnails — sidecar/extracted if present, otherwise async grab.
        // Parking events skip thumb extraction during scan, so firstThumbs may not
        // contain the slot key at all — fall through to video grab unconditionally.
        bool isPark = (t.footageType == "parking");
        for (const std::string slot : {"front", "rear", "interior"}) {
            auto it = t.firstThumbs.find(slot);
            if (it != t.firstThumbs.end() && !it->second.empty()) {
                enqueueThumb(tile,
                             QString::fromStdString(slot),
                             QString::fromStdString(it->second));
            } else {
                auto [vidPath, offset] = pickThumbSource(t, slot, isPark);
                if (!vidPath.empty())
                    grabVideoThumb(tile, QString::fromStdString(slot),
                                   QString::fromStdString(vidPath), offset, isPark,
                                   QString::fromStdString(entry.manifestFile),
                                   QString::fromStdString(entry.id),
                                   QString::fromStdString(t.id));
            }
        }
    }

    m_stack->setCurrentIndex(PAGE_GRID);
    QTimer::singleShot(0, this, &TripGridPanel::layoutTiles);
    if (!m_thumbQueue.empty())
        QTimer::singleShot(0, this, &TripGridPanel::loadNextThumbnail);
}

void TripGridPanel::clearTiles()
{
    for (auto* t : m_tiles) t->deleteLater();
    m_tiles.clear();
}

void TripGridPanel::layoutTiles()
{
    int tileW  = int(TripTile::W * m_zoomFactor);
    int tileH  = int(TripTile::H * m_zoomFactor);
    int availW = m_scrollArea->viewport()->width();
    int cols   = qMax(1, (availW + TILE_SPACING) / (tileW + TILE_SPACING));
    int n      = (int)m_tiles.size();

    for (int i = 0; i < n; ++i) {
        int row = i / cols;
        int col = i % cols;
        int x   = TILE_SPACING + col * (tileW + TILE_SPACING);
        int y   = TILE_SPACING + row * (tileH + TILE_SPACING);
        m_tiles[i]->setGeometry(x, y, tileW, tileH);
    }

    int rows       = n == 0 ? 0 : (n + cols - 1) / cols;
    int totalH     = rows * (tileH + TILE_SPACING) + TILE_SPACING;
    int containerW = cols * (tileW + TILE_SPACING) + TILE_SPACING;
    int containerH = qMax(totalH, m_scrollArea->viewport()->height());
    m_gridContainer->resize(containerW, containerH);
}

void TripGridPanel::grabVideoThumb(TripTile* tile, const QString& slot,
                                    const QString& videoPath, double offsetSecs,
                                    bool parking,
                                    const QString& manifestFile,
                                    const QString& manifestId,
                                    const QString& eventId)
{
    if (m_ffmpegPath.empty() || videoPath.isEmpty()) return;
    m_procQueue.push_back({QPointer<TripTile>(tile), slot, videoPath, offsetSecs, parking,
                           manifestFile, manifestId, eventId});
    drainProcQueue();
}

void TripGridPanel::drainProcQueue()
{
    while (m_activeProcs < MAX_THUMB_PROCS && !m_procQueue.empty()) {
        ProcRequest req = m_procQueue.front();
        m_procQueue.pop_front();
        startThumbProc(req);
    }
}

void TripGridPanel::startThumbProc(const ProcRequest& req)
{
    if (!req.tile) return;

    auto* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    const QString scale = QString("scale=%1:%2:force_original_aspect_ratio=decrease,"
                                  "pad=%1:%2:(ow-iw)/2:(oh-ih)/2")
                          .arg(TripTile::THUMB_W).arg(TripTile::THUMB_H);

    QStringList args;
    if (req.parking) {
        // Scene-detection pass: [10, 25]s window for the motion-trigger frame.
        // select=gt(scene,0.10) finds the first frame with significant scene change.
        args = { "-y", "-ss", "10", "-t", "15",
                 "-i", req.videoPath,
                 "-vf", QString("select=gt(scene\\,0.10),%1").arg(scale),
                 "-vsync", "vfr", "-frames:v", "1",
                 "-f", "image2pipe", "-vcodec", "mjpeg", "pipe:1" };
    } else {
        args = { "-y", "-ss", QString::number(req.offsetSecs, 'f', 1),
                 "-i", req.videoPath,
                 "-frames:v", "1",
                 "-vf", scale,
                 "-f", "image2pipe", "-vcodec", "mjpeg", "pipe:1" };
    }

    ++m_activeProcs;
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, req, proc](int code, QProcess::ExitStatus) {
        QByteArray data = proc->readAllStandardOutput();
        proc->deleteLater();
        --m_activeProcs;
        drainProcQueue();
        QPixmap px;
        bool valid = (code == 0 && px.loadFromData(data, "JPEG") && !px.isNull());
        if (valid) {
            // Save to disk regardless of whether the tile is still on screen —
            // the user may have switched manifests mid-grab; the result is still worth keeping.
            saveGrabbedThumb(data, req);
            if (req.tile) req.tile->setThumbnail(req.slot, px);
        } else if (req.parking && req.tile) {
            // Scene detection found nothing — fall back to fixed 17.5s offset.
            // Only retry if the tile is still visible; no point grabbing for a hidden manifest.
            ProcRequest fallback = req;
            fallback.offsetSecs = 17.5;
            fallback.parking    = false;
            m_procQueue.push_front(fallback);
            drainProcQueue();
        }
    });
    proc->start(QString::fromStdString(m_ffmpegPath), args);
}

void TripGridPanel::enqueueThumb(TripTile* tile,
                                 const QString& slot,
                                 const QString& path)
{
    m_thumbQueue.push_back({tile, slot, path});
}

void TripGridPanel::loadNextThumbnail()
{
    if (m_thumbQueue.empty()) return;

    auto [tile, slot, path] = m_thumbQueue.front();
    m_thumbQueue.pop_front();

    // tile may have been deleted if the user switched manifests mid-load
    if (tile) {
        QPixmap px(path);
        if (!px.isNull())
            tile->setThumbnail(slot,
                px.scaled(TripTile::THUMB_W, TripTile::THUMB_H,
                          Qt::KeepAspectRatio,
                          Qt::SmoothTransformation));
    }

    if (!m_thumbQueue.empty())
        QTimer::singleShot(0, this, &TripGridPanel::loadNextThumbnail);
}

void TripGridPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_stack->currentIndex() == PAGE_GRID)
        QTimer::singleShot(0, this, &TripGridPanel::layoutTiles);
}

void TripGridPanel::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const double delta = event->angleDelta().y() > 0 ? ZOOM_STEP : -ZOOM_STEP;
        m_zoomFactor = qBound(ZOOM_MIN, m_zoomFactor + delta, ZOOM_MAX);
        applyZoom();
        emit zoomChanged(m_zoomFactor);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

bool TripGridPanel::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            wheelEvent(we);
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void TripGridPanel::setZoom(double factor)
{
    m_zoomFactor = qBound(ZOOM_MIN, factor, ZOOM_MAX);
    applyZoom();
}

void TripGridPanel::applyZoom()
{
    for (auto* tile : m_tiles)
        tile->setZoom(m_zoomFactor);
    layoutTiles();
}

void TripGridPanel::loadSearchResults(const QList<SearchResult>& results,
                                      const CamClops::ManifestEntry& virtualEntry)
{
    m_currentManifest = virtualEntry;
    clearTiles();
    m_thumbQueue.clear();
    m_procQueue.clear();

    ConfigManager config;
    config.loadSettings();
    m_imperial = config.getUseImperial();

    {
        int n = results.size();
        m_manifestHeader->setText(
            QString("Search Results —— %1 trip%2").arg(n).arg(n == 1 ? "" : "s"));
        m_manifestHeader->show();
    }

    for (const auto& r : results) {
        auto* tile = new TripTile(r.trip, m_imperial,
                                  r.manifest.path, r.manifest.id,
                                  m_gridContainer);
        tile->show();
        connect(tile, &TripTile::doubleClicked,
                [this, r]() { emit tripDoubleClicked(r.manifest, r.trip); });
        connect(tile, &TripTile::buildRequested,
                [this, r]() { onBuildRequested(r.trip, r.manifest); });
        connect(tile, &TripTile::tripChanged,
                [this, r]() {
                    // Reload the tile's source manifest to refresh its data
                    CamClops::ConfigManager mgr;
                    auto trips = mgr.loadTripCache(r.manifest.manifestFile);
                    for (auto* t : m_tiles) {
                        for (const auto& fresh : trips) {
                            if (fresh.id == t->trip().id) { t->refreshFrom(fresh); break; }
                        }
                    }
                });
        tile->setZoom(m_zoomFactor);
        m_tiles.push_back(tile);

        bool isPark = (r.trip.footageType == "parking");
        for (const std::string slot : {"front", "rear", "interior"}) {
            auto it = r.trip.firstThumbs.find(slot);
            if (it != r.trip.firstThumbs.end() && !it->second.empty()) {
                enqueueThumb(tile,
                             QString::fromStdString(slot),
                             QString::fromStdString(it->second));
            } else {
                auto [vidPath, offset] = pickThumbSource(r.trip, slot, isPark);
                if (!vidPath.empty())
                    grabVideoThumb(tile, QString::fromStdString(slot),
                                   QString::fromStdString(vidPath), offset, isPark,
                                   QString::fromStdString(r.manifest.manifestFile),
                                   QString::fromStdString(r.manifest.id),
                                   QString::fromStdString(r.trip.id));
            }
        }
    }

    m_stack->setCurrentIndex(PAGE_GRID);
    QTimer::singleShot(0, this, &TripGridPanel::layoutTiles);
    if (!m_thumbQueue.empty())
        QTimer::singleShot(0, this, &TripGridPanel::loadNextThumbnail);
}

void TripGridPanel::onBuildRequested(const CamClops::Trip& trip,
                                     const CamClops::ManifestEntry& manifest)
{
    CamClops::Trip freshTrip = trip;
    if (!manifest.isVirtual && !manifest.manifestFile.empty()) {
        CamClops::ConfigManager mgr;
        auto trips = mgr.loadTripCache(manifest.manifestFile);
        for (const auto& t : trips) {
            if (t.id == trip.id) { freshTrip = t; break; }
        }
    }

    TripBuildDialog dlg(manifest, freshTrip, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    if (dlg.processNow()) {
        VideoOptions opts = dlg.buildOptions();
        auto* job = new CollageJob(freshTrip, opts, manifest.id);
        JobQueue::instance().enqueue(job);
    }
}

void TripGridPanel::onJobFinished(Job*, bool ok)
{
    if (!ok || m_currentManifest.isVirtual || m_currentManifest.manifestFile.empty()) return;

    ConfigManager config;
    config.loadSettings();
    auto trips = config.loadTripCache(m_currentManifest.manifestFile);
    for (auto* tile : m_tiles) {
        for (const auto& t : trips) {
            if (t.id == tile->trip().id) {
                tile->refreshFrom(t);
                break;
            }
        }
    }
}

void TripGridPanel::saveGrabbedThumb(const QByteArray& jpeg, const ProcRequest& req)
{
    if (req.manifestFile.isEmpty() || req.manifestId.isEmpty() || req.eventId.isEmpty()) return;

    QString thumbDir  = QFileInfo(req.manifestFile).absolutePath()
                        + "/clops_thumbs_" + req.manifestId + "/";
    QDir().mkpath(thumbDir);
    QString thumbPath = thumbDir + req.eventId + "_" + req.slot + ".jpg";

    { QFile f(thumbPath); if (f.open(QIODevice::WriteOnly)) f.write(jpeg); }

    // Relative to manifest parent (= source root in the common case; stored as-is otherwise).
    std::string relThumb = ("clops_thumbs_" + req.manifestId + "/"
                            + req.eventId + "_" + req.slot + ".jpg").toStdString();

    std::string mfPath = req.manifestFile.toStdString();
    try {
        std::ifstream ifs(mfPath);
        if (!ifs.is_open()) return;
        json root;
        ifs >> root;
        ifs.close();
        if (!root.contains("trips") || !root["trips"].is_array()) return;
        std::string eid  = req.eventId.toStdString();
        std::string slot = req.slot.toStdString();
        bool changed = false;
        for (auto& jt : root["trips"]) {
            if (!jt.contains("id") || jt["id"].get<std::string>() != eid) continue;
            if (!jt.contains("firstThumbs") || !jt["firstThumbs"].is_object())
                jt["firstThumbs"] = json::object();
            jt["firstThumbs"][slot] = relThumb;
            changed = true;
            break;
        }
        if (!changed) return;
        std::ofstream ofs(mfPath);
        if (ofs.is_open()) ofs << root.dump(2);
    } catch (...) {}
}
// SN: 00122
