// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "MapProgressDialog.h"
#include "config_manager.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
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

using namespace CamClops;

// ---------------------------------------------------------------------------
// MapWorker — runs the render script on a background QThread.
// Parses stderr for progress lines and status messages.
// cancel() kills the child process; safe to call from any thread.
// Q_OBJECT in a .cpp requires the moc include at EOF.
// ---------------------------------------------------------------------------
class MapWorker : public QObject {
    Q_OBJECT
public:
    MapWorker(const QString& python3, const QString& script,
              const QStringList& args)
        : m_python3(python3), m_script(script), m_args(args) {}

public slots:
    void start() {
        QStringList fullArgs;
        fullArgs << m_script << m_args;

        QProcess proc;

#ifdef __APPLE__
        // macOS GUI apps have a minimal PATH that excludes Homebrew.
        {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("PATH", "/usr/local/bin:/opt/homebrew/bin:" + env.value("PATH"));
            proc.setProcessEnvironment(env);
        }
#endif
        // Register the process pointer so cancel() can reach it.
        {
            QMutexLocker lk(&m_mutex);
            m_proc = &proc;
        }

        proc.start(m_python3, fullArgs);
        if (!proc.waitForStarted(10000)) {
            QMutexLocker lk(&m_mutex);
            m_proc = nullptr;
            emit finished(false,
                "Failed to start python3 — is it installed and on PATH?");
            return;
        }

        QByteArray buf;
        while (proc.state() != QProcess::NotRunning) {
            proc.waitForReadyRead(200);
            buf += proc.readAllStandardError();
            processBuffer(buf, false);
        }
        buf += proc.readAllStandardError();
        processBuffer(buf, true);
        proc.waitForFinished(-1);

        {
            QMutexLocker lk(&m_mutex);
            m_proc = nullptr;
        }

        bool wasCancelled = m_cancelled.loadAcquire();
        bool ok = !wasCancelled
               && proc.exitStatus() == QProcess::NormalExit
               && proc.exitCode() == 0;
        QString err;
        if (wasCancelled)
            err = "Cancelled.";
        else if (!ok)
            err = QString("python3 exited with code %1").arg(proc.exitCode());
        emit finished(ok, err);
    }

    void cancel() {
        m_cancelled.storeRelease(1);
        QMutexLocker lk(&m_mutex);
        if (m_proc) {
            m_proc->kill();
        }
    }

signals:
    void progress(int pct, const QString& timeStr, const QString& speedStr);
    void statusMessage(const QString& msg);
    void finished(bool ok, const QString& error);

private:
    // Split buf on \r and \n.  Flush remaining bytes when flush=true.
    void processBuffer(QByteArray& buf, bool flush) {
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

            // Progress line: "  47%  [4:32 / 9:41]  3.2x"
            int pctIdx = line.indexOf('%');
            if (pctIdx > 0) {
                bool ok;
                int pct = line.left(pctIdx).trimmed().toInt(&ok);
                if (ok) {
                    QString timeStr, speedStr;
                    int lb = line.indexOf('[');
                    int rb = line.indexOf(']');
                    if (lb >= 0 && rb > lb)
                        timeStr = line.mid(lb + 1, rb - lb - 1).trimmed();
                    if (rb >= 0)
                        speedStr = line.mid(rb + 1).trimmed();
                    emit progress(pct, timeStr, speedStr);
                    continue;
                }
            }
            emit statusMessage(line);
        }
    }

    QString     m_python3;
    QString     m_script;
    QStringList m_args;
    QMutex      m_mutex;
    QProcess*   m_proc     = nullptr;
    QAtomicInt  m_cancelled{0};
};

// ---------------------------------------------------------------------------
// MapProgressDialog
// ---------------------------------------------------------------------------
MapProgressDialog::MapProgressDialog(const Trip&       trip,
                                     const QString&    manifestFile,
                                     const QString&    outputPath,
                                     int               width,
                                     int               height,
                                     QWidget*          parent,
                                     const QString&    scriptName,
                                     const QString&    title,
                                     const QStringList& extraArgs)
    : QDialog(parent),
      m_trip(trip), m_manifestFile(manifestFile), m_outputPath(outputPath),
      m_width(width), m_height(height),
      m_scriptName(scriptName.isEmpty() ? "clops_maprender.py" : scriptName),
      m_extraArgs(extraArgs)
{
    setWindowTitle(title.isEmpty() ? "Generating Map" : title);
    setMinimumWidth(520);
    // Keep the X button — closeEvent() handles it safely.

    auto* vlay = new QVBoxLayout(this);
    vlay->setSpacing(10);
    vlay->setContentsMargins(16, 16, 16, 16);

    // Trip info header
    vlay->addWidget(new QLabel(
        QString("Trip: <b>%1  %2</b>  (%3)")
            .arg(QString::fromStdString(trip.date))
            .arg(QString::fromStdString(trip.startTime))
            .arg(QString::fromStdString(trip.duration)),
        this));

    // Output path
    auto* outLbl = new QLabel(
        QString("Output: <tt>%1</tt>").arg(outputPath.toHtmlEscaped()), this);
    outLbl->setStyleSheet("font-size: 8pt;");
    outLbl->setWordWrap(true);
    vlay->addWidget(outLbl);

    // Scrolling status log
    m_statusLog = new QTextEdit(this);
    m_statusLog->setReadOnly(true);
    m_statusLog->setFixedHeight(110);
    m_statusLog->setFont(QFont("monospace", 8));
    vlay->addWidget(m_statusLog);

    // Progress bar
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_bar->setValue(0);
    vlay->addWidget(m_bar);

    // Time / speed label
    m_timeLabel = new QLabel("Starting...", this);
    m_timeLabel->setAlignment(Qt::AlignCenter);
    vlay->addWidget(m_timeLabel);

    // Final result label
    m_finalLabel = new QLabel(this);
    m_finalLabel->setAlignment(Qt::AlignCenter);
    vlay->addWidget(m_finalLabel);

    // Buttons: Cancel (active during render) + Close (active after)
    auto* hlay = new QHBoxLayout;
    hlay->addStretch();
    m_cancelBtn = new QPushButton("Cancel", this);
    m_cancelBtn->setEnabled(false);   // enabled in startRender()
    connect(m_cancelBtn, &QPushButton::clicked, this, &MapProgressDialog::onCancel);
    hlay->addWidget(m_cancelBtn);
    m_closeBtn = new QPushButton("Close", this);
    m_closeBtn->setEnabled(false);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    hlay->addWidget(m_closeBtn);
    vlay->addLayout(hlay);
}

