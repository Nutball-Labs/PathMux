// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "TripPropertiesDialog.h"
#include "JobQueue.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QTabBar>
#include <QStackedWidget>
#include <QPainter>
#include <QStylePainter>
#include <QStyleOptionTab>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidget>
#include <QMessageBox>
#include <QDesktopServices>
#include <QCloseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QProcess>
#include <QScrollArea>
#include <QUrl>
#include <QString>
#include <QCoreApplication>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include "config_manager.hpp"
#include "gps_export.hpp"
#include "json.hpp"

using namespace CamClops;
using json = nlohmann::json;

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

// ---------------------------------------------------------------------------
// InactiveAwareTabBar
// A QTabBar that can be marked inactive.  When inactive, all tabs are drawn
// without the State_Selected flag so no tab appears highlighted — only the
// active bar's current tab shows the platform selection indicator.
// ---------------------------------------------------------------------------
class InactiveAwareTabBar : public QTabBar {
    Q_OBJECT
    bool m_active = true;
public:
    using QTabBar::QTabBar;
    void setBarActive(bool active) { m_active = active; update(); }
protected:
    void paintEvent(QPaintEvent* event) override {
        if (m_active) { QTabBar::paintEvent(event); return; }
        QStylePainter p(this);
        for (int i = 0; i < count(); ++i) {
            QStyleOptionTab opt;
            initStyleOption(&opt, i);
            opt.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus);
            opt.palette.setColor(QPalette::ButtonText, QColor(0x80, 0x80, 0x80));
            opt.palette.setColor(QPalette::WindowText, QColor(0x80, 0x80, 0x80));
            p.drawControl(QStyle::CE_TabBarTab, opt);
        }
    }
};

