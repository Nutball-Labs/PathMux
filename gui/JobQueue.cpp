// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "JobQueue.h"
#include "config_manager.hpp"
#include "trip_detection.hpp"
#include "gps_export.hpp"
#include "video_build.hpp"
#include "json.hpp"
#include <QThread>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QAtomicInt>
#include <fstream>
#include <atomic>
#include <vector>

using json = nlohmann::json;
using namespace CamClops;

static QString fmtElapsed(qint64 msecs)
{
    int s = (int)(msecs / 1000);
    return s < 60 ? QString("%1s").arg(s)
                  : QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

static void addStep(QVector<Job::StepInfo>& v, const QString& name)
{
    for (const auto& s : v) if (s.name == name) return;
    Job::StepInfo si; si.name = name; v.append(si);
}

static Job::StepInfo* findStep(QVector<Job::StepInfo>& v, const QString& name)
{
    for (auto& s : v) if (s.name == name) return &s;
    return nullptr;
}

// ─── GpsExtractJobWorker ──────────────────────────────────────────────────────
class GpsExtractJobWorker : public QObject {
    Q_OBJECT
public:
    GpsExtractJobWorker(const std::string& tripId,
                        const std::string& manifestFile,
                        const std::string& exiftoolPath)
        : m_tripId(tripId), m_manifestFile(manifestFile),
          m_exiftoolPath(exiftoolPath) {}

public slots:
    void start() {
        std::ifstream ifs(m_manifestFile);
        if (!ifs.is_open()) {
            emit finished(false, QString("Cannot open manifest: %1")
                              .arg(QString::fromStdString(m_manifestFile)));
            return;
        }
        json root;
        try { ifs >> root; } catch (...) {
            emit finished(false, "Manifest JSON parse error");
            return;
        }
        ifs.close();

        int tripIdx = -1;
        if (root.contains("trips") && root["trips"].is_array()) {
            for (int i = 0; i < (int)root["trips"].size(); ++i) {
                if (root["trips"][i].value("id", "") == m_tripId) {
                    tripIdx = i; break;
                }
            }
        }
        if (tripIdx < 0) {
            emit finished(false, QString("Trip %1 not found in manifest")
                              .arg(QString::fromStdString(m_tripId)));
            return;
        }

        bool ok = CamClops::extractGps(root, tripIdx, m_manifestFile,
                                      m_exiftoolPath, false,
                                      [this](int done, int total) {
                                          emit progress(done, total);
                                      });
        emit finished(ok,
            ok ? "" : "GPS extraction failed — verify exiftool and camera profile");
    }

signals:
    void progress(int done, int total);
    void finished(bool ok, const QString& error);

private:
    std::string m_tripId, m_manifestFile, m_exiftoolPath;
};

// ─── MapRenderWorker ──────────────────────────────────────────────────────────
class MapRenderWorker : public QObject {
    Q_OBJECT
public:
    MapRenderWorker(const QString& python3, const QString& script,
                    const QStringList& args)
        : m_python3(python3), m_script(script), m_args(args) {}

    void cancel() {
        // Set the flag only — do NOT call m_proc->kill() here.
        // QProcess is not thread-safe; m_proc is owned by the worker thread.
        // Calling kill() across threads can deadlock on Qt's internal mutexes,
        // freezing the main thread.  The start() loop checks m_cancelled every
        // 100 ms and kills the process from the correct thread.
        m_cancelled.storeRelease(1);
    }

public slots:
    void start() {
        QStringList fullArgs;
        fullArgs << m_script << m_args;

        QProcess proc;
        // Merge stdout+stderr so clops_sync_analyze.py (stdout) and map/hud scripts
        // (stderr) both feed the same drain loop — prevents stdout pipe deadlock.
        proc.setProcessChannelMode(QProcess::MergedChannels);
#ifdef __APPLE__
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("PATH", "/usr/local/bin:/opt/homebrew/bin:" + env.value("PATH"));
        proc.setProcessEnvironment(env);
#endif
        proc.start(m_python3, fullArgs);
        if (!proc.waitForStarted(10000)) {
            emit finished(false, "Failed to start python3");
            return;
        }

        QByteArray buf;
        while (proc.state() != QProcess::NotRunning) {
            if (m_cancelled.loadAcquire()) {
                proc.kill();                 // safe: called from worker thread
                proc.waitForFinished(3000);
                emit finished(false, "Cancelled.");
                return;
            }
            proc.waitForReadyRead(100);      // 100 ms poll so cancel is responsive
            buf += proc.readAllStandardOutput();
            processBuffer(buf, false);
        }
        buf += proc.readAllStandardOutput();
        processBuffer(buf, true);
        proc.waitForFinished(-1);

        bool ok = !m_cancelled.loadAcquire()
               && proc.exitStatus() == QProcess::NormalExit
               && proc.exitCode() == 0;
        QString err;
        if (m_cancelled.loadAcquire()) err = "Cancelled.";
        else if (!ok) {
            // Use the last line the script printed as the error — it's the actual
            // Python error message. Fall back to the generic code only if silent.
            err = m_lastLine.isEmpty()
                  ? QString("python3 exited with code %1").arg(proc.exitCode())
                  : m_lastLine;
        }
        emit finished(ok, err);
    }

signals:
    void progress(int pct, const QString& timeStr, const QString& speedStr);
    void statusMessage(const QString& msg);
    void finished(bool ok, const QString& error);

private:
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

            int pctIdx = line.indexOf('%');
            if (pctIdx > 0) {
                bool ok;
                int pct = line.left(pctIdx).trimmed().toInt(&ok);
                if (ok) {
                    QString timeStr, speedStr;
                    int lb = line.indexOf('['), rb = line.indexOf(']');
                    if (lb >= 0 && rb > lb) timeStr  = line.mid(lb + 1, rb - lb - 1).trimmed();
                    if (rb >= 0)             speedStr = line.mid(rb + 1).trimmed();
                    emit progress(pct, timeStr, speedStr);
                    continue;
                }
            }
            m_lastLine = line;
            emit statusMessage(line);
        }
    }

    QString     m_python3, m_script;
    QStringList m_args;
    QAtomicInt  m_cancelled{0};
    QString     m_lastLine;
};

