// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <QList>
#include "config_manager.hpp"
#include "trip_detection.hpp"

// One result row: the source manifest + trip + precomputed straight-line distance.
// distKm == -1.0 means no GPS data available.
struct SearchResult {
    CamClops::ManifestEntry manifest;
    CamClops::Trip          trip;
    double                  distKm = -1.0;
};
Q_DECLARE_METATYPE(SearchResult)

class QCheckBox;
class QComboBox;
class QDateEdit;
class QTimeEdit;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QLabel;

class QWheelEvent;

class TripSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit TripSearchDialog(QWidget* parent = nullptr);

signals:
    void searchCompleted(QList<SearchResult> results, QString label);

protected:
    void wheelEvent(QWheelEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onSearch();
    void onSearchProgress(int done, int total);
    void onSearchFinished(QList<SearchResult> results);
    void onLocationToggled(bool checked);
    void onLocationPresetChanged(int index);
    void onAnyDateToggled(bool checked);
    void onAnyTimeToggled(bool checked);

private:
    QCheckBox*      m_anyDate;
    QDateEdit*      m_dateFrom;
    QDateEdit*      m_dateTo;
    QCheckBox*      m_anyTime;
    QTimeEdit*      m_timeFrom;
    QTimeEdit*      m_timeTo;
    QComboBox*      m_timeZone;
    QCheckBox*      m_locationEnable;
    QComboBox*      m_locationPreset;
    QWidget*        m_locationRow;
    QDoubleSpinBox* m_lat;
    QDoubleSpinBox* m_lon;
    QDoubleSpinBox* m_radius;
    QPushButton*    m_radiusKmBtn;
    QPushButton*    m_radiusMiBtn;
    QCheckBox*      m_hasNote;
    QLineEdit*      m_notesFilter;
    QDoubleSpinBox* m_minDist;
    QDoubleSpinBox* m_maxDist;
    QLabel*         m_distUnit;
    QPushButton*    m_searchBtn;
    QProgressBar*   m_progress;
    QLabel*         m_statusLabel;
    bool            m_imperial    = false;
    double          m_zoomFactor  = 1.0;
    double          m_baseFontPt  = 0.0;
};
// SN: 00122