// ---------------------------------------------------------------------------
// TripPropertiesDialog
// ---------------------------------------------------------------------------
TripPropertiesDialog::TripPropertiesDialog(const Trip& trip,
                                           const std::string& sourcePath,
                                           const std::string& mid,
                                           QWidget* parent)
    : QDialog(parent), m_trip(trip), m_sourcePath(sourcePath), m_mid(mid)
{
    QString addr = mid.empty()
        ? QString::fromStdString(trip.id)
        : QString::fromStdString(mid + ":" + trip.id);
    setWindowTitle(QString("Trip Properties \u2014 %1").arg(addr));
    resize(640, 520);
    setSizeGripEnabled(true);

    auto* vbox = new QVBoxLayout(this);

    m_topTabBar = new InactiveAwareTabBar(this);
    m_topTabBar->setExpanding(false);
    m_tabStack  = new QStackedWidget(this);

    bool hasGps = (trip.gpsTrackStatus == "complete");

    // makeStatusIcon is a file-scope static function defined above.

    // Active/inactive state is managed by InactiveAwareTabBar::setBarActive().
    // No stylesheet needed — the custom paintEvent suppresses the selection
    // indicator entirely when the bar is inactive.

    // -----------------------------------------------------------------------
    // -----------------------------------------------------------------------
    // General tab  (top bar)
    // -----------------------------------------------------------------------
    {
        auto* w    = new QWidget;
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

        // Camera start offsets — shown when measureCameraOffsets() has run.
        {
            ConfigManager cfgOff;
            cfgOff.loadSettings();
            CameraProfile prof = cfgOff.getManifestProfile(m_sourcePath);
            if (!prof.cameraStartOffsets.empty()) {
                QStringList parts;
                for (const auto& [k, v] : prof.cameraStartOffsets) {
                    QString sign = (v >= 0) ? "+" : "";
                    parts << QString("%1: %2%3s")
                                 .arg(QString::fromStdString(k))
                                 .arg(sign)
                                 .arg(v, 0, 'f', 3);
                }
                form->addRow("Cam Offsets:", valueLabel(parts.join("   "), w));
            }
        }

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

        m_topTabBar->addTab("General");
        m_tabStack->addWidget(w);
    }

    // -----------------------------------------------------------------------
    // Per-camera tabs  (top bar)
    // -----------------------------------------------------------------------
    {
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
            m_topTabBar->addTab(tabName);
            m_tabStack->addWidget(makeCameraTab(trip, camSlot, nullptr));
        }
    }

    // Sync Values tab — top bar, rightmost
    m_syncTabIdx = m_tabStack->count();
    m_topTabBar->addTab("Sync Values");
    m_tabStack->addWidget(buildSyncWidget());


    // -----------------------------------------------------------------------
    // Outputs tab — GPS / Map / Dashboard / HUD in one scrolled page
    // -----------------------------------------------------------------------
    {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);

        auto* outer    = new QWidget;
        auto* outerLay = new QVBoxLayout(outer);
        outerLay->setContentsMargins(8, 8, 8, 8);
        outerLay->setSpacing(10);

        const QString sourceDir = QString::fromStdString(m_sourcePath);
        const QString tripId    = QString::fromStdString(trip.id);
        const QString ftid      = m_mid.empty() ? tripId.toUpper()
                                : QString::fromStdString(m_mid).toUpper() + "-" + tripId.toUpper();

        // ── GPS Track ────────────────────────────────────────────────────────
        {
            auto* grp  = new QGroupBox("GPS Track", outer);
            auto* vlay = new QVBoxLayout(grp);
            vlay->setContentsMargins(10, 6, 10, 10);
            vlay->setSpacing(6);

            auto* extRow = new QHBoxLayout;
            m_gpsStatusLabel = new QLabel(
                hasGps ? "✓ Extracted" : "✗ Not extracted", grp);
            m_gpsStatusLabel->setStyleSheet(
                hasGps ? "color: green; font-weight: bold;" : "color: #cc6600;");
            extRow->addWidget(m_gpsStatusLabel, 1);
            m_extractGpsBtn = new QPushButton("Extract GPS…", grp);
            connect(m_extractGpsBtn, &QPushButton::clicked,
                    this, &TripPropertiesDialog::onExtractGps);
            extRow->addWidget(m_extractGpsBtn);
            vlay->addLayout(extRow);

            m_extractMsgLabel = new QLabel(grp);
            m_extractMsgLabel->setStyleSheet("font-size: 8pt; color: gray;");
            m_extractMsgLabel->setWordWrap(true);
            vlay->addWidget(m_extractMsgLabel);

            auto* sep = new QFrame(grp);
            sep->setFrameShape(QFrame::HLine);
            sep->setFrameShadow(QFrame::Sunken);
            vlay->addWidget(sep);

            auto* exportForm = new QFormLayout;
            exportForm->setContentsMargins(0, 0, 0, 0);
            exportForm->setSpacing(4);
            m_exportFormatCombo = new QComboBox(grp);
            m_exportFormatCombo->addItems({"GPX", "KML", "GeoJSON"});
            exportForm->addRow("Export format:", m_exportFormatCombo);

            m_exportPathEdit  = new QLineEdit(grp);
            m_exportPathEdit->setText(sourceDir + "/clops_trip_" + ftid + "_track.gpx");
            auto* browseBtn = new QPushButton("…", grp);
            browseBtn->setFixedWidth(28);
            connect(browseBtn, &QPushButton::clicked,
                    this, &TripPropertiesDialog::onExportBrowse);
            auto* pathRow = new QHBoxLayout;
            pathRow->addWidget(m_exportPathEdit);
            pathRow->addWidget(browseBtn);
            exportForm->addRow("Output:", pathRow);

            m_exportGpsBtn = new QPushButton("Export", grp);
            m_exportGpsBtn->setEnabled(hasGps);
            exportForm->addRow("", m_exportGpsBtn);
            vlay->addLayout(exportForm);

            connect(m_exportFormatCombo,
                    QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, &TripPropertiesDialog::onExportFormatChanged);
            connect(m_exportGpsBtn, &QPushButton::clicked,
                    this, &TripPropertiesDialog::onExportGps);

            outerLay->addWidget(grp);
        }

        // ── Map Video ────────────────────────────────────────────────────────
        {
            auto* grp  = new QGroupBox("Map Video", outer);
            auto* vlay = new QVBoxLayout(grp);
            vlay->setContentsMargins(10, 6, 10, 10);
            vlay->setSpacing(6);

            m_mapWarnLabel = new QLabel(
                "GPS track not yet extracted — extract it first.", grp);
            m_mapWarnLabel->setWordWrap(true);
            m_mapWarnLabel->setStyleSheet("color: #cc6600;");
            m_mapWarnLabel->setVisible(!hasGps);
            vlay->addWidget(m_mapWarnLabel);

            auto* outRow = new QHBoxLayout;
            auto* outLbl = new QLabel("Output:", grp); outLbl->setFixedWidth(54);
            m_mapOutputEdit = new QLineEdit(grp);
            m_mapOutputEdit->setText(
                QString::fromStdString(m_sourcePath) + "/clops_trip_" + ftid + "_map.mp4");
            auto* mapBrowseBtn = new QPushButton("…", grp);
            mapBrowseBtn->setFixedWidth(28);
            connect(mapBrowseBtn, &QPushButton::clicked, [this]() {
                QString path = QFileDialog::getSaveFileName(
                    this, "Map Output", m_mapOutputEdit->text(), "Video (*.mp4)");
                if (!path.isEmpty()) m_mapOutputEdit->setText(path);
            });
            outRow->addWidget(outLbl);
            outRow->addWidget(m_mapOutputEdit);
            outRow->addWidget(mapBrowseBtn);
            vlay->addLayout(outRow);

            auto* resLbl = new QLabel(
                QString("Resolution: %1×%2  (trip video profile)")
                    .arg(trip.videoProfile.width).arg(trip.videoProfile.height), grp);
            resLbl->setStyleSheet("font-size: 8pt; color: gray;");
            vlay->addWidget(resLbl);

            m_mapFileList = new QListWidget(grp);
            m_mapFileList->setMaximumHeight(80);
            m_mapFileList->setAlternatingRowColors(true);
            connect(m_mapFileList, &QListWidget::itemDoubleClicked,
                    [](QListWidgetItem* item) {
                QDesktopServices::openUrl(
                    QUrl::fromLocalFile(item->data(Qt::UserRole).toString()));
            });
            vlay->addWidget(m_mapFileList);

            m_mapGenerateBtn = new QPushButton("Generate Map…", grp);
            m_mapGenerateBtn->setEnabled(hasGps);
            connect(m_mapGenerateBtn, &QPushButton::clicked,
                    this, &TripPropertiesDialog::onGenerateMap);
            vlay->addWidget(m_mapGenerateBtn);

            outerLay->addWidget(grp);
        }

        // ── Dashboard ────────────────────────────────────────────────────────
        {
            auto* grp  = new QGroupBox("Dashboard", outer);
            auto* vlay = new QVBoxLayout(grp);
            vlay->setContentsMargins(10, 6, 10, 10);
            vlay->setSpacing(6);

            m_dashWarnLabel = new QLabel(
                "GPS track not yet extracted — extract it first.", grp);
            m_dashWarnLabel->setWordWrap(true);
            m_dashWarnLabel->setStyleSheet("color: #cc6600;");
            m_dashWarnLabel->setVisible(!hasGps);
            vlay->addWidget(m_dashWarnLabel);

            auto* outRow = new QHBoxLayout;
            auto* outLbl = new QLabel("Output:", grp); outLbl->setFixedWidth(54);
            m_dashOutputEdit = new QLineEdit(grp);
            m_dashOutputEdit->setText(
                QString::fromStdString(m_sourcePath) + "/clops_trip_" + ftid + "_dash.mp4");
            auto* dashBrowseBtn = new QPushButton("…", grp);
            dashBrowseBtn->setFixedWidth(28);
            connect(dashBrowseBtn, &QPushButton::clicked, [this]() {
                bool transp = m_dashTransparentCheck && m_dashTransparentCheck->isChecked();
                QString filter = transp ? "Video (*.webm)" : "Video (*.mp4)";
                QString path = QFileDialog::getSaveFileName(
                    this, "Dashboard Output", m_dashOutputEdit->text(),
                    filter + ";;All Files (*)");
                if (!path.isEmpty()) m_dashOutputEdit->setText(path);
            });
            outRow->addWidget(outLbl);
            outRow->addWidget(m_dashOutputEdit);
            outRow->addWidget(dashBrowseBtn);
            vlay->addLayout(outRow);

            m_dashTransparentCheck = new QCheckBox(
                "Transparent background  (WebM/VP9, for overlay use)", grp);
            connect(m_dashTransparentCheck, &QCheckBox::toggled, this, [this](bool checked) {
                if (!m_dashOutputEdit) return;
                QString path = m_dashOutputEdit->text();
                if (checked && path.endsWith(".mp4", Qt::CaseInsensitive))
                    m_dashOutputEdit->setText(path.chopped(4) + ".webm");
                else if (!checked && path.endsWith(".webm", Qt::CaseInsensitive))
                    m_dashOutputEdit->setText(path.chopped(5) + ".mp4");
            });
            vlay->addWidget(m_dashTransparentCheck);

            {
                auto* layoutRow = new QHBoxLayout;
                auto* layoutLbl = new QLabel("Layout:", grp); layoutLbl->setFixedWidth(54);
                m_dashLayoutCombo = new QComboBox(grp);
                m_dashLayoutCombo->addItem("Standard (3-panel)", QString("standard"));
                m_dashLayoutCombo->addItem("Quadrant HUD",       QString("quadrant-hud"));
                m_dashLayoutCombo->addItem("Custom JSON…",  QString("custom"));
                layoutRow->addWidget(layoutLbl);
                layoutRow->addWidget(m_dashLayoutCombo, 1);
                vlay->addLayout(layoutRow);

                auto* customRowW = new QWidget(grp);
                auto* customRowL = new QHBoxLayout(customRowW);
                customRowL->setContentsMargins(54, 0, 0, 0);
                m_dashLayoutPath = new QLineEdit(customRowW);
                m_dashLayoutPath->setPlaceholderText("layout.json…");
                auto* customBrowseBtn = new QPushButton("…", customRowW);
                customBrowseBtn->setFixedWidth(28);
                connect(customBrowseBtn, &QPushButton::clicked, [this]() {
                    QString path = QFileDialog::getOpenFileName(
                        this, "Layout JSON", m_dashLayoutPath->text(),
                        "JSON (*.json);;All Files (*)");
                    if (!path.isEmpty()) m_dashLayoutPath->setText(path);
                });
                customRowL->addWidget(m_dashLayoutPath, 1);
                customRowL->addWidget(customBrowseBtn);
                m_dashLayoutRow = customRowW;
                m_dashLayoutRow->setVisible(false);
                vlay->addWidget(m_dashLayoutRow);

                connect(m_dashLayoutCombo, &QComboBox::currentIndexChanged,
                        this, [this](int) {
                    if (m_dashLayoutRow)
                        m_dashLayoutRow->setVisible(
                            m_dashLayoutCombo->currentData().toString() == "custom");
                });
            }

            auto* infoLbl = new QLabel(
                "Resolution: 960×540  (collage quadrant slot)", grp);
            infoLbl->setStyleSheet("font-size: 8pt; color: gray;");
            vlay->addWidget(infoLbl);

            m_dashFileList = new QListWidget(grp);
            m_dashFileList->setMaximumHeight(80);
            m_dashFileList->setAlternatingRowColors(true);
            connect(m_dashFileList, &QListWidget::itemDoubleClicked,
                    [](QListWidgetItem* item) {
                QDesktopServices::openUrl(
                    QUrl::fromLocalFile(item->data(Qt::UserRole).toString()));
            });
            vlay->addWidget(m_dashFileList);

            m_dashGenerateBtn = new QPushButton("Generate Dashboard…", grp);
            connect(m_dashGenerateBtn, &QPushButton::clicked,
                    this, &TripPropertiesDialog::onGenerateDashboard);
            vlay->addWidget(m_dashGenerateBtn);

            outerLay->addWidget(grp);
        }

        // ── HUD Overlay ──────────────────────────────────────────────────────
        {
            auto* grp  = new QGroupBox("HUD Overlay", outer);
            auto* vlay = new QVBoxLayout(grp);
            vlay->setContentsMargins(10, 6, 10, 10);
            vlay->setSpacing(6);

            m_hudWarnLabel = new QLabel(
                "GPS track not yet extracted — extract it first.", grp);
            m_hudWarnLabel->setWordWrap(true);
            m_hudWarnLabel->setStyleSheet("color: #cc6600;");
            m_hudWarnLabel->setVisible(!hasGps);
            vlay->addWidget(m_hudWarnLabel);

            {
                auto* row = new QHBoxLayout;
                auto* lbl = new QLabel("Output:", grp); lbl->setFixedWidth(54);
                m_hudOutputEdit = new QLineEdit(grp);
                m_hudOutputEdit->setText(
                    QString::fromStdString(m_sourcePath) + "/clops_trip_" + ftid + "_hud.webm");
                auto* btn = new QPushButton("…", grp); btn->setFixedWidth(28);
                connect(btn, &QPushButton::clicked, [this]() {
                    QString path = QFileDialog::getSaveFileName(
                        this, "HUD Output", m_hudOutputEdit->text(),
                        "Video (*.webm);;All Files (*)");
                    if (!path.isEmpty()) m_hudOutputEdit->setText(path);
                });
                row->addWidget(lbl); row->addWidget(m_hudOutputEdit); row->addWidget(btn);
                vlay->addLayout(row);
            }

            {
                auto* row = new QHBoxLayout;
                row->addWidget(new QLabel("Render:", grp));
                row->addWidget(new QLabel("W", grp));
                m_hudWidth = new QSpinBox(grp);
                m_hudWidth->setRange(640, 7680); m_hudWidth->setSingleStep(16);
                m_hudWidth->setValue(3840);
                row->addWidget(m_hudWidth);
                row->addWidget(new QLabel("H", grp));
                m_hudHeight = new QSpinBox(grp);
                m_hudHeight->setRange(480, 4320); m_hudHeight->setSingleStep(16);
                m_hudHeight->setValue(2160);
                row->addWidget(m_hudHeight);
                row->addWidget(new QLabel("px  (match collage resolution)", grp));
                row->addStretch();
                vlay->addLayout(row);
            }

            {
                ConfigManager cfg_seed; cfg_seed.loadSettings();
                auto* sg   = new QGroupBox("Style", grp);
                auto* glay = new QFormLayout(sg);
                glay->setContentsMargins(8, 4, 8, 8); glay->setSpacing(6);
                m_hudFontScale = new QDoubleSpinBox(sg);
                m_hudFontScale->setRange(0.25, 4.0); m_hudFontScale->setSingleStep(0.25);
                m_hudFontScale->setDecimals(2); m_hudFontScale->setSuffix("×");
                m_hudFontScale->setValue(cfg_seed.getHudFontScale());
                glay->addRow("Font scale:", m_hudFontScale);
                m_hudLineScale = new QDoubleSpinBox(sg);
                m_hudLineScale->setRange(0.25, 4.0); m_hudLineScale->setSingleStep(0.25);
                m_hudLineScale->setDecimals(2); m_hudLineScale->setSuffix("×");
                m_hudLineScale->setValue(cfg_seed.getHudLineScale());
                glay->addRow("Line scale:", m_hudLineScale);
                m_hudColorHex = new QLineEdit(sg);
                m_hudColorHex->setMaxLength(7);
                m_hudColorHex->setPlaceholderText("#00ff41");
                m_hudColorHex->setText(QString::fromStdString(cfg_seed.getHudColor()));
                glay->addRow("Color (hex):", m_hudColorHex);
                vlay->addWidget(sg);
            }

            {
                auto* sg   = new QGroupBox("Speed Tapes", grp);
                auto* glay = new QVBoxLayout(sg);
                glay->setContentsMargins(8, 4, 8, 8); glay->setSpacing(6);
                auto* globalRow = new QHBoxLayout;
                globalRow->addWidget(new QLabel("Tape width:", sg));
                m_hudTapeWidth = new QSpinBox(sg);
                m_hudTapeWidth->setRange(0, 800); m_hudTapeWidth->setValue(0);
                m_hudTapeWidth->setSpecialValueText("auto");
                globalRow->addWidget(m_hudTapeWidth);
                globalRow->addWidget(new QLabel("px", sg));
                globalRow->addSpacing(16);
                globalRow->addWidget(new QLabel("Visible range:", sg));
                m_hudVisibleRange = new QSpinBox(sg);
                m_hudVisibleRange->setRange(20, 200); m_hudVisibleRange->setSingleStep(10);
                m_hudVisibleRange->setValue(100); m_hudVisibleRange->setSuffix(" units");
                globalRow->addWidget(m_hudVisibleRange);
                globalRow->addStretch();
                glay->addLayout(globalRow);
                auto addPosRow = [&](const QString& label, QSpinBox*& spX, QSpinBox*& spY,
                                     int defX, int defY) {
                    auto* row = new QHBoxLayout;
                    row->addWidget(new QLabel(label, sg));
                    row->addWidget(new QLabel("X:", sg));
                    spX = new QSpinBox(sg); spX->setRange(-1, 7680); spX->setValue(defX);
                    spX->setSpecialValueText("auto");
                    row->addWidget(spX);
                    row->addWidget(new QLabel("Y:", sg));
                    spY = new QSpinBox(sg); spY->setRange(-1, 4320); spY->setValue(defY);
                    spY->setSpecialValueText("auto");
                    row->addWidget(spY);
                    row->addWidget(new QLabel("px  (-1 = auto)", sg));
                    row->addStretch();
                    glay->addLayout(row);
                };
                addPosRow("Left tape (KPH):",  m_hudLsX, m_hudLsY, 0,  -1);
                addPosRow("Right tape (MPH):", m_hudRsX, m_hudRsY, -1, -1);
                vlay->addWidget(sg);
            }

            {
                auto* sg   = new QGroupBox("Compass Rose", grp);
                auto* glay = new QVBoxLayout(sg);
                glay->setContentsMargins(8, 4, 8, 8); glay->setSpacing(6);
                auto* roseNote = new QLabel(
                    "Auto = H\xc3\xb7" "8 (270 px at 4K). Rose is bottom-centered; "
                    "Crop sets how much of the ring sits below the frame edge.", sg);
                roseNote->setWordWrap(true);
                roseNote->setStyleSheet("font-size: 8pt; color: gray;");
                glay->addWidget(roseNote);
                auto* rRow = new QHBoxLayout;
                rRow->addWidget(new QLabel("Radius:", sg));
                m_hudCompassRadius = new QSpinBox(sg);
                m_hudCompassRadius->setRange(0, 1000); m_hudCompassRadius->setValue(0);
                m_hudCompassRadius->setSpecialValueText("auto");
                m_hudCompassRadius->setSingleStep(10);
                connect(m_hudCompassRadius, QOverload<int>::of(&QSpinBox::valueChanged),
                        sg, [this](int v) {
                    if (v > 0 && v < 100) {
                        QSignalBlocker sb(m_hudCompassRadius);
                        m_hudCompassRadius->setValue(100);
                    }
                });
                rRow->addWidget(m_hudCompassRadius);
                rRow->addWidget(new QLabel("px", sg));
                rRow->addStretch();
                glay->addLayout(rRow);
                auto* cropRow = new QHBoxLayout;
                cropRow->addWidget(new QLabel("Crop:", sg));
                m_hudCompassCrop = new QSpinBox(sg);
                m_hudCompassCrop->setRange(0, 50); m_hudCompassCrop->setValue(20);
                m_hudCompassCrop->setSingleStep(5); m_hudCompassCrop->setSuffix("%");
                cropRow->addWidget(m_hudCompassCrop);
                cropRow->addStretch();
                glay->addLayout(cropRow);
                auto* cRow = new QHBoxLayout;
                cRow->addWidget(new QLabel("Center X:", sg));
                m_hudCompassX = new QSpinBox(sg);
                m_hudCompassX->setRange(-1, 7680); m_hudCompassX->setValue(-1);
                m_hudCompassX->setSpecialValueText("auto");
                cRow->addWidget(m_hudCompassX);
                cRow->addWidget(new QLabel("Y:", sg));
                m_hudCompassY = new QSpinBox(sg);
                m_hudCompassY->setRange(-1, 4320); m_hudCompassY->setValue(-1);
                m_hudCompassY->setSpecialValueText("auto");
                cRow->addWidget(m_hudCompassY);
                cRow->addWidget(new QLabel("px  (-1 = bottom-center)", sg));
                cRow->addStretch();
                glay->addLayout(cRow);
                vlay->addWidget(sg);
            }

            auto* hudFileLbl = new QLabel(
                "Generated HUD files (double-click to open):", grp);
            hudFileLbl->setStyleSheet("font-size: 8pt; color: gray;");
            vlay->addWidget(hudFileLbl);

            m_hudFileList = new QListWidget(grp);
            m_hudFileList->setMaximumHeight(80);
            m_hudFileList->setAlternatingRowColors(true);
            connect(m_hudFileList, &QListWidget::itemDoubleClicked,
                    [](QListWidgetItem* item) {
                QDesktopServices::openUrl(
                    QUrl::fromLocalFile(item->data(Qt::UserRole).toString()));
            });
            vlay->addWidget(m_hudFileList);

            m_hudGenerateBtn = new QPushButton("Generate HUD…", grp);
            connect(m_hudGenerateBtn, &QPushButton::clicked,
                    this, &TripPropertiesDialog::onGenerateHud);
            vlay->addWidget(m_hudGenerateBtn);

            outerLay->addWidget(grp);
        }

        // ── Queue All Pending ─────────────────────────────────────────────────
        m_buildAllBtn = new QPushButton("Queue All Pending…", outer);
        connect(m_buildAllBtn, &QPushButton::clicked,
                this, &TripPropertiesDialog::onBuildAll);
        outerLay->addWidget(m_buildAllBtn);
        outerLay->addStretch();

        scroll->setWidget(outer);
        m_topTabBar->addTab("Outputs");
        m_tabStack->addWidget(scroll);
    }

    // Populate video lists from manifest data.
    populateVideoList(m_mapFileList,  m_trip.mapVideos);
    populateVideoList(m_dashFileList, m_trip.dashVideos);
    populateVideoList(m_hudFileList,  m_trip.hudVideos);

    // Single tab bar — direct index mapping to stacked widget.
    connect(m_topTabBar, &QTabBar::currentChanged,
            m_tabStack, &QStackedWidget::setCurrentIndex);
    m_topTabBar->setCurrentIndex(0);

    vbox->addWidget(m_topTabBar);
    vbox->addWidget(m_tabStack, 1);

    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &TripPropertiesDialog::onAccepted);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    vbox->addWidget(m_buttonBox);
}


// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
std::string TripPropertiesDialog::updatedNote() const
{
    return m_noteEdit ? m_noteEdit->text().trimmed().toStdString() : m_trip.note;
}

// ---------------------------------------------------------------------------
// Close / reject — GPS extraction runs in the job queue; dialog closes freely.
// ---------------------------------------------------------------------------
void TripPropertiesDialog::reject()   { QDialog::reject(); }
void TripPropertiesDialog::closeEvent(QCloseEvent* event) { QDialog::closeEvent(event); }

// ---------------------------------------------------------------------------
// GPS — extract (submit to job queue)
// ---------------------------------------------------------------------------
void TripPropertiesDialog::onExtractGps()
{
    ConfigManager config;
    config.loadSettings();
    std::string manifestFile = config.lookupManifestFilePath(m_sourcePath);
    if (manifestFile.empty()) {
        if (m_extractMsgLabel) m_extractMsgLabel->setText(
            "Error: manifest file not found. Try rescanning this source directory.");
        return;
    }

    m_extractGpsBtn->setEnabled(false);
    if (m_extractMsgLabel) m_extractMsgLabel->setText("Queued for extraction...");

    auto* job = new GpsExtractJob(m_mid, m_trip.id, manifestFile, config.getExiftoolPath());
    connect(job, &Job::statusChanged, this, [this](const QString& msg) {
        if (m_extractMsgLabel) m_extractMsgLabel->setText(msg);
    });
    connect(job, &Job::finished, this, [this](bool ok) {
        m_extractGpsBtn->setEnabled(true);
        if (ok) {
            m_trip.gpsTrackStatus = "complete";
            if (m_gpsStatusLabel) {
                m_gpsStatusLabel->setText("Status: complete");
                m_gpsStatusLabel->setStyleSheet("color: green; font-weight: bold;");
            }
            if (m_exportGpsBtn)   m_exportGpsBtn->setEnabled(true);
            if (m_mapGenerateBtn) m_mapGenerateBtn->setEnabled(true);
            if (m_mapWarnLabel)   m_mapWarnLabel->hide();
            if (m_dashWarnLabel)  m_dashWarnLabel->hide();
            if (m_hudWarnLabel)   m_hudWarnLabel->hide();
            if (m_extractMsgLabel) m_extractMsgLabel->setText("Extraction complete.");
            emit gpsExtracted();
            onRunSyncAnalysis();
        } else {
            if (m_extractMsgLabel)
                m_extractMsgLabel->setStyleSheet("font-size: 8pt; color: red;");
        }
    });
    JobQueue::instance().enqueue(job);
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
    if      (fmt == 0) result = CamClops::writeGpx    (root, tripIdx, outPath);
    else if (fmt == 1) result = CamClops::writeKml    (root, tripIdx, outPath);
    else               result = CamClops::writeGeoJson(root, tripIdx, outPath);

    if (result.empty()) {
        m_exportGpsBtn->setText("Export \u2717");
        m_exportGpsBtn->setStyleSheet("color: red;");
    } else {
        m_exportGpsBtn->setText("Exported \u2713");
        m_exportGpsBtn->setStyleSheet("color: green;");
    }
}