// ─── CollageWorker ────────────────────────────────────────────────────────────
// Based on BuildWorker from BuildProgressDialog.cpp, adapted for JobQueue signals.
class CollageWorker : public QObject {
    Q_OBJECT
public:
    CollageWorker(const Trip& trip, const VideoOptions& opts,
                  bool verbose)
        : m_trip(trip), m_opts(opts), m_verbose(verbose) {}

    void cancel() { m_cancel.store(true); }

public slots:
    void start() {
        VideoBuilder builder;

        builder.progressCallback = [this](const std::string& label, int pct, int eta) {
            emit progress(QString::fromStdString(label), pct, eta, "");
        };

        builder.ffmpegRunner = [this](const std::string& cmd,
                                      const std::string& label,
                                      int totalSecs) -> bool {
            std::string fullCmd = cmd;
            {
                auto pos = fullCmd.find(' ');
                if (pos != std::string::npos)
                    fullCmd.insert(pos, " -loglevel error -progress pipe:1");
            }
            // Probe phase owns 0–24%; scale ffmpeg 0–100% to 25–100% for concat stages.
            const bool isConcat = (label.rfind("concat:", 0) == 0);
            auto scalePct = [isConcat](int raw) -> int {
                if (!isConcat || raw < 0) return raw;
                return 25 + raw * 75 / 100;
            };

            int64_t totalUs = (int64_t)totalSecs * 1000000LL;
            int64_t outTimeUs = 0;
            std::vector<double> speedHist;
            speedHist.reserve(8);

            emit progress(QString::fromStdString(label), -2, 0,
                          QString::fromStdString(fullCmd));
            emit progress(QString::fromStdString(label), scalePct(0), 0, "");

            QProcess proc;
            proc.setReadChannel(QProcess::StandardOutput);
#ifdef _WIN32
            proc.setProgram("cmd.exe");
            proc.setNativeArguments("/c " + QString::fromStdString(fullCmd));
            proc.start();
#elif defined(__APPLE__)
            {
                QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
                env.insert("PATH", "/usr/local/bin:/opt/homebrew/bin:" + env.value("PATH"));
                proc.setProcessEnvironment(env);
            }
            proc.start("sh", {"-c", QString::fromStdString(fullCmd)});
#else
            proc.start("sh", {"-c", QString::fromStdString(fullCmd)});
#endif
            if (!proc.waitForStarted(10000)) {
                emit progress(QString::fromStdString(label), -1, 0, "Process failed to start");
                return false;
            }

            QByteArray buf, errBuf, errParseBuf;
            while (proc.state() != QProcess::NotRunning) {
                if (m_cancel.load()) {
                    proc.kill();
                    proc.waitForFinished(3000);
                    emit finished(false, "Cancelled");
                    return false;
                }
                proc.waitForReadyRead(100);
                buf += proc.readAllStandardOutput();
                QByteArray newErr = proc.readAllStandardError();
                errBuf     += newErr;
                errParseBuf += newErr;

                if (m_verbose) {
                    int nl;
                    while ((nl = errParseBuf.indexOf('\n')) >= 0) {
                        QByteArray line = errParseBuf.left(nl).trimmed();
                        errParseBuf.remove(0, nl + 1);
                        if (!line.isEmpty())
                            emit progress(QString::fromStdString(label), -3, 0,
                                          QString::fromUtf8(line));
                    }
                }

                int nl;
                while ((nl = buf.indexOf('\n')) >= 0) {
                    QByteArray line = buf.left(nl).trimmed();
                    buf.remove(0, nl + 1);
                    int eq = line.indexOf('=');
                    if (eq < 0) continue;
                    QByteArray key = line.left(eq), val = line.mid(eq + 1);
                    if (key == "out_time_us" && totalUs > 0) {
                        bool ok; int64_t t = val.toLongLong(&ok);
                        if (ok && t > 0) outTimeUs = t;
                    } else if (key == "speed") {
                        if (!val.isEmpty() && val.back() == 'x') val.chop(1);
                        bool ok; double s = val.toDouble(&ok);
                        if (ok && s > 0.001) {
                            if (speedHist.size() >= 8) speedHist.erase(speedHist.begin());
                            speedHist.push_back(s);
                        }
                    }
                    if (totalUs > 0) {
                        int pct = (int)(std::min(1.0, (double)outTimeUs / (double)totalUs) * 100);
                        double avgSpeed = 0.0;
                        for (double sv : speedHist) avgSpeed += sv;
                        if (!speedHist.empty()) avgSpeed /= (double)speedHist.size();
                        int eta = avgSpeed > 0.001
                            ? (int)((totalUs - outTimeUs) / (avgSpeed * 1e6)) : 0;
                        emit progress(QString::fromStdString(label), scalePct(pct), eta, "");
                    }
                }
            }

            buf += proc.readAllStandardOutput();
            errBuf += proc.readAllStandardError();
            errParseBuf += proc.readAllStandardError();
            proc.waitForFinished(-1);

            if (m_verbose) {
                int nl;
                while ((nl = errParseBuf.indexOf('\n')) >= 0) {
                    QByteArray line = errParseBuf.left(nl).trimmed();
                    errParseBuf.remove(0, nl + 1);
                    if (!line.isEmpty())
                        emit progress(QString::fromStdString(label), -3, 0,
                                      QString::fromUtf8(line));
                }
                if (!errParseBuf.trimmed().isEmpty())
                    emit progress(QString::fromStdString(label), -3, 0,
                                  QString::fromUtf8(errParseBuf.trimmed()));
            }

            bool success = proc.exitStatus() == QProcess::NormalExit
                        && proc.exitCode() == 0;
            QString errMsg;
            if (!success && !errBuf.isEmpty()) {
                for (const QByteArray& ln : errBuf.split('\n')) {
                    QString s = QString::fromLatin1(ln).trimmed();
                    if (!s.isEmpty()) { errMsg = s; break; }
                }
            }
            emit progress(QString::fromStdString(label), success ? 100 : -1, 0, errMsg);
            return success;
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
    void progress(const QString& label, int pct, int etaSecs, const QString& msg);
    void finished(bool ok, const QString& error);

private:
    Trip               m_trip;
    VideoOptions       m_opts;
    bool               m_verbose;
    std::atomic<bool>  m_cancel{false};
};

// ─── GpsExtractJob ────────────────────────────────────────────────────────────
GpsExtractJob::GpsExtractJob(const std::string& mid,
                             const std::string& tripId,
                             const std::string& manifestFile,
                             const std::string& exiftoolPath)
    : m_mid(mid), m_tripId(tripId), m_manifestFile(manifestFile), m_exiftoolPath(exiftoolPath)
{}

GpsExtractJob::~GpsExtractJob()
{
    if (m_syncWorker) m_syncWorker->cancel();
    if (m_thread2 && m_thread2->isRunning()) { m_thread2->quit(); m_thread2->wait(3000); }
    if (m_thread  && m_thread->isRunning())  { m_thread->quit();  m_thread->wait(3000); }
}

QString GpsExtractJob::description() const
{
    QString addr = m_mid.empty()
        ? QString::fromStdString(m_tripId)
        : QString::fromStdString(m_mid + ":" + m_tripId);
    return QString("GPS extract — %1").arg(addr);
}

void GpsExtractJob::start()
{
    m_state  = State::Running;
    m_pct    = -1;
    m_elapsed.start();
    m_status = "GPS extraction…";
    emit statusChanged(m_status);
    emit stageLog("▷ GPS extraction");
    addStep(m_steps, "GPS extraction");
    findStep(m_steps, "GPS extraction")->state = Job::StepInfo::StepState::Running;
    emit stepStarted("GPS extraction");

    auto* worker = new GpsExtractJobWorker(m_tripId, m_manifestFile, m_exiftoolPath);
    m_thread = new QThread(this);
    worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, worker, &GpsExtractJobWorker::start);
    connect(worker, &GpsExtractJobWorker::progress,
            this, [this](int done, int total) {
        m_pct    = total > 0 ? (done * 100 / total) / 2 : -1;   // 0–50 % for stage 1
        m_status = QString("Segment %1 / %2").arg(done).arg(total);
        int stepPct = total > 0 ? done * 100 / total : -1;
        if (auto* si = findStep(m_steps, "GPS extraction")) si->pct = stepPct;
        emit progressChanged(m_pct);
        emit statusChanged(m_status);
        emit stepProgress("GPS extraction", stepPct);
    }, Qt::QueuedConnection);
    connect(worker, &GpsExtractJobWorker::finished,
            this, [this](bool ok, QString err) {
        m_thread->quit();
        if (!ok) {
            m_state  = State::Failed;
            m_status = err;
            if (auto* si = findStep(m_steps, "GPS extraction"))
                si->state = Job::StepInfo::StepState::Failed;
            emit stageLog("✗ GPS extraction — " + err);
            emit stepDone("GPS extraction", false);
            emit progressChanged(m_pct);
            emit statusChanged(m_status);
            emit finished(false);
            return;
        }
        if (auto* si = findStep(m_steps, "GPS extraction")) {
            si->state = Job::StepInfo::StepState::Done;
            si->pct   = 100;
        }
        emit stageLog("✓ GPS extraction");
        emit stepDone("GPS extraction", true);
        startSyncPhase();
    }, Qt::QueuedConnection);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);

