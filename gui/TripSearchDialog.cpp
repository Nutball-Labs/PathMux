// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "TripSearchDialog.h"
#include "format_helpers.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QTimeEdit>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QButtonGroup>
#include <QProgressBar>
#include <QLabel>
#include <QGroupBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDateTime>
#include <QTimeZone>
#include <QThread>
#include <QWheelEvent>

// ---------------------------------------------------------------------------
// SearchParams — internal; not exposed in header
// ---------------------------------------------------------------------------
struct SearchParams {
    bool    useDateRange = false;
    QDate   dateFrom;
    QDate   dateTo;
    bool    useTimeRange  = false;
    QTime   timeFrom;
    QTime   timeTo;
    bool    useLocalTime  = true;
    bool    hasNote      = false;
    bool    useLocation  = false;
    double  lat          = 0.0;
    double  lon          = 0.0;
    double  radiusKm     = 1.0;
    QString notesFilter;
    double  minDistKm    = 0.0;
    double  maxDistKm    = 0.0;
};

// ---------------------------------------------------------------------------
// TripSearchWorker — runs on a background QThread
// ---------------------------------------------------------------------------
class TripSearchWorker : public QObject {
    Q_OBJECT
public:
    explicit TripSearchWorker(const SearchParams& p) : m_params(p) {}

signals:
    void progress(int done, int total);
    void finished(QList<SearchResult> results);

public slots:
    void run()
    {
        qRegisterMetaType<SearchResult>();
        qRegisterMetaType<QList<SearchResult>>();

        CamClops::ConfigManager cfg;
        cfg.loadSettings();
        auto manifests = cfg.loadManifestIndex();
        int total = (int)manifests.size();
        QList<SearchResult> results;

        for (int i = 0; i < total; ++i) {
            emit progress(i, total);
            const auto& entry = manifests[i];
            if (entry.isVirtual || entry.manifestFile.empty()) continue;

            auto trips = cfg.loadTripCache(entry.manifestFile);
            for (const auto& trip : trips) {
                double distKm = computeDist(trip);
                if (matchesFilters(trip, distKm))
                    results.append({entry, trip, distKm});
            }
        }
        emit progress(total, total);
        emit finished(results);
    }

private:
    bool matchesFilters(const CamClops::Trip& trip, double distKm) const
    {
        // startEpoch == 0 means the trip has no valid timestamp — exclude from
        // any time/date filter rather than matching the Unix epoch by accident.
        if (m_params.useDateRange) {
            if (trip.startEpoch == 0) return false;
            QDate d = QDateTime::fromSecsSinceEpoch((qint64)trip.startEpoch).date();
            if (d < m_params.dateFrom || d > m_params.dateTo) return false;
        }
        if (m_params.useTimeRange) {
            if (trip.startEpoch == 0) return false;
            auto dt = m_params.useLocalTime
                ? QDateTime::fromSecsSinceEpoch((qint64)trip.startEpoch)
                : QDateTime::fromSecsSinceEpoch((qint64)trip.startEpoch, QTimeZone::utc());
            QTime t = dt.time();
            if (m_params.timeFrom <= m_params.timeTo) {
                if (t < m_params.timeFrom || t > m_params.timeTo) return false;
            } else {
                if (t < m_params.timeFrom && t > m_params.timeTo) return false;
            }
        }
        if (m_params.hasNote && trip.note.empty()) return false;
        if (!m_params.notesFilter.isEmpty()) {
            if (!QString::fromStdString(trip.note)
                    .contains(m_params.notesFilter, Qt::CaseInsensitive)) return false;
        }
        if (m_params.useLocation) {
            if (trip.gpsTrackStatus == "none") return false;
            if (trip.startLat == 0.0 && trip.startLon == 0.0 &&
                trip.endLat   == 0.0 && trip.endLon   == 0.0) return false;
            double dS = CamClops::haversineKm(m_params.lat, m_params.lon,
                                              trip.startLat, trip.startLon);
            double dE = CamClops::haversineKm(m_params.lat, m_params.lon,
                                              trip.endLat,   trip.endLon);
            if (dS > m_params.radiusKm && dE > m_params.radiusKm) return false;
        }
        if (m_params.minDistKm > 0.0 && (distKm < 0.0 || distKm < m_params.minDistKm))
            return false;
        if (m_params.maxDistKm > 0.0 && distKm >= 0.0 && distKm > m_params.maxDistKm)
            return false;
        return true;
    }

    double computeDist(const CamClops::Trip& trip) const
    {
        if ((trip.startLat == 0.0 && trip.startLon == 0.0) ||
            (trip.endLat   == 0.0 && trip.endLon   == 0.0)) return -1.0;
        return CamClops::haversineKm(trip.startLat, trip.startLon,
                                     trip.endLat,   trip.endLon);
    }

