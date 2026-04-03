// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "BuildProgressDialog.h"
#include "video_build.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QThread>
#include <QScrollBar>

using namespace Pathmux;

// ---------------------------------------------------------------------------
// Friendly display name for stage labels emitted by VideoBuilder.
// e.g. "concat:front" → "Front — concat"
//      "collage:4K"   → "Collage 4K"
//      "norm:0"       → "Normalize segment 1"
// ---------------------------------------------------------------------------
static QString stageDisplayName(const QString& label)
{
    if (label.startsWith("concat:"))
    {
        QString cam = label.mid(7);
        cam[0] = cam[0].toUpper();
        return cam + " \u2014 concat";
    }
    if (label == "collage:4K")   return "Collage 4K";
    if (label == "collage:1080") return "Collage 1080p";
    if (label.startsWith("norm:"))
    {
        bool ok;
        int n = label.mid(5).toInt(&ok);
        return ok ? QString("Normalize segment %1").arg(n + 1) : label;
    }
    if (label.startsWith("audio:"))
    {
        QString cam = label.mid(6);
        if (!cam.isEmpty()) cam[0] = cam[0].toUpper();
        return cam + " \u2014 audio extract";
    }
    // Unknown label: return as-is with first char uppercased
    if (label.isEmpty()) return label;
    QString s = label;
    s[0] = s[0].toUpper();
    return s;
}

// ---------------------------------------------------------------------------
// BuildWorker — runs VideoBuilder::buildTrip on a background thread.
// Q_OBJECT in a .cpp requires the .moc include at EOF.
// ---------------------------------------------------------------------------
class BuildWorker : public QObject {
    Q_OBJECT
public:
    BuildWorker(const Trip& trip, const VideoOptions& opts)
        : m_trip(trip), m_opts(opts) {}

public slots:
    void start() {
        VideoBuilder builder;
        builder.progressCallback = [this](const std::string& label, int pct, int eta) {
            emit progress(QString::fromStdString(label), pct, eta);
        };
        try {
            builder.buildTrip(m_trip, m_opts);
            emit finished(true, "");
        } catch (const std::exception& ex) {
            emit finished(false, QString::fromLatin1(ex.what()));
        } catch (...) {
            emit finished(false, "Unknown error during build");
        }
    }

signals:
    void progress(const QString& label, int pct, int etaSecs);
    void finished(bool ok, const QString& error);

private:
    Trip         m_trip;
    VideoOptions m_opts;
};

// ---------------------------------------------------------------------------
// BuildProgressDialog
// ---------------------------------------------------------------------------
BuildProgressDialog::BuildProgressDialog(const Trip& trip,
                                         const VideoOptions& opts,
                                         QWidget* parent)
    : QDialog(parent), m_trip(trip), m_opts(opts)
{
    setWindowTitle("Building Video");
    setModal(true);
    setMinimumWidth(560);
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);

    auto* vlay = new QVBoxLayout(this);
    vlay->setSpacing(10);
    vlay->setContentsMargins(16, 16, 16, 16);

    // Trip info header
    auto* tripLabel = new QLabel(
        QString("Trip: <b>%1  %2</b>  (%3)")
            .arg(QString::fromStdString(trip.date))
            .arg(QString::fromStdString(trip.startTime))
            .arg(QString::fromStdString(trip.duration)),
        this);
    vlay->addWidget(tripLabel);

    // Scroll area for the growing stage stack
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setMinimumHeight(120);
    m_scrollArea->setMaximumHeight(320);
    m_scrollArea->setFrameShape(QFrame::StyledPanel);

    auto* stageContainer = new QWidget;
    m_stageLayout = new QVBoxLayout(stageContainer);
    m_stageLayout->setSpacing(6);
    m_stageLayout->setContentsMargins(8, 8, 8, 8);
    m_stageLayout->addStretch();   // pushes rows to top; rows insert before stretch
    m_scrollArea->setWidget(stageContainer);
    vlay->addWidget(m_scrollArea);

    // Final status line (shows "Build complete" or error)
    m_finalLabel = new QLabel("Waiting for first stage...", this);
    m_finalLabel->setStyleSheet("color: #606060; font-style: italic;");
    vlay->addWidget(m_finalLabel);

    m_closeBtn = new QPushButton("Close", this);
    m_closeBtn->setEnabled(false);
    auto* bbox = new QDialogButtonBox(this);
    bbox->addButton(m_closeBtn, QDialogButtonBox::AcceptRole);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    vlay->addWidget(bbox);

    resize(560, 300);
}