    m_thread->start();
}

void GpsExtractJob::startSyncPhase()
{
    m_pct    = 50;
    m_status = "Camera sync…";
    emit progressChanged(m_pct);
    emit statusChanged(m_status);
    emit stageLog("▷ Camera sync");
    addStep(m_steps, "Camera sync");
    findStep(m_steps, "Camera sync")->state = Job::StepInfo::StepState::Running;
    emit stepStarted("Camera sync");

    QString script = MapRenderJob::findScript("clops_sync_analyze.py");
    if (script.isEmpty()) {
        if (auto* si = findStep(m_steps, "Camera sync"))
            si->state = Job::StepInfo::StepState::Failed;
        emit stageLog("⚠ Camera sync skipped — clops_sync_analyze.py not found");
        emit stepDone("Camera sync", false);
        m_state  = State::Done;
        m_pct    = 100;
        m_status = "Done";
        emit progressChanged(100);
        emit statusChanged(m_status);
        emit finished(true);
        return;
    }

    QString tripAddr = QString::fromStdString(
        m_mid.empty() ? m_tripId : m_mid + ":" + m_tripId).toUpper();
    QStringList args;
    args << tripAddr << "--all-segments" << "--write";

    m_syncWorker = new MapRenderWorker("python3", script, args);
    m_thread2    = new QThread(this);
    m_syncWorker->moveToThread(m_thread2);

    connect(m_thread2, &QThread::started, m_syncWorker, &MapRenderWorker::start);
    connect(m_syncWorker, &MapRenderWorker::statusMessage,
            this, [this](QString msg) { emit logLine(msg); }, Qt::QueuedConnection);
    connect(m_syncWorker, &MapRenderWorker::finished,
            this, [this](bool syncOk, QString) {
        m_thread2->quit();
        emit stageLog(syncOk
            ? "✓ Camera sync"
            : "⚠ Camera sync failed — GPS data preserved");
        if (auto* si = findStep(m_steps, "Camera sync")) {
            si->state = syncOk ? Job::StepInfo::StepState::Done
                               : Job::StepInfo::StepState::Failed;
            if (syncOk) si->pct = 100;
        }
        emit stepDone("Camera sync", syncOk);
        m_state  = State::Done;
        m_pct    = 100;
        m_status = "Done — " + fmtElapsed(m_elapsed.elapsed());
        emit progressChanged(100);
        emit statusChanged(m_status);
        emit finished(true);   // sync failure is a warning, not a job failure
    }, Qt::QueuedConnection);
    connect(m_thread2, &QThread::finished, this, [this]() {
        if (m_syncWorker) { m_syncWorker->deleteLater(); m_syncWorker = nullptr; }
    });

    m_thread2->start();
}

