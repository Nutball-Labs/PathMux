// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QWidget>
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
    void selectEntry(const Pathmux::ManifestEntry& entry);
    void setZoom(double factor);

signals:
    void manifestSelected(const Pathmux::ManifestEntry& entry);
    void scanRequested();
    void zoomChanged(double factor);

protected:
    void wheelEvent(QWheelEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onItemClicked(QListWidgetItem* item);
    void onSortChanged(int index);
    void onAddClicked();

private:
    QListWidget*           m_list;
    QComboBox*             m_sortCombo;
    QPushButton*           m_addBtn;
    ManifestItemDelegate*  m_delegate;

    std::vector<Pathmux::ManifestEntry> m_entries;
    double m_baseFontPt  = 9.0;
    double m_zoomFactor  = 1.0;

    static constexpr double ZOOM_MIN  = 0.5;
    static constexpr double ZOOM_MAX  = 3.0;
    static constexpr double ZOOM_STEP = 0.1;

    void populateList();
    void applySort();
    void applyListZoom();
};
// SN: 00090