// ---------------------------------------------------------------------------
// Add a new stage row to the stack
// ---------------------------------------------------------------------------
void BuildProgressDialog::addStageRow(const QString& label)
{
    StageRow row;
    row.label = label;

    auto* rowWidget = new QWidget;
    auto* hbox      = new QHBoxLayout(rowWidget);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(8);

    auto* nameLabel = new QLabel(stageDisplayName(label), rowWidget);
    nameLabel->setMinimumWidth(180);
    nameLabel->setMaximumWidth(180);

    row.bar = new QProgressBar(rowWidget);
    row.bar->setRange(0, 100);
    row.bar->setValue(0);
    row.bar->setTextVisible(false);
    row.bar->setFixedHeight(16);

    row.status = new QLabel(rowWidget);
    row.status->setMinimumWidth(90);
    row.status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row.status->setStyleSheet("color: #505050; font-size: 8pt;");

    hbox->addWidget(nameLabel);
    hbox->addWidget(row.bar, 1);
    hbox->addWidget(row.status);

    // Insert before the trailing stretch (always at last position)
    m_stageLayout->insertWidget(m_stageLayout->count() - 1, rowWidget);
    m_rows.append(row);

    // Scroll to bottom so the newest stage is always visible
    QScrollBar* sb = m_scrollArea->verticalScrollBar();
    sb->setValue(sb->maximum());
}

// ---------------------------------------------------------------------------
// Mark the current (last) row as complete
// ---------------------------------------------------------------------------
void BuildProgressDialog::completeCurrentRow()
{
    if (m_rows.isEmpty()) return;
    StageRow& row = m_rows.last();
    row.bar->setValue(100);
    row.status->setText("\u2713");   // ✓
    row.status->setStyleSheet("color: #228b22; font-weight: bold;");
}

// ---------------------------------------------------------------------------
// Start the background build thread
// ---------------------------------------------------------------------------
void BuildProgressDialog::startBuild()
{
    QThread*     thread = new QThread(this);
    BuildWorker* worker = new BuildWorker(m_trip, m_opts);
    worker->moveToThread(thread);

    connect(thread, &QThread::started,      worker, &BuildWorker::start);
    connect(worker, &BuildWorker::progress, this,   &BuildProgressDialog::onProgress);
    connect(worker, &BuildWorker::finished, this,   &BuildProgressDialog::onFinished);
    connect(worker, &BuildWorker::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished,     worker, &QObject::deleteLater);

    thread->start();
}

// ---------------------------------------------------------------------------
// Progress update from worker
// ---------------------------------------------------------------------------
void BuildProgressDialog::onProgress(const QString& label, int pct, int etaSecs)
{
    // New stage — complete the previous row and start a new one
    if (label != m_currentLabel) {
        completeCurrentRow();
        addStageRow(label);
        m_currentLabel = label;
        m_finalLabel->setText(stageDisplayName(label) + "\u2026");
        m_finalLabel->setStyleSheet("color: #606060; font-style: italic;");
    }

    // Update current row
    StageRow& row = m_rows.last();
    row.bar->setValue(pct);

    if (etaSecs > 0) {
        if (etaSecs >= 60)
            row.status->setText(QString("%1m %2s")
                .arg(etaSecs / 60).arg(etaSecs % 60, 2, 10, QChar('0')));
        else
            row.status->setText(QString("%1s").arg(etaSecs));
        row.status->setStyleSheet("color: #505050; font-size: 8pt;");
    }
}

// ---------------------------------------------------------------------------
// Build finished
// ---------------------------------------------------------------------------
void BuildProgressDialog::onFinished(bool ok, const QString& error)
{
    if (ok) {
        completeCurrentRow();
        m_finalLabel->setText("\u2713  Build complete.");
        m_finalLabel->setStyleSheet("color: #228b22; font-weight: bold;");
    } else {
        // Mark current row as errored
        if (!m_rows.isEmpty()) {
            m_rows.last().status->setText("\u2717");   // ✗
            m_rows.last().status->setStyleSheet("color: #cc0000; font-weight: bold;");
        }
        m_finalLabel->setText("\u2717  Error: " + error);
        m_finalLabel->setStyleSheet("color: #cc0000; font-weight: bold;");
    }

    m_closeBtn->setEnabled(true);
    emit buildComplete(ok);
}

#include "BuildProgressDialog.moc"
// SN: 00091