    SearchParams m_params;
};

// ---------------------------------------------------------------------------
// TripSearchDialog
// ---------------------------------------------------------------------------
TripSearchDialog::TripSearchDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Search Trips");
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(540, 0);
    m_baseFontPt = font().pointSizeF();

    qRegisterMetaType<SearchResult>();
    qRegisterMetaType<QList<SearchResult>>();

    CamClops::ConfigManager cfg;
    cfg.loadSettings();
    m_imperial = cfg.getUseImperial();

    auto* main = new QVBoxLayout(this);
    main->setSpacing(8);

    // ---- Search Criteria group ----
    auto* grp = new QGroupBox("Search Criteria", this);
    auto* gl  = new QVBoxLayout(grp);
    gl->setSpacing(6);

    // Date range row
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("Date:", this));
        m_dateFrom = new QDateEdit(QDate::currentDate().addDays(-30), this);
        m_dateFrom->setCalendarPopup(true);
        m_dateFrom->setDisplayFormat("yyyy-MM-dd");
        m_dateTo   = new QDateEdit(QDate::currentDate(), this);
        m_dateTo->setCalendarPopup(true);
        m_dateTo->setDisplayFormat("yyyy-MM-dd");
        m_anyDate  = new QCheckBox("Any", this);
        m_anyDate->setChecked(true);
        m_dateFrom->setEnabled(false);
        m_dateTo->setEnabled(false);
        row->addWidget(m_dateFrom);
        row->addWidget(new QLabel("to", this));
        row->addWidget(m_dateTo);
        row->addStretch();
        row->addWidget(m_anyDate);
        gl->addLayout(row);
    }

    // Time of day row
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("Time of day:", this));
        m_timeFrom = new QTimeEdit(QTime(0, 0), this);
        m_timeFrom->setDisplayFormat("HH:mm");
        m_timeTo   = new QTimeEdit(QTime(23, 59), this);
        m_timeTo->setDisplayFormat("HH:mm");
        m_anyTime  = new QCheckBox("Any", this);
        m_anyTime->setChecked(true);
        m_timeFrom->setEnabled(false);
        m_timeTo->setEnabled(false);
        m_timeZone = new QComboBox(this);
        m_timeZone->addItem("Local");
        m_timeZone->addItem("UTC (Zulu)");
        m_timeZone->setEnabled(false);
        row->addWidget(m_timeFrom);
        row->addWidget(new QLabel("to", this));
        row->addWidget(m_timeTo);
        row->addWidget(m_timeZone);
        row->addStretch();
        row->addWidget(m_anyTime);
        gl->addLayout(row);
    }

    {
        auto* sep = new QFrame(this);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        gl->addWidget(sep);
    }

    // Location enable + named-location preset row
    {
        auto* row = new QHBoxLayout;
        m_locationEnable = new QCheckBox("Search by location", this);
        row->addWidget(m_locationEnable);

        m_locationPreset = new QComboBox(this);
        m_locationPreset->setMinimumWidth(160);
        m_locationPreset->addItem("-- Named location --");
        {
            CamClops::ConfigManager cfgLoc;
            cfgLoc.loadSettings();
            auto locs = cfgLoc.loadLocations();
            for (const auto& loc : locs)
                m_locationPreset->addItem(QString::fromStdString(loc.name));
            m_locationPreset->setEnabled(!locs.empty());
            if (locs.empty())
                m_locationPreset->setToolTip("No named locations defined");
        }
        row->addWidget(m_locationPreset);
        row->addStretch();
        gl->addLayout(row);
    }

    // Lat/lon/radius row (disabled until checkbox ticked)
    m_locationRow = new QWidget(this);
    {
        auto* row = new QHBoxLayout(m_locationRow);
        row->setContentsMargins(16, 0, 0, 0);
        row->addWidget(new QLabel("Lat:", this));
        m_lat = new QDoubleSpinBox(this);
        m_lat->setRange(-90.0, 90.0);
        m_lat->setDecimals(6);
        m_lat->setSingleStep(0.001);
        row->addWidget(m_lat);
        row->addWidget(new QLabel("Lon:", this));
        m_lon = new QDoubleSpinBox(this);
        m_lon->setRange(-180.0, 180.0);
        m_lon->setDecimals(6);
        m_lon->setSingleStep(0.001);
        row->addWidget(m_lon);
        row->addWidget(new QLabel("Radius:", this));
        m_radius = new QDoubleSpinBox(this);
        m_radius->setRange(0.1, 5000.0);
        m_radius->setValue(5.0);
        m_radius->setSuffix(m_imperial ? " mi" : " km");
        row->addWidget(m_radius);

        // km / mi unit toggle — defaults to app setting, overridable per search
        m_radiusKmBtn = new QPushButton("km", this);
        m_radiusMiBtn = new QPushButton("mi", this);
        m_radiusKmBtn->setCheckable(true);
        m_radiusMiBtn->setCheckable(true);
        m_radiusKmBtn->setChecked(!m_imperial);
        m_radiusMiBtn->setChecked(m_imperial);
        auto* unitGroup = new QButtonGroup(this);
        unitGroup->setExclusive(true);
        unitGroup->addButton(m_radiusKmBtn);
        unitGroup->addButton(m_radiusMiBtn);

        const QString toggleStyle =
            "QPushButton { padding: 1px 5px; font-size: 8pt; border: 1px solid #666; }"
            "QPushButton:checked { background: #0078d4; color: white; border-color: #0078d4; }"
            "QPushButton:!checked { background: #3a3a3a; color: #888; }"
            "QPushButton:disabled { color: #555; border-color: #444; }";
        m_radiusKmBtn->setStyleSheet(toggleStyle);
        m_radiusMiBtn->setStyleSheet(toggleStyle);
        m_radiusKmBtn->setFixedHeight(22);
        m_radiusMiBtn->setFixedHeight(22);

        connect(m_radiusKmBtn, &QPushButton::toggled, this, [this](bool checked) {
            if (!checked) return;
            m_radius->setValue(m_radius->value() / 0.621371);  // mi → km
            m_radius->setSuffix(" km");
        });
        connect(m_radiusMiBtn, &QPushButton::toggled, this, [this](bool checked) {
            if (!checked) return;
            m_radius->setValue(m_radius->value() * 0.621371);  // km → mi
            m_radius->setSuffix(" mi");
        });

        row->addWidget(m_radiusKmBtn);
        row->addWidget(m_radiusMiBtn);
        row->addStretch();
    }
    m_locationRow->setEnabled(false);
    gl->addWidget(m_locationRow);

    auto* locNote = new QLabel(
        "  Checks trip start/end coordinates. Trips without GPS are excluded.", this);
    locNote->setStyleSheet("color: #888; font-size: 8pt;");
    gl->addWidget(locNote);

    {
        auto* sep = new QFrame(this);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        gl->addWidget(sep);
    }

    // Notes row
    {
        auto* row = new QHBoxLayout;
        m_hasNote = new QCheckBox("Has note", this);
        row->addWidget(m_hasNote);
        row->addWidget(new QLabel("  Contains:", this));
        m_notesFilter = new QLineEdit(this);
        m_notesFilter->setPlaceholderText("(empty = any text)");
        row->addWidget(m_notesFilter);
        gl->addLayout(row);
    }

    // Distance row
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("Distance:", this));
        row->addWidget(new QLabel("Min:", this));
        m_minDist = new QDoubleSpinBox(this);
        m_minDist->setRange(0.0, 99999.0);
        m_minDist->setDecimals(1);
        row->addWidget(m_minDist);
        row->addWidget(new QLabel("Max:", this));
        m_maxDist = new QDoubleSpinBox(this);
        m_maxDist->setRange(0.0, 99999.0);
        m_maxDist->setDecimals(1);
        row->addWidget(m_maxDist);
        m_distUnit = new QLabel(m_imperial ? "mi  (0 = no limit)" : "km  (0 = no limit)", this);
        row->addWidget(m_distUnit);
        row->addStretch();
        gl->addLayout(row);
    }

    main->addWidget(grp);

    // ---- Search Queue section ----
    {
        auto* queueBox = new QGroupBox("Search Queue", this);
        auto* ql = new QHBoxLayout(queueBox);
        m_searchBtn = new QPushButton("Search", this);
        m_searchBtn->setDefault(true);
        ql->addWidget(m_searchBtn);

        m_progress = new QProgressBar(this);
        m_progress->setRange(0, 1);
        m_progress->setValue(0);
        m_progress->setFixedHeight(14);
        m_progress->setTextVisible(false);
        ql->addWidget(m_progress, 1);

        m_statusLabel = new QLabel(this);
        ql->addWidget(m_statusLabel);
        main->addWidget(queueBox);
    }

    // Signals
    connect(m_anyDate,        &QCheckBox::toggled,   this, &TripSearchDialog::onAnyDateToggled);
    connect(m_anyTime,        &QCheckBox::toggled,   this, &TripSearchDialog::onAnyTimeToggled);
    connect(m_locationEnable, &QCheckBox::toggled,   this, &TripSearchDialog::onLocationToggled);
    connect(m_locationPreset, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TripSearchDialog::onLocationPresetChanged);
    connect(m_searchBtn,      &QPushButton::clicked, this, &TripSearchDialog::onSearch);

    adjustSize();

    // Install event filter on all child widgets so Ctrl+scroll zooms even when
    // the mouse is over a spinbox, date edit, etc. (those widgets consume wheel
    // events before they reach the dialog's own wheelEvent).
    for (auto* w : findChildren<QWidget*>())
        w->installEventFilter(this);
}