void GpsExtractJob::cancel()
{
    if (m_syncWorker) m_syncWorker->cancel();
    // GPS stage 1 is not safely interruptible.
}

// ─── MapRenderJob ─────────────────────────────────────────────────────────────
MapRenderJob::MapRenderJob(const QString& title, const QString& scriptName,
                           const QString& manifestFile, const QString& tripId,
                           const QString& outputPath, int width, int height,
                           const QStringList& extraArgs)
    : m_title(title), m_scriptName(scriptName), m_manifestFile(manifestFile),
      m_tripId(tripId), m_outputPath(outputPath),
      m_width(width), m_height(height), m_extraArgs(extraArgs)
{}

MapRenderJob::~MapRenderJob()
{
    if (m_worker) m_worker->cancel();
    if (m_thread && m_thread->isRunning()) { m_thread->quit(); m_thread->wait(); }
}

QString MapRenderJob::description() const { return m_title; }

QString MapRenderJob::findScript(const QString& scriptName)
{
    QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/scripts/" + scriptName,
        appDir + "/../scripts/" + scriptName,
        appDir + "/../share/camclops/scripts/" + scriptName,
        appDir + "/" + scriptName,
    };
    for (const QString& p : candidates) {
        if (QFile::exists(p)) return QFileInfo(p).canonicalFilePath();
    }
    return {};
}

