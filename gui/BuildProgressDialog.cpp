// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "BuildProgressDialog.h"
#include "video_build.hpp"
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QThread>

using namespace Pathmux;

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
    setMinimumWidth(480);
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);

    auto* vlay = new QVBoxLayout(this);
    vlay->setSpacing(10);
    vlay->setContentsMargins(16, 16, 16, 16);

    auto* tripLabel = new QLabel(
        QString("Trip: <b>%1  %2</b>  (%3)")
            .arg(QString::fromStdString(trip.date))
            .arg(QString::fromStdString(trip.startTime))
            .arg(QString::fromStdString(trip.duration)),
        this);
    vlay->addWidget(tripLabel);

    m_stageLabel = new QLabel("Preparing build...", this);
    vlay->addWidget(m_stageLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    vlay->addWidget(m_progressBar);

    m_etaLabel = new QLabel(this);
    m_etaLabel->setAlignment(Qt::AlignRight);
    vlay->addWidget(m_etaLabel);

    m_closeBtn = new QPushButton("Close", this);
    m_closeBtn->setEnabled(false);
    auto* bbox = new QDialogButtonBox(this);
    bbox->addButton(m_closeBtn, QDialogButtonBox::AcceptRole);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    vlay->addWidget(bbox);
}

void BuildProgressDialog::startBuild()
{
    QThread*     thread = new QThread(this);
    BuildWorker* worker = new BuildWorker(m_trip, m_opts);
    worker->moveToThread(thread);

    connect(thread, &QThread::started,   worker, &BuildWorker::start);
    connect(worker, &BuildWorker::progress, this, &BuildProgressDialog::onProgress);
    connect(worker, &BuildWorker::finished, this, &BuildProgressDialog::onFinished);
    connect(worker, &BuildWorker::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished,  worker, &QObject::deleteLater);

    thread->start();
}

void BuildProgressDialog::onProgress(const QString& label, int pct, int etaSecs)
{
    m_stageLabel->setText(label);
    m_progressBar->setValue(pct);

    if (etaSecs > 0) {
        if (etaSecs >= 60)
            m_etaLabel->setText(QString("ETA: %1m %2s")
                .arg(etaSecs / 60).arg(etaSecs % 60, 2, 10, QChar('0')));
        else
            m_etaLabel->setText(QString("ETA: %1s").arg(etaSecs));
    } else {
        m_etaLabel->clear();
    }
}

void BuildProgressDialog::onFinished(bool ok, const QString& error)
{
    m_progressBar->setValue(ok ? 100 : m_progressBar->value());
    m_closeBtn->setEnabled(true);

    if (ok) {
        m_stageLabel->setText("Build complete.");
        m_etaLabel->clear();
        emit buildComplete(true);
    } else {
        m_stageLabel->setText("Error: " + error);
        emit buildComplete(false);
    }
}

#include "BuildProgressDialog.moc"
// SN: 00091
