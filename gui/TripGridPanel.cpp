// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "TripGridPanel.h"
#include "TripTile.h"
#include "EmptyManifestWidget.h"
#include <QStackedWidget>
#include <QScrollArea>
#include <QLabel>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QTimer>

using namespace Pathmux;

TripGridPanel::TripGridPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(0, 0, 0, 0);

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

    // Determine initial page
    ConfigManager config;
    config.loadSettings();
    m_imperial = config.getUseImperial();
    auto index = config.loadManifestIndex();
    m_stack->setCurrentIndex(index.empty() ? PAGE_EMPTY : PAGE_NONE);
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

    for (const auto& t : trips) {
        auto* tile = new TripTile(t, m_imperial, m_gridContainer);
        tile->show();
        connect(tile, &TripTile::doubleClicked,
                [this, t]() { emit tripDoubleClicked(m_currentManifest, t); });
        tile->setTextZoom(m_zoomFactor);
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
    int availW = m_scrollArea->viewport()->width();
    int cols   = qMax(1, (availW + TILE_SPACING) /
                            (TripTile::W + TILE_SPACING));
    int n      = (int)m_tiles.size();

    for (int i = 0; i < n; ++i) {
        int row = i / cols;
        int col = i % cols;
        int x = TILE_SPACING + col * (TripTile::W + TILE_SPACING);
        int y = TILE_SPACING + row * (TripTile::H + TILE_SPACING);
        m_tiles[i]->setGeometry(x, y, TripTile::W, TripTile::H);
    }

    int rows   = n == 0 ? 0 : (n + cols - 1) / cols;
    int totalH = rows * (TripTile::H + TILE_SPACING) + TILE_SPACING;
    m_gridContainer->setMinimumSize(
        cols * (TripTile::W + TILE_SPACING) + TILE_SPACING,
        qMax(totalH, m_scrollArea->viewport()->height()));
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
        tile->setTextZoom(m_zoomFactor);
}
// SN: 00090