void MapRenderJob::start()
{
    m_state  = State::Running;
    m_pct    = 0;
    m_elapsed.start();
    m_status = "Starting…";
    emit statusChanged(m_status);

    QString script = findScript(m_scriptName);
    if (script.isEmpty()) {
        m_state  = State::Failed;
        m_status = m_scriptName + " not found";
        emit statusChanged(m_status);
        emit finished(false);
        return;
    }

    QStringList extraArgs = m_extraArgs;
    if (!extraArgs.contains("--ffmpeg")) {
        CamClops::ConfigManager cfg;
        cfg.loadSettings();
        std::string fp = cfg.getSettings().ffmpegPath;
        if (!fp.empty()) extraArgs << "--ffmpeg" << QString::fromStdString(fp);
    }

    QStringList args;
    args << "--manifest" << m_manifestFile
         << "--trip"     << m_tripId
         << "--output"   << m_outputPath
         << "--width"    << QString::number(m_width)
         << "--height"   << QString::number(m_height)
         << "--fps"      << "30"
         << extraArgs;

    m_worker = new MapRenderWorker("python3", script, args);
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &MapRenderWorker::start);
    connect(m_worker, &MapRenderWorker::progress,
            this, [this](int pct, QString timeStr, QString speedStr) {
        m_pct = pct;
        QString s = QString("%1%").arg(pct);
        if (!timeStr.isEmpty())  s += "  [" + timeStr + "]";
        if (!speedStr.isEmpty()) s += "  " + speedStr;
        m_status = s;
        emit progressChanged(pct);
        emit statusChanged(m_status);
    }, Qt::QueuedConnection);
    connect(m_worker, &MapRenderWorker::statusMessage,
            this, [this](QString msg) {
        m_status = msg;
        emit statusChanged(msg);
        emit logLine(msg);
    }, Qt::QueuedConnection);
    connect(m_worker, &MapRenderWorker::finished,
            this, [this](bool ok, QString err) {
        // Write the output path to the manifest before emitting finished.
        // This is guaranteed regardless of whether the originating dialog is
        // still open — the in-memory / UI update is handled by any connected
        // finished() slots in the dialog, but the manifest is ours to own.
        if (ok) {
            static auto keyFor = [](const QString& script) -> std::string {
                if (script == "clops_maprender.py")  return "mapVideos";
                if (script == "clops_dashboard.py")  return "dashVideos";
                if (script == "clops_hud.py")        return "hudVideos";
                return {};
            };
            std::string key = keyFor(m_scriptName);
            std::string p   = m_outputPath.toStdString();
            if (!key.empty() && !p.empty()) {
                try {
                    std::ifstream ifs(m_manifestFile.toStdString());
                    json root;  ifs >> root;  ifs.close();
                    for (auto& jt : root["trips"]) {
                        if (jt.value("id", "") != m_tripId.toStdString()) continue;
                        auto& arr = jt[key];
                        if (!arr.is_array()) arr = json::array();
                        bool dup = false;
                        for (const auto& v : arr)
                            if (v.get<std::string>() == p) { dup = true; break; }
                        if (!dup) arr.push_back(p);
                        break;
                    }
                    std::ofstream ofs(m_manifestFile.toStdString());
                    ofs << root.dump(2);
                } catch (...) {}
            }
        }
        m_state  = ok ? State::Done : (err == "Cancelled." ? State::Cancelled : State::Failed);
        m_pct    = ok ? 100 : m_pct;
        m_status = ok ? "Done — " + fmtElapsed(m_elapsed.elapsed()) : err;
        emit progressChanged(m_pct);
        emit statusChanged(m_status);
        emit finished(ok);
        m_thread->quit();
    }, Qt::QueuedConnection);
    connect(m_thread, &QThread::finished, this, [this]() {
        if (m_worker) { m_worker->deleteLater(); m_worker = nullptr; }
    });

    m_thread->start();
}

void MapRenderJob::cancel()
{
    if (m_worker) m_worker->cancel();
}

