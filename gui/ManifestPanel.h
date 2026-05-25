// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QWidget>
#include <QPixmap>
#include <vector>
#include "config_manager.hpp"

class QListWidget;
class QListWidgetItem;
class QComboBox;
class QPushButton;
class QWheelEvent;
class ManifestItemDelegate;

class ManifestPanel : public QWidget {
    Q_OBJECT
public:
    explicit ManifestPanel(QWidget* parent = nullptr);

    void refresh();
    void selectEntry(const CamClops::ManifestEntry& entry);
    void setZoom(double factor);

    void addVirtualEntry(const CamClops::ManifestEntry& entry);
    void removeVirtualEntry(const QString& id);

signals:
    void manifestSelected(const CamClops::ManifestEntry& entry);
    void scanRequested();
    void rebuildRequested(const CamClops::ManifestEntry& entry);
    void virtualEntryRemoved(const QString& id);
    void zoomChanged(double factor);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onItemClicked(QListWidgetItem* item);
    void onSortChanged(int index);
    void onAddClicked();
    void onContextMenu(const QPoint& pos);

private:
    QPixmap                m_bgLogo;
    QListWidget*           m_list;
    QComboBox*             m_sortCombo;
    QPushButton*           m_addBtn;
    ManifestItemDelegate*  m_delegate;

    std::vector<CamClops::ManifestEntry> m_entries;
    double m_baseFontPt  = 9.0;
    double m_zoomFactor  = 1.0;

    static constexpr double ZOOM_MIN  = 0.5;
    static constexpr double ZOOM_MAX  = 3.0;
    static constexpr double ZOOM_STEP = 0.1;

    void populateList();
    void applySort();
    void applyListZoom();
};
// SN: 00122