// ---------------------------------------------------------------------------
// Build All — consecutive map → dashboard → HUD using each tab's settings.
// ---------------------------------------------------------------------------
void TripPropertiesDialog::onBuildAll()
{
    ConfigManager config;
    config.loadSettings();
    std::string manifestFile = config.lookupManifestFilePath(m_sourcePath);
    if (manifestFile.empty()) return;
    QString mf       = QString::fromStdString(manifestFile);
    QString tripQId  = QString::fromStdString(m_trip.id);
    QString tripAddr = m_mid.empty() ? tripQId
                                     : QString::fromStdString(m_mid + ":" + m_trip.id);

    // GPS Extract — always first; renders depend on the track it produces.
    {
        auto* job = new GpsExtractJob(m_mid, m_trip.id, manifestFile, config.getExiftoolPath());
        connect(job, &Job::statusChanged, this, [this](const QString& msg) {
            if (m_extractMsgLabel) m_extractMsgLabel->setText(msg);
        });
        connect(job, &Job::finished, this, [this](bool ok) {
            if (!ok) return;
            m_trip.gpsTrackStatus = "complete";
            if (m_gpsStatusLabel) {
                m_gpsStatusLabel->setText("Status: complete");
                m_gpsStatusLabel->setStyleSheet("color: green; font-weight: bold;");
            }
            if (m_exportGpsBtn)    m_exportGpsBtn->setEnabled(true);
            if (m_mapGenerateBtn)  m_mapGenerateBtn->setEnabled(true);
            if (m_mapWarnLabel)    m_mapWarnLabel->hide();
            if (m_dashWarnLabel)   m_dashWarnLabel->hide();
            if (m_hudWarnLabel)    m_hudWarnLabel->hide();
            if (m_extractMsgLabel) m_extractMsgLabel->setText("Extraction complete.");
            emit gpsExtracted();
            onRunSyncAnalysis();
        });
        JobQueue::instance().enqueue(job);
    }

    // Map
    {
        QString output = m_mapOutputEdit ? m_mapOutputEdit->text().trimmed()
                                         : QString::fromStdString(m_sourcePath)
                             + "/clops_trip_" + tripQId + "_map.mp4";
        QStringList extraArgs;
        {
            QString fp = QString::fromStdString(config.getSettings().ffmpegPath);
            if (!fp.isEmpty()) extraArgs << "--ffmpeg" << fp;
        }
        auto* job = new MapRenderJob(
            "Map — " + tripAddr, "clops_maprender.py",
            mf, tripQId, output,
            m_trip.videoProfile.width, m_trip.videoProfile.height,
            extraArgs);
        connect(job, &Job::finished, this, [this, output](bool ok) {
            if (ok) appendVideoToManifest(output, "mapVideos", m_trip.mapVideos);
        });
        JobQueue::instance().enqueue(job);
    }

    // Dashboard
    {
        QString output = m_dashOutputEdit ? m_dashOutputEdit->text().trimmed()
                                          : QString::fromStdString(m_sourcePath)
                              + "/clops_trip_" + tripQId + "_dash.mp4";
        QString units = config.getUseImperial() ? "imperial" : "metric";
        bool transparent = m_dashTransparentCheck && m_dashTransparentCheck->isChecked();
        QStringList extraArgs{"--units", units};
        if (transparent) extraArgs << "--transparent";
        if (m_dashLayoutCombo) {
            QString lv = m_dashLayoutCombo->currentData().toString();
            if (lv == "custom") {
                QString lp = m_dashLayoutPath ? m_dashLayoutPath->text().trimmed() : "";
                if (!lp.isEmpty()) extraArgs << "--layout" << lp;
            } else if (lv != "standard") {
                extraArgs << "--layout" << lv;
            }
        }
        {
            QString fp = QString::fromStdString(config.getSettings().ffmpegPath);
            if (!fp.isEmpty()) extraArgs << "--ffmpeg" << fp;
        }
        auto* job = new MapRenderJob(
            "Dashboard — " + tripAddr, "clops_dashboard.py",
            mf, tripQId, output, 960, 540, extraArgs);
        connect(job, &Job::finished, this, [this, output](bool ok) {
            if (ok) appendVideoToManifest(output, "dashVideos", m_trip.dashVideos);
        });
        JobQueue::instance().enqueue(job);
    }

    // HUD
    {
        QString output = m_hudOutputEdit ? m_hudOutputEdit->text().trimmed()
                                         : QString::fromStdString(m_sourcePath)
                             + "/clops_trip_" + tripQId + "_hud.webm";
        if (!output.endsWith(".webm", Qt::CaseInsensitive))
            output += ".webm";
        int W = m_hudWidth  ? m_hudWidth->value()  : 3840;
        int H = m_hudHeight ? m_hudHeight->value() : 2160;
        QString hexColor = m_hudColorHex ? m_hudColorHex->text().trimmed() : "#00ff41";
        if (hexColor.isEmpty() || !hexColor.startsWith('#')) hexColor = "#00ff41";
        double fontSc = m_hudFontScale ? m_hudFontScale->value() : 1.0;
        double lineSc = m_hudLineScale ? m_hudLineScale->value() : 1.0;
        QStringList extraArgs{
            "--color-hex",  hexColor,
            "--font-scale", QString::number(fontSc, 'f', 2),
            "--line-scale", QString::number(lineSc, 'f', 2),
        };
        if (m_hudTapeWidth && m_hudTapeWidth->value() > 0)
            extraArgs << "--tape-width" << QString::number(m_hudTapeWidth->value());
        extraArgs << "--visible-range"
                  << QString::number(m_hudVisibleRange ? m_hudVisibleRange->value() : 100);
        auto addNeg = [&](const QString& flag, QSpinBox* sp) {
            if (sp && sp->value() >= 0) extraArgs << flag << QString::number(sp->value());
        };
        addNeg("--left-speed-x",  m_hudLsX);
        addNeg("--left-speed-y",  m_hudLsY);
        addNeg("--right-speed-x", m_hudRsX);
        addNeg("--right-speed-y", m_hudRsY);
        if (m_hudCompassRadius && m_hudCompassRadius->value() > 0)
            extraArgs << "--heading-height" << QString::number(m_hudCompassRadius->value());
        if (m_hudCompassCrop && m_hudCompassCrop->value() != 20)
            extraArgs << "--heading-crop" << QString::number(m_hudCompassCrop->value());
        addNeg("--heading-x", m_hudCompassX);
        addNeg("--heading-y", m_hudCompassY);
        {
            QString fp = QString::fromStdString(config.getSettings().ffmpegPath);
            if (!fp.isEmpty()) extraArgs << "--ffmpeg" << fp;
        }
        auto* job = new MapRenderJob(
            "HUD — " + tripAddr, "clops_hud.py",
            mf, tripQId, output, W, H, extraArgs);
        connect(job, &Job::finished, this, [this, output](bool ok) {
            if (ok) appendVideoToManifest(output, "hudVideos", m_trip.hudVideos);
        });
        JobQueue::instance().enqueue(job);
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

    QStringList extraArgs;
    {
        QString fp = QString::fromStdString(config.getSettings().ffmpegPath);
        if (!fp.isEmpty()) extraArgs << "--ffmpeg" << fp;
    }
    QString mapAddr = m_mid.empty()
        ? QString::fromStdString(m_trip.id)
        : QString::fromStdString(m_mid + ":" + m_trip.id);
    auto* job = new MapRenderJob(
        "Generating Map — " + mapAddr,
        "clops_maprender.py",
        QString::fromStdString(manifestFile),
        QString::fromStdString(m_trip.id),
        output,
        m_trip.videoProfile.width, m_trip.videoProfile.height,
        extraArgs);
    connect(job, &Job::finished, this, [this, output](bool ok) {
        if (ok) appendVideoToManifest(output, "mapVideos", m_trip.mapVideos);
    });
    JobQueue::instance().enqueue(job);
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
    bool transparent = m_dashTransparentCheck && m_dashTransparentCheck->isChecked();
    QStringList extraArgs{"--units", units};
    if (transparent) extraArgs << "--transparent";
    if (m_dashLayoutCombo) {
        QString lv = m_dashLayoutCombo->currentData().toString();
        if (lv == "custom") {
            QString lp = m_dashLayoutPath ? m_dashLayoutPath->text().trimmed() : "";
            if (!lp.isEmpty()) extraArgs << "--layout" << lp;
        } else if (lv != "standard") {
            extraArgs << "--layout" << lv;
        }
    }
    {
        QString fp = QString::fromStdString(config.getSettings().ffmpegPath);
        if (!fp.isEmpty()) extraArgs << "--ffmpeg" << fp;
    }
    QString dashAddr = m_mid.empty() ? QString::fromStdString(m_trip.id)
                                     : QString::fromStdString(m_mid + ":" + m_trip.id);
    auto* job = new MapRenderJob(
        "Generating Dashboard — " + dashAddr,
        "clops_dashboard.py",
        QString::fromStdString(manifestFile),
        QString::fromStdString(m_trip.id),
        output, 960, 540,
        extraArgs);
    connect(job, &Job::finished, this, [this, output](bool ok) {
        if (ok) appendVideoToManifest(output, "dashVideos", m_trip.dashVideos);
    });
    JobQueue::instance().enqueue(job);
}

// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------
void TripPropertiesDialog::onGenerateHud()
{
    if (m_trip.gpsTrackStatus != "complete") {
        QMessageBox::information(
            this,
            "GPS Track Required",
            "This trip does not have a GPS track in the manifest yet.\n\n"
            "Go to the GPS tab, click 'Extract GPS Track', then return\n"
            "to the HUD tab and generate the HUD.");
        return;
    }

    QString output = m_hudOutputEdit ? m_hudOutputEdit->text().trimmed() : QString();
    if (output.isEmpty()) return;
    if (!output.endsWith(".webm", Qt::CaseInsensitive))
        output += ".webm";

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
        if (m_hudWarnLabel) {
            m_hudWarnLabel->setText("Error: manifest file not found. Try rescanning.");
            m_hudWarnLabel->setVisible(true);
        }
        return;
    }

    int     W        = m_hudWidth  ? m_hudWidth->value()  : 3840;
    int     H        = m_hudHeight ? m_hudHeight->value() : 2160;
    QString hexColor = m_hudColorHex ? m_hudColorHex->text().trimmed() : "#00ff41";
    if (hexColor.isEmpty() || !hexColor.startsWith('#')) hexColor = "#00ff41";
    double  fontSc   = m_hudFontScale ? m_hudFontScale->value() : 1.0;
    double  lineSc   = m_hudLineScale ? m_hudLineScale->value() : 1.0;

    QStringList extraArgs{
        "--color-hex",    hexColor,
        "--font-scale",   QString::number(fontSc, 'f', 2),
        "--line-scale",   QString::number(lineSc, 'f', 2),
    };

    // Tape width (global for both tapes; 0 = omit → script uses auto)
    if (m_hudTapeWidth && m_hudTapeWidth->value() > 0)
        extraArgs << "--tape-width" << QString::number(m_hudTapeWidth->value());

    // Always pass visible-range so the GUI value is authoritative
    extraArgs << "--visible-range"
              << QString::number(m_hudVisibleRange ? m_hudVisibleRange->value() : 100);

    // Speed tape positions (-1 = auto, only pass when explicitly set)
    auto addNeg = [&](const QString& flag, QSpinBox* sp) {
        if (sp && sp->value() >= 0) extraArgs << flag << QString::number(sp->value());
    };
    addNeg("--left-speed-x",  m_hudLsX);
    addNeg("--left-speed-y",  m_hudLsY);
    addNeg("--right-speed-x", m_hudRsX);
    addNeg("--right-speed-y", m_hudRsY);

    // Compass rose
    if (m_hudCompassRadius && m_hudCompassRadius->value() > 0)
        extraArgs << "--heading-height" << QString::number(m_hudCompassRadius->value());
    if (m_hudCompassCrop && m_hudCompassCrop->value() != 20)
        extraArgs << "--heading-crop" << QString::number(m_hudCompassCrop->value());
    addNeg("--heading-x", m_hudCompassX);
    addNeg("--heading-y", m_hudCompassY);

    {
        QString fp = QString::fromStdString(config.getSettings().ffmpegPath);
        if (!fp.isEmpty()) extraArgs << "--ffmpeg" << fp;
    }
    QString hudAddr = m_mid.empty() ? QString::fromStdString(m_trip.id)
                                    : QString::fromStdString(m_mid + ":" + m_trip.id);
    auto* job = new MapRenderJob(
        "Generating HUD — " + hudAddr,
        "clops_hud.py",
        QString::fromStdString(manifestFile),
        QString::fromStdString(m_trip.id),
        output, W, H,
        extraArgs);
    connect(job, &Job::finished, this, [this, output](bool ok) {
        if (ok) appendVideoToManifest(output, "hudVideos", m_trip.hudVideos);
    });
    JobQueue::instance().enqueue(job);
}