// ─── SyncAnalysisJob ──────────────────────────────────────────────────────────
SyncAnalysisJob::SyncAnalysisJob(const std::string& mid,
                                 const std::string& tripId)
    : m_mid(mid), m_tripId(tripId)
{}

SyncAnalysisJob::~SyncAnalysisJob()
{
    if (m_worker) m_worker->cancel();
    if (m_thread && m_thread->isRunning()) { m_thread->quit(); m_thread->wait(); }
}

QString SyncAnalysisJob::description() const
{
    QString addr = m_mid.empty()
        ? QString::fromStdString(m_tripId)
        : QString::fromStdString(m_mid + ":" + m_tripId);
    return QString("Sync analysis — %1").arg(addr);
}

void SyncAnalysisJob::start()
{
    m_state  = State::Running;
    m_pct    = -1;
    m_elapsed.start();
    m_status = "Analyzing…";
    emit statusChanged(m_status);

    QString script = MapRenderJob::findScript("clops_sync_analyze.py");
    if (script.isEmpty()) {
        m_state  = State::Failed;
        m_status = "clops_sync_analyze.py not found";
        emit statusChanged(m_status);
        emit finished(false);
        return;
    }

    // clops_sync_analyze.py takes MID:TID as a positional arg and finds
    // the manifest itself via the config index — no --manifest or --trip flags.
    QString tripAddr = QString::fromStdString(
        m_mid.empty() ? m_tripId : m_mid + ":" + m_tripId);
    CamClops::ConfigManager cfg;
    cfg.loadSettings();
    const auto& s = cfg.getSettings();

    QStringList args;
    args << tripAddr.toUpper()
         << "--all-segments"
         << "--write";
    if (!s.ffmpegPath.empty())
        args << "--ffmpeg" << QString::fromStdString(s.ffmpegPath);
    if (!s.exiftoolPath.empty())
        args << "--exiftool" << QString::fromStdString(s.exiftoolPath);

    m_worker = new MapRenderWorker("python3", script, args);
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &MapRenderWorker::start);
    connect(m_worker, &MapRenderWorker::statusMessage,
            this, [this](QString msg) {
        m_status = msg;
        emit statusChanged(msg);
        emit logLine(msg);
    }, Qt::QueuedConnection);
    connect(m_worker, &MapRenderWorker::finished,
            this, [this](bool ok, QString err) {
        m_state  = ok ? State::Done : (err == "Cancelled." ? State::Cancelled : State::Failed);
        m_pct    = ok ? 100 : m_pct;
        m_status = ok ? "Done — " + fmtElapsed(m_elapsed.elapsed()) : err;
        emit progressChanged(m_pct);
        emit statusChanged(m_status);
        emit finished(ok);
        m_thread->quit();
    }, Qt::QueuedConnection);
    connect(m_thread, &QThread::finished, this, [this]() {
        if (m_worker) { m_worker->deleteLater(); m_worker = nullptr; }
    });

    m_thread->start();
}

void SyncAnalysisJob::cancel()
{
    if (m_worker) m_worker->cancel();
}

// ─── CollageJob ───────────────────────────────────────────────────────────────
CollageJob::CollageJob(const Trip& trip, const VideoOptions& opts,
                       const std::string& mid)
    : m_trip(trip), m_opts(opts), m_mid(mid) {}

CollageJob::~CollageJob()
{
    if (m_worker) m_worker->cancel();
    if (m_thread && m_thread->isRunning()) { m_thread->quit(); m_thread->wait(); }
}

QString CollageJob::description() const
{
    QString addr = m_mid.empty()
        ? QString::fromStdString(m_trip.id)
        : QString::fromStdString(m_mid + ":" + m_trip.id);
    QString label = m_opts.basenameOverride.empty()
        ? QString::fromStdString(m_trip.date)
        : QString::fromStdString(m_opts.basenameOverride);
    return QString("Collage — %1  %2").arg(addr.toUpper()).arg(label);
}

