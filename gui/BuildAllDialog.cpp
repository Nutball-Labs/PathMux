// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "BuildAllDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QThread>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QScrollBar>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QMutex>
#include <QProcessEnvironment>
#include <fstream>
#include "gps_export.hpp"
#include "json.hpp"
using json = nlohmann::json;

using namespace CamClops;

// ---------------------------------------------------------------------------
// BuildAllWorker — runs each script consecutively on a background QThread.
// Emits per-stage progress and status signals; cancel() kills the current
// process and prevents subsequent stages from starting.
// ---------------------------------------------------------------------------
class BuildAllWorker : public QObject {
    Q_OBJECT
public:
    struct Spec {
        BuildAllStage::Type type = BuildAllStage::Type::Script;
        // Script:
        QString     scriptPath;
        QString     outputPath;
        int         width = 0, height = 0;
        QStringList extraArgs;
        // Both:
        QString     manifestFile;
        QString     tripId;
        // GpsExtract:
        std::string exiftoolPath;
    };

    explicit BuildAllWorker(const QList<Spec>& specs) : m_specs(specs) {}

    void cancel() {
        m_cancelled.storeRelease(1);
        QMutexLocker lk(&m_mutex);
        if (m_proc) m_proc->kill();
    }

public slots:
    void start() {
        for (int i = 0; i < m_specs.size(); ++i) {
            if (m_cancelled.loadAcquire()) {
                emit allFinished(false);
                return;
            }
            emit stageStarted(i);
            if (!runOne(i)) {
                emit allFinished(false);
                return;
            }
        }
        emit allFinished(true);
    }

signals:
    void stageStarted(int idx);
    void progress(int idx, int pct, const QString& timeStr, const QString& speedStr);
    void stageMessage(int idx, const QString& msg);
    void stageFinished(int idx, bool ok, const QString& error);
    void allFinished(bool allOk);

private:
    bool runOne(int idx) {
        if (m_specs[idx].type == BuildAllStage::Type::GpsExtract)
            return runGpsExtract(idx);
        return runScript(idx);
    }

    bool runGpsExtract(int idx) {
        const auto& s = m_specs[idx];
        std::ifstream ifs(s.manifestFile.toStdString());
        if (!ifs.is_open()) {
            emit stageFinished(idx, false, "Cannot open manifest");
            return false;
        }
        json root;
        try { ifs >> root; } catch (...) {
            emit stageFinished(idx, false, "Manifest JSON parse error");
            return false;
        }
        ifs.close();

        int tripIdx = -1;
        if (root.contains("trips") && root["trips"].is_array()) {
            for (int i = 0; i < (int)root["trips"].size(); ++i) {
                if (root["trips"][i].value("id", "") == s.tripId.toStdString()) {
                    tripIdx = i;
                    break;
                }
            }
        }
        if (tripIdx < 0) {
            emit stageFinished(idx, false,
                QString("Trip %1 not found in manifest").arg(s.tripId));
            return false;
        }

        bool ok = CamClops::extractGps(root, tripIdx, s.manifestFile.toStdString(),
                                      s.exiftoolPath,
                                      /*verbose=*/false,
                                      [this, idx](int done, int total) {
                                          int pct = total > 0 ? done * 100 / total : 0;
                                          emit progress(idx, pct,
                                              QString("Segment %1 / %2").arg(done).arg(total),
                                              "");
                                      });
        emit stageFinished(idx, ok,
            ok ? "" : "GPS extraction failed — verify exiftool is installed");
        return ok;
    }

    bool runScript(int idx) {
        const auto& s = m_specs[idx];

        QStringList fullArgs;
        fullArgs << s.scriptPath
                 << "--manifest" << s.manifestFile
                 << "--trip"     << s.tripId
                 << "--output"   << s.outputPath
                 << "--width"    << QString::number(s.width)
                 << "--height"   << QString::number(s.height)
                 << "--fps"      << "30"
                 << s.extraArgs;

        QProcess proc;
#ifdef __APPLE__
        {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("PATH", "/usr/local/bin:/opt/homebrew/bin:" + env.value("PATH"));
            proc.setProcessEnvironment(env);
        }
#endif
        { QMutexLocker lk(&m_mutex); m_proc = &proc; }

        proc.start("python3", fullArgs);
        if (!proc.waitForStarted(10000)) {
            { QMutexLocker lk(&m_mutex); m_proc = nullptr; }
            emit stageFinished(idx, false,
                "Failed to start python3 — is it installed and on PATH?");
            return false;
        }

        QByteArray buf;
        while (proc.state() != QProcess::NotRunning) {
            proc.waitForReadyRead(200);
            buf += proc.readAllStandardError();
            processBuffer(idx, buf, false);
        }
        buf += proc.readAllStandardError();
        processBuffer(idx, buf, true);
        proc.waitForFinished(-1);

        { QMutexLocker lk(&m_mutex); m_proc = nullptr; }

        bool wasCancelled = m_cancelled.loadAcquire();
        bool ok = !wasCancelled
               && proc.exitStatus() == QProcess::NormalExit
               && proc.exitCode() == 0;

        QString err;
        if (wasCancelled)      err = "Cancelled.";
        else if (!ok)          err = QString("Exited with code %1").arg(proc.exitCode());

        emit stageFinished(idx, ok && !wasCancelled, err);
        return ok && !wasCancelled;
    }