// ---------------------------------------------------------------------------
// Accept — save note
// ---------------------------------------------------------------------------
void TripPropertiesDialog::onAccepted()
{
    std::string newNote = updatedNote();
    if (newNote != m_trip.note && !m_sourcePath.empty()) {
        CamClops::ConfigManager config;
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
// ---------------------------------------------------------------------------
// Sync Values tab helpers
// ---------------------------------------------------------------------------

static QString findSyncScript() {
    const QString name = "clops_sync_analyze.py";
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString& c : {
        appDir + "/scripts/" + name,
        appDir + "/../scripts/" + name,
        appDir + "/../share/camclops/scripts/" + name,
        appDir + "/" + name,
    }) {
        if (QFileInfo::exists(c)) return QFileInfo(c).canonicalFilePath();
    }
    return {};
}

QWidget* TripPropertiesDialog::buildSyncWidget() {
    auto* w    = new QWidget;
    auto* vlay = new QVBoxLayout(w);
    vlay->setContentsMargins(12, 12, 12, 12);
    vlay->setSpacing(10);

    if (m_trip.cameraSync.valid) {
        // Summary
        auto* grp  = new QGroupBox("Sync Analysis");
        auto* form = new QFormLayout(grp);
        form->addRow("Analyzed:",
            new QLabel(QString::fromStdString(m_trip.cameraSync.analyzedAt)));
        form->addRow("Span / variation:",
            new QLabel(QString("%1f  /  %2f")
                .arg(double(m_trip.cameraSync.spanFrames),    0, 'f', 1)
                .arg(double(m_trip.cameraSync.spanVariation), 0, 'f', 1)));
        form->addRow("Segments analyzed:",
            new QLabel(QString::number(m_trip.cameraSync.segmentTrims.size())));
        vlay->addWidget(grp);

        // Per-segment table
        const auto& trims = m_trip.cameraSync.segmentTrims;
        if (!trims.empty()) {
            auto* tblGrp  = new QGroupBox("Per-segment leading trims (seconds)");
            auto* tblVlay = new QVBoxLayout(tblGrp);

            QStringList camCols;
            for (const auto& [cam, _] : trims.begin()->second)
                camCols << QString::fromStdString(cam);
            std::sort(camCols.begin(), camCols.end());

            auto* tbl = new QTableWidget(int(trims.size()), 1 + camCols.size());
            tbl->setHorizontalHeaderItem(0, new QTableWidgetItem("Segment"));
            for (int c = 0; c < int(camCols.size()); ++c)
                tbl->setHorizontalHeaderItem(c + 1, new QTableWidgetItem(camCols[c]));
            tbl->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
            tbl->setEditTriggers(QAbstractItemView::NoEditTriggers);
            tbl->setAlternatingRowColors(true);
            tbl->verticalHeader()->setVisible(false);

            int row = 0;
            for (const auto& [key, camMap] : trims) {
                auto* segItem = new QTableWidgetItem(QString::fromStdString(key));
                segItem->setTextAlignment(Qt::AlignCenter);
                tbl->setItem(row, 0, segItem);
                for (int c = 0; c < int(camCols.size()); ++c) {
                    auto it  = camMap.find(camCols[c].toStdString());
                    double v = (it != camMap.end()) ? double(it->second) : 0.0;
                    auto* item = new QTableWidgetItem(QString("%1").arg(v, 0, 'f', 3));
                    item->setTextAlignment(Qt::AlignCenter);
                    tbl->setItem(row, c + 1, item);
                }
                ++row;
            }
            tblVlay->addWidget(tbl);
            vlay->addWidget(tblGrp);
        }

        m_syncRunBtn = new QPushButton("Re-analyze");
    } else {
        auto* lbl = new QLabel(
            "No sync analysis has been run for this trip.\n\n"
            "Click Run Analysis to measure per-camera start offsets using\n"
            "audio cross-correlation (~1–2 seconds per segment).");
        lbl->setWordWrap(true);
        vlay->addWidget(lbl);
        m_syncRunBtn = new QPushButton("Run Analysis");
    }

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(m_syncRunBtn);
    btnRow->addStretch();
    vlay->addLayout(btnRow);

    m_syncOutput = new QPlainTextEdit;
    m_syncOutput->setReadOnly(true);
    m_syncOutput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_syncOutput->setVisible(false);
    QFont mono("Monospace");
    mono.setPointSize(8);
    m_syncOutput->setFont(mono);

    vlay->addWidget(m_syncOutput, 1);

    connect(m_syncRunBtn, &QPushButton::clicked,
            this, &TripPropertiesDialog::onRunSyncAnalysis);
    return w;
}

void TripPropertiesDialog::refreshSyncTab() {
    if (m_syncTabIdx < 0) return;
    // Reset pointers — they belong to the old widget which is about to be deleted
    m_syncRunBtn  = nullptr;
    m_syncOutput  = nullptr;
    auto* old = m_tabStack->widget(m_syncTabIdx);
    auto* fresh = buildSyncWidget();
    m_tabStack->removeWidget(old);
    old->deleteLater();
    m_tabStack->insertWidget(m_syncTabIdx, fresh);
    m_tabStack->setCurrentIndex(m_syncTabIdx);
}

void TripPropertiesDialog::onRunSyncAnalysis() {
    if (!m_syncRunBtn || !m_syncOutput)
        return;   // sync tab not built for this camera profile
    if (m_syncProcess && m_syncProcess->state() != QProcess::NotRunning)
        return;

    QString scriptPath = findSyncScript();
    if (scriptPath.isEmpty()) {
        m_syncOutput->setVisible(true);
        m_syncOutput->appendPlainText("Error: clops_sync_analyze.py not found.");
        return;
    }

    // Look up manifest ID from source path
    ConfigManager cfg;
    cfg.loadSettings();
    auto index = cfg.loadManifestIndex();
    std::string mid;
    for (const auto& e : index) {
        if (e.path == m_sourcePath) { mid = e.id; break; }
    }
    if (mid.empty()) {
        m_syncOutput->setVisible(true);
        m_syncOutput->appendPlainText("Error: manifest not found for this source path.");
        return;
    }

    QString tripAddr = QString::fromStdString(mid + ":" + m_trip.id);
    m_syncRunBtn->setEnabled(false);
    m_syncRunBtn->setText("Analyzing…");
    m_syncOutput->setVisible(true);
    m_syncOutput->clear();
    m_syncOutput->appendPlainText("clops_sync_analyze.py --all-segments --write " + tripAddr + "\n");

    ConfigManager syncCfg;
    syncCfg.loadSettings();
    const auto& syncSettings = syncCfg.getSettings();
    QStringList syncArgs = {scriptPath, "--all-segments", "--write", tripAddr};
    if (!syncSettings.ffmpegPath.empty())
        syncArgs << "--ffmpeg" << QString::fromStdString(syncSettings.ffmpegPath);
    if (!syncSettings.exiftoolPath.empty())
        syncArgs << "--exiftool" << QString::fromStdString(syncSettings.exiftoolPath);

    m_syncProcess = new QProcess(this);
    m_syncProcess->setProgram("python3");
    m_syncProcess->setArguments(syncArgs);
    m_syncProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_syncProcess, &QProcess::readyRead, this, [this]() {
        m_syncOutput->appendPlainText(
            QString::fromLocal8Bit(m_syncProcess->readAll()).trimmed());
    });
    connect(m_syncProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TripPropertiesDialog::onSyncFinished);
    m_syncProcess->start();
}