void CollageJob::start()
{
    m_state  = State::Running;
    m_pct    = 0;
    m_status = "Starting…";
    emit statusChanged(m_status);

    m_elapsed.start();
    m_lastLabel = {};
    m_seenStages.clear();
    m_worker = new CollageWorker(m_trip, m_opts, m_opts.verbose);
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    // Human-readable names for the raw label strings from video_build.cpp.
    auto fmtStage = [](const QString& raw) -> QString {
        if (raw == "concat:Front")  return "Front - join segments";
        if (raw == "concat:Rear")   return "Rear - join segments";
        if (raw == "concat:Left")   return "Left - join segments";
        if (raw == "concat:Right")  return "Right - join segments";
        if (raw == "collage:4K")    return "Collage 4K";
        if (raw == "collage:1080p") return "Collage 1080p";
        return raw;
    };

    connect(m_thread, &QThread::started, m_worker, &CollageWorker::start);
    auto fmtSecs = [](int s) -> QString {
        if (s <= 0) return {};
        return s < 60 ? QString("%1s").arg(s)
                      : QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
    };
    // fmtElapsed() (free function) used for job-level done string below.

    connect(m_worker, &CollageWorker::progress,
            this, [this, fmtStage, fmtSecs](QString label, int pct, int eta, QString msg) {
        if (pct == -2 || pct == -3) {
            emit logLine(msg.isEmpty() ? label : msg);
            return;
        }
        QString stage = fmtStage(label);
        if (pct == 0) {
            if (!m_seenStages.contains(label)) {
                m_seenStages.append(label);
                emit stageLog("▷ " + stage);
                addStep(m_steps, stage);
                findStep(m_steps, stage)->state = Job::StepInfo::StepState::Running;
                findStep(m_steps, stage)->pct   = 0;
                emit stepStarted(stage);
            }
            m_lastLabel = stage;
            m_status    = stage;
            emit statusChanged(m_status);
        } else if (pct > 0 && pct < 100) {
            m_lastLabel = stage;
            m_pct    = pct;
            m_status = stage + QString("  %1%").arg(pct);
            QString etaStr     = fmtSecs(eta);
            QString elapsedStr = fmtSecs((int)(m_elapsed.elapsed() / 1000));
            if (!elapsedStr.isEmpty()) m_status += "  +" + elapsedStr;
            if (!etaStr.isEmpty())     m_status += "  ETA " + etaStr;
            if (auto* si = findStep(m_steps, stage)) si->pct = pct;
            emit progressChanged(pct);
            emit statusChanged(m_status);
            emit stepProgress(stage, pct);
        } else if (pct == 100) {
            m_lastLabel = stage;
            m_pct    = 100;
            m_status = stage + "  done";
            emit stageLog("✓ " + stage);
            if (auto* si = findStep(m_steps, stage)) {
                si->state = Job::StepInfo::StepState::Done;
                si->pct   = 100;
            }
            emit stepDone(stage, true);
            emit progressChanged(100);
            emit statusChanged(m_status);
        } else if (pct == -1) {
            QString failed = m_lastLabel.isEmpty() ? stage : m_lastLabel;
            emit stageLog("✗ " + failed + (msg.isEmpty() ? "" : " — " + msg));
            if (auto* si = findStep(m_steps, failed))
                si->state = Job::StepInfo::StepState::Failed;
            emit stepDone(failed, false);
            m_lastLabel = {};
            m_status    = failed + (msg.isEmpty() ? "" : ":  " + msg);
            emit statusChanged(m_status);
        }
    }, Qt::QueuedConnection);
    connect(m_worker, &CollageWorker::finished,
            this, [this](bool ok, QString err) {
        m_state  = ok ? State::Done : (err == "Cancelled" ? State::Cancelled : State::Failed);
        m_pct    = ok ? 100 : m_pct;
        m_status = ok ? "Done — " + fmtElapsed(m_elapsed.elapsed()) : err;
        emit progressChanged(m_pct);
        emit statusChanged(m_status);
        emit finished(ok);
        m_thread->quit();
    }, Qt::QueuedConnection);
    connect(m_thread, &QThread::finished, this, [this]() {
        if (m_worker) { m_worker->deleteLater(); m_worker = nullptr; }
    });

    m_thread->start();
}

void CollageJob::cancel()
{
    if (m_worker) m_worker->cancel();
}

// ─── ScanJobWorker ────────────────────────────────────────────────────────────
class ScanJobWorker : public QObject {
    Q_OBJECT
public:
    QString profileId;
public slots:
    void start(const QString& path) {
        try {
            ConfigManager config;
            config.loadSettings();
            std::string stdPath = path.toStdString();

            CameraProfile profile;
            if (!config.lookupManifestFilePath(stdPath).empty()) {
                profile = config.getManifestProfile(stdPath);
            } else {
                if (!profileId.isEmpty()) {
                    auto s = config.getSettings();
                    s.activeProfileId = profileId.toStdString();
                    config.applySettings(s);
                }
                profile = config.getCameraProfile();
            }

            TripDetection td;
            auto trips = td.detectTrips(
                stdPath, profile,
                config.getGapThreshold(),
                config.getFuzzyWindow(),
                config.getFfprobePath(),
                "exiftool",
                [this](int done, int total) { emit segProgress(done, total); },
                [this](int done, int total) { emit gpsProgress(done, total); });
            config.saveTripCache(stdPath, trips, profile);
            emit finished(true, "");
        } catch (const std::exception& ex) {
            emit finished(false, QString::fromLatin1(ex.what()));
        } catch (...) {
            emit finished(false, "Unknown error during scan");
        }
    }
signals:
    void segProgress(int done, int total);
    void gpsProgress(int done, int total);
    void finished(bool ok, const QString& error);
};