    void processBuffer(int idx, QByteArray& buf, bool flush) {
        while (true) {
            int rpos = buf.indexOf('\r');
            int npos = buf.indexOf('\n');
            int pos  = -1;
            if      (rpos >= 0 && (npos < 0 || rpos <= npos)) pos = rpos;
            else if (npos >= 0)                                 pos = npos;
            else if (flush && !buf.isEmpty())                   pos = buf.size();
            else break;

            QByteArray raw = buf.left(pos).trimmed();
            buf.remove(0, std::min(pos + 1, (int)buf.size()));
            if (raw.isEmpty()) continue;

            QString line = QString::fromUtf8(raw);
            int pctIdx = line.indexOf('%');
            if (pctIdx > 0) {
                bool numOk;
                int pct = line.left(pctIdx).trimmed().toInt(&numOk);
                if (numOk) {
                    QString timeStr, speedStr;
                    int lb = line.indexOf('['), rb = line.indexOf(']');
                    if (lb >= 0 && rb > lb)
                        timeStr = line.mid(lb + 1, rb - lb - 1).trimmed();
                    if (rb >= 0)
                        speedStr = line.mid(rb + 1).trimmed();
                    emit progress(idx, pct, timeStr, speedStr);
                    continue;
                }
            }
            emit stageMessage(idx, line);
        }
    }

    QList<Spec>  m_specs;
    QMutex       m_mutex;
    QProcess*    m_proc = nullptr;
    QAtomicInt   m_cancelled{0};
};

// ---------------------------------------------------------------------------
// BuildAllDialog
// ---------------------------------------------------------------------------
static QString findBuildScript(const QString& name)
{
    QString appDir = QCoreApplication::applicationDirPath();
    const QStringList dirs = {
        appDir + "/scripts",
        appDir + "/../scripts",
        appDir,
    };
    for (const QString& d : dirs) {
        QString p = d + "/" + name;
        if (QFile::exists(p))
            return QFileInfo(p).canonicalFilePath();
    }
    return {};
}

BuildAllDialog::BuildAllDialog(const Trip&              trip,
                               const QString&           manifestFile,
                               const QList<BuildAllStage>& stages,
                               QWidget*                 parent)
    : QDialog(parent),
      m_trip(trip), m_manifestFile(manifestFile), m_stages(stages)
{
    setWindowTitle("Build All Overlay Videos");
    setMinimumWidth(560);

    auto* vlay = new QVBoxLayout(this);
    vlay->setSpacing(10);
    vlay->setContentsMargins(16, 16, 16, 16);

    vlay->addWidget(new QLabel(
        QString("Trip: <b>%1  %2</b>  (%3)")
            .arg(QString::fromStdString(trip.date))
            .arg(QString::fromStdString(trip.startTime))
            .arg(QString::fromStdString(trip.duration)),
        this));

    // Stage rows — name | bar | status
    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);

    for (int i = 0; i < stages.size(); ++i) {
        auto* nameLbl = new QLabel(stages[i].name + ":", this);
        nameLbl->setFixedWidth(130);

        auto* bar = new QProgressBar(this);
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setTextVisible(false);

        auto* status = new QLabel("Pending", this);
        status->setFixedWidth(160);
        status->setStyleSheet("color: gray;");

        grid->addWidget(nameLbl, i, 0);
        grid->addWidget(bar,     i, 1);
        grid->addWidget(status,  i, 2);

        m_rows.append({bar, status});
    }
    vlay->addLayout(grid);

    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setFixedHeight(100);
    m_log->setFont(QFont("monospace", 8));
    vlay->addWidget(m_log);

    m_finalLabel = new QLabel(this);
    m_finalLabel->setAlignment(Qt::AlignCenter);
    vlay->addWidget(m_finalLabel);

    auto* hlay = new QHBoxLayout;
    hlay->addStretch();
    m_cancelBtn = new QPushButton("Cancel", this);
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &BuildAllDialog::onCancel);
    hlay->addWidget(m_cancelBtn);
    m_closeBtn = new QPushButton("Close", this);
    m_closeBtn->setEnabled(false);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    hlay->addWidget(m_closeBtn);
    vlay->addLayout(hlay);
}