void MapProgressDialog::startRender()
{
    QString script = findScript();
    if (script.isEmpty()) {
        m_statusLog->append(
            "Error: " + m_scriptName + " not found.\n"
            "Expected alongside the camclops-gui binary in a scripts/ subdirectory.");
        m_finalLabel->setText("Error: script not found");
        m_finalLabel->setStyleSheet("color: red;");
        m_closeBtn->setEnabled(true);
        m_done = true;
        return;
    }

    // Ensure --ffmpeg is set. Call sites may pass it via extraArgs; if not,
    // fall back to whatever is in settings so the user's configured path is
    // always honoured regardless of which call site launched the dialog.
    if (!m_extraArgs.contains("--ffmpeg")) {
        ConfigManager cfg;
        cfg.loadSettings();
        std::string fp = cfg.getSettings().ffmpegPath;
        if (!fp.empty())
            m_extraArgs << "--ffmpeg" << QString::fromStdString(fp);
    }

    QStringList args;
    args << "--manifest"   << m_manifestFile
         << "--trip"       << QString::fromStdString(m_trip.id)
         << "--output"     << m_outputPath
         << "--width"      << QString::number(m_width)
         << "--height"     << QString::number(m_height)
         << "--fps"        << "30"
         << m_extraArgs;

    m_worker = new MapWorker("python3", script, args);
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started,         m_worker, &MapWorker::start);
    connect(m_worker, &MapWorker::progress,      this,     &MapProgressDialog::onProgress);
    connect(m_worker, &MapWorker::statusMessage, this,     &MapProgressDialog::onStatusMessage);
    connect(m_worker, &MapWorker::finished,      this,     &MapProgressDialog::onFinished);
    connect(m_worker, &MapWorker::finished,      m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished,        m_worker, &QObject::deleteLater);
    connect(this,     &MapProgressDialog::cancelRequested,
            m_worker, &MapWorker::cancel, Qt::QueuedConnection);

    m_cancelBtn->setEnabled(true);
    m_thread->start();
}

// ---------------------------------------------------------------------------
// Cancel — ask the worker to kill the process; don't close yet.
// onFinished() will run when the process actually exits and will enable Close.
// ---------------------------------------------------------------------------
void MapProgressDialog::onCancel()
{
    m_cancelBtn->setEnabled(false);
    m_finalLabel->setText("Cancelling...");
    m_finalLabel->setStyleSheet("color: #cc6600;");
    emit cancelRequested();
}

// ---------------------------------------------------------------------------
// closeEvent / reject — block close while the render thread is still running.
// ---------------------------------------------------------------------------
void MapProgressDialog::closeEvent(QCloseEvent* event)
{
    if (!m_done && m_thread && m_thread->isRunning()) {
        // Trigger cancel and ignore this close attempt.
        // The dialog will close itself once onFinished() fires.
        onCancel();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void MapProgressDialog::reject()
{
    if (!m_done && m_thread && m_thread->isRunning()) {
        onCancel();
        return;
    }
    QDialog::reject();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------
void MapProgressDialog::onProgress(int pct, const QString& timeStr,
                                   const QString& speedStr)
{
    m_bar->setValue(pct);
    QString label = timeStr.isEmpty() ? QString("%1%").arg(pct)
                                      : QString("%1%  [%2]").arg(pct).arg(timeStr);
    if (!speedStr.isEmpty())
        label += "  " + speedStr;
    m_timeLabel->setText(label);
}

void MapProgressDialog::onStatusMessage(const QString& msg)
{
    m_statusLog->append(msg);
    auto* sb = m_statusLog->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MapProgressDialog::onFinished(bool ok, const QString& error)
{
    m_done = true;
    m_cancelBtn->setEnabled(false);
    m_bar->setValue(ok ? 100 : m_bar->value());
    if (ok) {
        m_finalLabel->setText("Done.");
        m_finalLabel->setStyleSheet("color: green; font-weight: bold;");
    } else {
        m_finalLabel->setText(error.isEmpty() ? "Failed." : error);
        m_finalLabel->setStyleSheet(
            error == "Cancelled." ? "color: #cc6600;" : "color: red;");
    }
    m_closeBtn->setEnabled(true);
    emit renderComplete(ok);
}

QString MapProgressDialog::findScript() const
{
    QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/scripts/" + m_scriptName,                           // Windows: next to binary
        appDir + "/../scripts/" + m_scriptName,                        // macOS: app bundle Contents/scripts
        appDir + "/../share/camclops/scripts/" + m_scriptName,         // Linux: /usr/share/camclops/scripts
        appDir + "/" + m_scriptName,
    };
    for (const QString& p : candidates) {
        if (QFile::exists(p))
            return QFileInfo(p).canonicalFilePath();
    }
    return {};
}

#include "MapProgressDialog.moc"
// SN: 00106