// ─── ManifestScanJob ──────────────────────────────────────────────────────────
ManifestScanJob::ManifestScanJob(const QString& sourcePath, const QString& profileId)
    : m_sourcePath(sourcePath), m_profileId(profileId)
{}

ManifestScanJob::~ManifestScanJob()
{
    if (m_thread && m_thread->isRunning()) { m_thread->quit(); m_thread->wait(); }
}

QString ManifestScanJob::description() const
{
    return "Scan: " + QFileInfo(m_sourcePath).fileName();
}

void ManifestScanJob::start()
{
    m_state  = State::Running;
    m_pct    = -1;
    m_elapsed.start();
    m_status = "Scanning…";
    emit progressChanged(-1);
    emit statusChanged(m_status);

    auto* worker = new ScanJobWorker;
    worker->profileId = m_profileId;
    m_thread = new QThread(this);
    worker->moveToThread(m_thread);

    QString path = m_sourcePath;
    connect(m_thread, &QThread::started, worker, [worker, path]() { worker->start(path); });
    connect(worker, &ScanJobWorker::segProgress, this, [this](int done, int total) {
        m_pct    = total > 0 ? done * 50 / total : -1;
        m_status = QString("Segment %1 / %2").arg(done).arg(total);
        emit progressChanged(m_pct);
        emit statusChanged(m_status);
    }, Qt::QueuedConnection);
    connect(worker, &ScanJobWorker::gpsProgress, this, [this](int done, int total) {
        m_pct    = 50 + (total > 0 ? done * 50 / total : 0);
        m_status = QString("GPS coords — trip %1 / %2").arg(done).arg(total);
        emit progressChanged(m_pct);
        emit statusChanged(m_status);
    }, Qt::QueuedConnection);
    connect(worker, &ScanJobWorker::finished, this, [this](bool ok, QString err) {
        if (ok) {
            ConfigManager cfg;
            cfg.loadSettings();
            std::string stdPath = m_sourcePath.toStdString();
            for (const auto& e : cfg.loadManifestIndex())
                if (e.path == stdPath) { m_entry = e; break; }
        }
        m_state  = ok ? State::Done : State::Failed;
        m_pct    = ok ? 100 : m_pct;
        if (ok) {
            QString elapsed = fmtElapsed(m_elapsed.elapsed());
            m_status = m_entry.tripCount > 0
                ? QString("Done — %1  %2 trip%3").arg(elapsed).arg(m_entry.tripCount)
                                                  .arg(m_entry.tripCount == 1 ? "" : "s")
                : "Done — " + elapsed;
        } else {
            m_status = err;
        }
        emit progressChanged(m_pct);
        emit statusChanged(m_status);
        emit finished(ok);
        m_thread->quit();
    }, Qt::QueuedConnection);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);

    m_thread->start();
}

void ManifestScanJob::cancel()
{
    // Scan is not safely interruptible.
}

// ─── JobQueue ─────────────────────────────────────────────────────────────────
JobQueue& JobQueue::instance()
{
    static JobQueue q;
    return q;
}

void JobQueue::enqueue(Job* job)
{
    m_jobs.append(job);
    // Wire finished → advance queue at enqueue time, not lazily in tryStartNext.
    // This guarantees the connection exists regardless of queue state when the
    // job eventually completes.
    connect(job, &Job::finished, this, [this, job](bool ok) {
        emit jobFinished(job, ok);
        tryStartNext();
    });
    emit jobAdded(job);
    tryStartNext();
}

void JobQueue::removeJob(Job* job)
{
    if (job->state() == Job::State::Running) return;
    m_jobs.removeOne(job);
    job->deleteLater();
}

void JobQueue::clearFinished()
{
    auto it = std::remove_if(m_jobs.begin(), m_jobs.end(), [](Job* j) {
        return j->state() == Job::State::Done
            || j->state() == Job::State::Failed
            || j->state() == Job::State::Cancelled;
    });
    for (auto jt = it; jt != m_jobs.end(); ++jt)
        (*jt)->deleteLater();
    m_jobs.erase(it, m_jobs.end());
}

void JobQueue::tryStartNext()
{
    for (Job* j : m_jobs) {
        if (j->state() == Job::State::Running) return;
    }
    for (Job* j : m_jobs) {
        if (j->state() == Job::State::Queued) {
            m_inhibitor.acquire();
            j->start();
            return;
        }
    }
    // No running and no queued — queue is idle, release inhibitor.
    m_inhibitor.release();
}

#include "JobQueue.moc"
// SN: 00117
