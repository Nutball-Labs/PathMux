// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QWidget>
#include <deque>
#include <tuple>
#include <vector>
#include "config_manager.hpp"
#include "trip_detection.hpp"

class QStackedWidget;
class QScrollArea;
class QLabel;
class QWheelEvent;
class TripTile;
class EmptyManifestWidget;

class TripGridPanel : public QWidget {
    Q_OBJECT
public:
    explicit TripGridPanel(QWidget* parent = nullptr);

    // Called after a successful scan to re-evaluate which page to show
    void refreshPageState();

public slots:
    void loadManifest(const Pathmux::ManifestEntry& entry);
    void setZoom(double factor);

signals:
    void tripDoubleClicked(const Pathmux::ManifestEntry& manifest,
                           const Pathmux::Trip& trip);
    void scanRequested();
    void zoomChanged(double factor);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    static constexpr int    PAGE_EMPTY   = 0;
    static constexpr int    PAGE_NONE    = 1;
    static constexpr int    PAGE_GRID    = 2;
    static constexpr int    TILE_SPACING = 8;
    static constexpr double ZOOM_MIN     = 0.5;
    static constexpr double ZOOM_MAX     = 3.0;
    static constexpr double ZOOM_STEP    = 0.1;

    QStackedWidget*      m_stack;
    EmptyManifestWidget* m_emptyWidget;
    QLabel*              m_noSelLabel;
    QScrollArea*         m_scrollArea;
    QWidget*             m_gridContainer;
    QLabel*              m_manifestHeader = nullptr;  // sticky bar above tile grid

    Pathmux::ManifestEntry  m_currentManifest;
    std::vector<TripTile*>  m_tiles;
    bool                    m_imperial    = false;
    double                  m_zoomFactor  = 1.0;

    // Deferred thumbnail loading queue: (tile*, slot, path)
    std::deque<std::tuple<TripTile*, QString, QString>> m_thumbQueue;

    void clearTiles();
    void layoutTiles();
    void applyZoom();
    void enqueueThumb(TripTile* tile, const QString& slot, const QString& path);

private slots:
    void loadNextThumbnail();
    void onBuildRequested(const Pathmux::Trip& trip);
};
// SN: 00093
