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
#include <algorithm>

using namespace CamClops;

TripGridPanel::TripGridPanel(QWidget* parent)
    : QWidget(parent)
{
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

    m_bgLogo = QPixmap(":/images/Nutball-Labs_logo.png");

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

    ConfigManager config;
    config.loadSettings();
    m_imperial = config.getUseImperial();
    auto trips = config.loadTripCache(entry.manifestFile);

    // Build sticky header using actual loaded count, not cached entry.tripCount,
    // so it stays accurate after trip deletions/archives.
    {
        QString id   = QString::fromStdString(entry.id).toUpper();
        QString path = QString::fromStdString(entry.path);
        int     n    = (int)trips.size();
        m_manifestHeader->setText(
            QString("%1 \u2014\u2014 %2 \u2014\u2014 %3 trip%4")
                .arg(id).arg(path).arg(n).arg(n == 1 ? "" : "s"));
        m_manifestHeader->show();
    }

    for (const auto& t : trips) {
        auto* tile = new TripTile(t, m_imperial, entry.path, entry.id, m_gridContainer);
        tile->show();
        connect(tile, &TripTile::doubleClicked,
                [this, t]() { emit tripDoubleClicked(m_currentManifest, t); });
        connect(tile, &TripTile::buildRequested,
                [this, t]() { onBuildRequested(t); });
        connect(tile, &TripTile::tripChanged,
                [this]() { loadManifest(m_currentManifest); });
        tile->setZoom(m_zoomFactor);
        m_tiles.push_back(tile);

        // Queue thumbnails for deferred loading
        for (const std::string slot : {"front", "rear"}) {
            auto it = t.firstThumbs.find(slot);
            if (it != t.firstThumbs.end() && !it->second.empty())
                enqueueThumb(tile,
                             QString::fromStdString(slot),
                             QString::fromStdString(it->second));
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

void TripGridPanel::onBuildRequested(const CamClops::Trip& trip)
{
    // Reload this trip from disk — clops_sync_analyze.py (or any other tool) may
    // have written cameraSync or other data to the manifest after the GUI last
    // scanned.  Fall back to the cached trip if the reload fails.
    CamClops::Trip freshTrip = trip;
    if (!m_currentManifest.manifestFile.empty()) {
        CamClops::ConfigManager mgr;
        auto trips = mgr.loadTripCache(m_currentManifest.manifestFile);
        for (const auto& t : trips) {
            if (t.id == trip.id) { freshTrip = t; break; }
        }
    }

    TripBuildDialog dlg(m_currentManifest, freshTrip, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    if (dlg.processNow()) {
        VideoOptions opts = dlg.buildOptions();
        auto* job = new CollageJob(freshTrip, opts, m_currentManifest.id);
        JobQueue::instance().enqueue(job);
    }
}

void TripGridPanel::onJobFinished(Job*, bool ok)
{
    if (!ok || m_currentManifest.manifestFile.empty()) return;

    // Reload trip data from the manifest so tile indicators (GPS/MAP/DASH/HUD)
    // reflect whatever the job just wrote, without rebuilding the tile widgets.
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
// SN: 00113