void BuildAllDialog::startBuild()
{
    // Resolve script paths and build worker spec list.
    QList<BuildAllWorker::Spec> specs;
    for (int i = 0; i < m_stages.size(); ++i) {
        const auto& st = m_stages[i];
        BuildAllWorker::Spec sp;
        sp.type         = st.type;
        sp.manifestFile = m_manifestFile;
        sp.tripId       = QString::fromStdString(m_trip.id);

        if (st.type == BuildAllStage::Type::GpsExtract) {
            sp.exiftoolPath = st.exiftoolPath.toStdString();
        } else {
            QString scriptPath = findBuildScript(st.scriptName);
            if (scriptPath.isEmpty()) {
                m_rows[i].status->setText("Script not found");
                m_rows[i].status->setStyleSheet("color: red;");
                m_log->append("Error: " + st.scriptName + " not found.");
                m_finalLabel->setText("Cannot start: missing script(s).");
                m_finalLabel->setStyleSheet("color: red;");
                m_closeBtn->setEnabled(true);
                m_done = true;
                return;
            }
            sp.scriptPath = scriptPath;
            sp.outputPath = st.outputPath;
            sp.width      = st.width;
            sp.height     = st.height;
            sp.extraArgs  = st.extraArgs;
        }
        specs.append(sp);
    }

    m_worker = new BuildAllWorker(specs);
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started,            m_worker, &BuildAllWorker::start);
    connect(m_worker, &BuildAllWorker::stageStarted, this,    &BuildAllDialog::onStageStarted);
    connect(m_worker, &BuildAllWorker::progress,     this,    &BuildAllDialog::onProgress);
    connect(m_worker, &BuildAllWorker::stageMessage, this,    &BuildAllDialog::onStageMessage);
    connect(m_worker, &BuildAllWorker::stageFinished,this,    &BuildAllDialog::onStageFinished);
    connect(m_worker, &BuildAllWorker::allFinished,  this,    &BuildAllDialog::onAllFinished);
    connect(m_worker, &BuildAllWorker::allFinished,  m_thread,&QThread::quit);
    connect(m_thread, &QThread::finished,            m_worker,&QObject::deleteLater);
    connect(this,     &BuildAllDialog::cancelRequested,
            m_worker, &BuildAllWorker::cancel, Qt::QueuedConnection);

    m_cancelBtn->setEnabled(true);
    m_thread->start();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------
void BuildAllDialog::onStageStarted(int idx)
{
    if (idx < 0 || idx >= m_rows.size()) return;
    m_rows[idx].bar->setValue(0);
    m_rows[idx].status->setText("Starting\xe2\x80\xa6");
    m_rows[idx].status->setStyleSheet("color: #cc6600;");
}

void BuildAllDialog::onProgress(int idx, int pct,
                                const QString& timeStr, const QString& speedStr)
{
    if (idx < 0 || idx >= m_rows.size()) return;
    m_rows[idx].bar->setValue(pct);
    QString label = timeStr.isEmpty() ? QString("%1%").arg(pct)
                                      : QString("%1%  [%2]").arg(pct).arg(timeStr);
    if (!speedStr.isEmpty()) label += "  " + speedStr;
    m_rows[idx].status->setText(label);
    m_rows[idx].status->setStyleSheet("");
}

void BuildAllDialog::onStageMessage(int idx, const QString& msg)
{
    Q_UNUSED(idx)
    m_log->append(msg);
    auto* sb = m_log->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void BuildAllDialog::onStageFinished(int idx, bool ok, const QString& error)
{
    if (idx < 0 || idx >= m_rows.size()) return;
    if (ok) {
        m_rows[idx].bar->setValue(100);
        m_rows[idx].status->setText("\xe2\x9c\x93 Done");
        m_rows[idx].status->setStyleSheet("color: green; font-weight: bold;");
    } else {
        m_rows[idx].status->setText("\xe2\x9c\x97 " + (error.isEmpty() ? "Failed" : error));
        m_rows[idx].status->setStyleSheet("color: red;");
    }
    emit stageComplete(idx, m_stages[idx].outputPath, ok);
}

void BuildAllDialog::onAllFinished(bool allOk)
{
    m_done = true;
    m_cancelBtn->setEnabled(false);
    m_closeBtn->setEnabled(true);
    if (allOk) {
        m_finalLabel->setText("All builds complete.");
        m_finalLabel->setStyleSheet("color: green; font-weight: bold;");
    } else {
        m_finalLabel->setText("Build stopped.");
        m_finalLabel->setStyleSheet("color: #cc6600;");
    }
}

void BuildAllDialog::onCancel()
{
    m_cancelBtn->setEnabled(false);
    m_finalLabel->setText("Cancelling\xe2\x80\xa6");
    m_finalLabel->setStyleSheet("color: #cc6600;");
    emit cancelRequested();
}

void BuildAllDialog::closeEvent(QCloseEvent* event)
{
    if (!m_done && m_thread && m_thread->isRunning()) {
        onCancel();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void BuildAllDialog::reject()
{
    if (!m_done && m_thread && m_thread->isRunning()) {
        onCancel();
        return;
    }
    QDialog::reject();
}

#include "BuildAllDialog.moc"
// SN: 00106