void TripSearchDialog::onAnyDateToggled(bool checked)
{
    m_dateFrom->setEnabled(!checked);
    m_dateTo->setEnabled(!checked);
}

void TripSearchDialog::onAnyTimeToggled(bool checked)
{
    m_timeFrom->setEnabled(!checked);
    m_timeTo->setEnabled(!checked);
    m_timeZone->setEnabled(!checked);
}

void TripSearchDialog::onLocationToggled(bool checked)
{
    m_locationRow->setEnabled(checked);
}

void TripSearchDialog::onLocationPresetChanged(int index)
{
    if (index <= 0) return;
    CamClops::ConfigManager cfgLoc;
    cfgLoc.loadSettings();
    auto locs = cfgLoc.loadLocations();
    int locIdx = index - 1;
    if (locIdx < 0 || locIdx >= (int)locs.size()) return;
    const auto& loc = locs[locIdx];
    m_lat->setValue(loc.lat);
    m_lon->setValue(loc.lon);
    double radiusKm = loc.radiusMetres / 1000.0;
    m_radius->setValue(qMax(0.1, m_radiusMiBtn->isChecked() ? radiusKm * 0.621371 : radiusKm));
    m_locationEnable->setChecked(true);
    m_locationRow->setEnabled(true);
}

void TripSearchDialog::onSearch()
{
    m_searchBtn->setEnabled(false);
    m_statusLabel->clear();
    m_progress->setRange(0, 0);

    SearchParams p;
    p.useDateRange = !m_anyDate->isChecked();
    p.dateFrom     = m_dateFrom->date();
    p.dateTo       = m_dateTo->date();
    p.useTimeRange  = !m_anyTime->isChecked();
    p.timeFrom      = m_timeFrom->time();
    p.timeTo        = m_timeTo->time();
    p.useLocalTime  = (m_timeZone->currentIndex() == 0);
    p.hasNote      = m_hasNote->isChecked();
    p.useLocation  = m_locationEnable->isChecked();
    p.lat          = m_lat->value();
    p.lon          = m_lon->value();
    p.radiusKm     = m_radiusMiBtn->isChecked()
                       ? m_radius->value() / 0.621371
                       : m_radius->value();
    p.notesFilter  = m_notesFilter->text().trimmed();
    double minVal  = m_minDist->value();
    double maxVal  = m_maxDist->value();
    p.minDistKm    = m_imperial ? minVal / 0.621371 : minVal;
    p.maxDistKm    = m_imperial ? maxVal / 0.621371 : maxVal;

    auto* worker = new TripSearchWorker(p);
    auto* thread = new QThread(this);
    worker->moveToThread(thread);

    connect(thread, &QThread::started,           worker, &TripSearchWorker::run);
    connect(worker, &TripSearchWorker::progress,  this,  &TripSearchDialog::onSearchProgress);
    connect(worker, &TripSearchWorker::finished,  this,  &TripSearchDialog::onSearchFinished);
    connect(worker, &TripSearchWorker::finished,  thread, &QThread::quit);
    connect(worker, &TripSearchWorker::finished,  worker, &QObject::deleteLater);
    connect(thread, &QThread::finished,           thread, &QObject::deleteLater);

    thread->start();
}

void TripSearchDialog::onSearchProgress(int done, int total)
{
    if (total > 0 && done < total) {
        m_progress->setRange(0, total);
        m_progress->setValue(done);
    }
}

void TripSearchDialog::onSearchFinished(QList<SearchResult> results)
{
    m_progress->setRange(0, 1);
    m_progress->setValue(1);
    m_searchBtn->setEnabled(true);

    int n = results.size();
    m_statusLabel->setText(QString("Found %1 trip%2").arg(n).arg(n == 1 ? "" : "s"));

    if (!results.isEmpty()) {
        QString label = QString("Search Results (%1 trip%2)").arg(n).arg(n == 1 ? "" : "s");
        emit searchCompleted(results, label);
    }
}

void TripSearchDialog::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const double step = event->angleDelta().y() > 0 ? 0.1 : -0.1;
        m_zoomFactor = qBound(0.5, m_zoomFactor + step, 3.0);
        QFont f = font();
        f.setPointSizeF(m_baseFontPt * m_zoomFactor);
        setFont(f);
        adjustSize();
        event->accept();
        return;
    }
    QDialog::wheelEvent(event);
}

bool TripSearchDialog::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            wheelEvent(we);
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}

#include "TripSearchDialog.moc"
// SN: 00123