void TripPropertiesDialog::onSyncFinished(int exitCode, QProcess::ExitStatus) {
    if (m_syncRunBtn) {
        m_syncRunBtn->setEnabled(true);
        m_syncRunBtn->setText(m_trip.cameraSync.valid ? "Re-analyze" : "Run Analysis");
    }
    if (exitCode != 0) {
        if (m_syncOutput)
            m_syncOutput->appendPlainText("\nAnalysis failed.");
        return;
    }

    // Reload cameraSync from manifest JSON
    ConfigManager cfg;
    cfg.loadSettings();
    std::string mpath = cfg.lookupManifestFilePath(m_sourcePath);
    if (!mpath.empty()) {
        try {
            std::ifstream f(mpath);
            auto mj = json::parse(f);
            for (const auto& jt : mj.value("trips", json::array())) {
                if (jt.value("id", "") != m_trip.id) continue;
                if (!jt.contains("cameraSync")) break;
                const auto& jSync = jt["cameraSync"];
                m_trip.cameraSync.valid         = true;
                m_trip.cameraSync.analyzedAt    = jSync.value("analyzedAt", "");
                m_trip.cameraSync.syncCam       = jSync.value("syncCam", "");
                m_trip.cameraSync.spanFrames    = jSync.value("spanFrames", 0.0f);
                m_trip.cameraSync.spanVariation = jSync.value("spanVariation", 0.0f);
                m_trip.cameraSync.segmentTrims.clear();
                if (jSync.contains("segments") && jSync["segments"].is_object()) {
                    for (auto it = jSync["segments"].begin(); it != jSync["segments"].end(); ++it) {
                        std::map<std::string, double> camMap;
                        for (auto jt = it.value().begin(); jt != it.value().end(); ++jt)
                            camMap[jt.key()] = jt.value().get<double>();
                        m_trip.cameraSync.segmentTrims[it.key()] = camMap;
                    }
                }
                break;
            }
        } catch (...) {}
    }

    // refreshSyncTab tears down and rebuilds the widget, destroying m_syncOutput.
    // Preserve the script output so it survives the rebuild.
    QString savedLog = m_syncOutput ? m_syncOutput->toPlainText() : QString();

    refreshSyncTab();

    if (m_syncOutput && !savedLog.isEmpty()) {
        m_syncOutput->setPlainText(savedLog);
        m_syncOutput->setVisible(true);
        // Scroll to bottom so the summary / write confirmation is in view.
        m_syncOutput->verticalScrollBar()->setValue(
            m_syncOutput->verticalScrollBar()->maximum());
    }

    emit syncAnalyzed();   // notify TripTile to repaint SYNC indicator
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

    // Update in-memory trip, refresh the list widget, and update tab icon.
    tripVec.push_back(p);
    if (key == "mapVideos")
        populateVideoList(m_mapFileList,  m_trip.mapVideos);
    else if (key == "hudVideos")
        populateVideoList(m_hudFileList,  m_trip.hudVideos);
    else
        populateVideoList(m_dashFileList, m_trip.dashVideos);
    emit videosChanged();
}

#include "TripPropertiesDialog.moc"
// SN: 00117
