// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QVector>
#include <QString>
#include <QStringList>
#include "trip_detection.hpp"
#include "video_build.hpp"
#include "SleepInhibitor.h"

class QThread;

// ─── Job ──────────────────────────────────────────────────────────────────────
// Abstract base for all queue-able work items.
// Concrete subclasses override description(), start(), and cancel().
// Signals are emitted from the main thread (via Qt::QueuedConnection from workers).
class Job : public QObject {
    Q_OBJECT
public:
    enum class State { Queued, Running, Done, Failed, Cancelled };

    struct StepInfo {
        QString name;
        enum class StepState { Waiting, Running, Done, Failed } state = StepState::Waiting;
        int pct = -1;
    };

    ~Job() override = default;
    virtual QString description() const = 0;
    virtual void start()  = 0;
    virtual void cancel() = 0;   // safe to call from any thread
    State   state()      const { return m_state; }
    int     pct()        const { return m_pct;   }   // 0-100; -1 = indeterminate
    QString statusText() const { return m_status; }
    const QVector<StepInfo>& steps() const { return m_steps; }
signals:
    void progressChanged(int pct);
    void statusChanged(const QString& msg);
    void logLine(const QString& line);       // verbose ffmpeg/script output
    void stageLog(const QString& line);      // one line per completed stage (✓/✗)
    void stepStarted(const QString& stepName);            // step begins
    void stepProgress(const QString& stepName, int pct);  // -1 = indeterminate
    void stepDone(const QString& stepName, bool ok);      // step finished
    void finished(bool ok);
protected:
    State   m_state  = State::Queued;
    int     m_pct    = -1;
    QString m_status;
    QVector<StepInfo> m_steps;
};

// ─── GpsExtractJob ────────────────────────────────────────────────────────────
class GpsExtractJob : public Job {
public:
    GpsExtractJob(const std::string& mid,
                  const std::string& tripId,
                  const std::string& manifestFile,
                  const std::string& exiftoolPath);
    ~GpsExtractJob() override;
    QString description() const override;
    void start()  override;
    void cancel() override;
private:
    void startSyncPhase();
    std::string            m_mid, m_tripId, m_manifestFile, m_exiftoolPath;
    QElapsedTimer          m_elapsed;
    QThread*               m_thread     = nullptr;
    QThread*               m_thread2    = nullptr;
    class MapRenderWorker* m_syncWorker = nullptr;
};

// ─── MapRenderJob ─────────────────────────────────────────────────────────────
// Runs a Python render script (clops_maprender.py, clops_dashboard.py, clops_hud.py).
// Locates the script via the standard search path next to the binary.
class MapRenderJob : public Job {
public:
    MapRenderJob(const QString& title,
                 const QString& scriptName,
                 const QString& manifestFile,
                 const QString& tripId,
                 const QString& outputPath,
                 int width, int height,
                 const QStringList& extraArgs = {});
    ~MapRenderJob() override;
    QString description() const override;
    void start()  override;
    void cancel() override;
    static QString findScript(const QString& scriptName);
private:
    QString       m_title, m_scriptName, m_manifestFile, m_tripId, m_outputPath;
    int           m_width, m_height;
    QStringList   m_extraArgs;
    QElapsedTimer m_elapsed;
    QThread*      m_thread = nullptr;
    class MapRenderWorker* m_worker = nullptr;
};

// ─── SyncAnalysisJob ──────────────────────────────────────────────────────────
// Runs clops_sync_analyze.py --manifest X --trip Y --all-segments --write.
// Uses the same MapRenderWorker/MapRenderJob::findScript infrastructure.
class SyncAnalysisJob : public Job {
public:
    SyncAnalysisJob(const std::string& mid,
                    const std::string& tripId);
    ~SyncAnalysisJob() override;
    QString description() const override;
    void start()  override;
    void cancel() override;
private:
    std::string   m_mid, m_tripId;
    QElapsedTimer m_elapsed;
    QThread*      m_thread = nullptr;
    class MapRenderWorker* m_worker = nullptr;
};

// ─── CollageJob ───────────────────────────────────────────────────────────────
class CollageJob : public Job {
public:
    CollageJob(const CamClops::Trip& trip, const VideoOptions& opts,
               const std::string& mid = {});
    ~CollageJob() override;
    QString description() const override;
    void start()  override;
    void cancel() override;
private:
    CamClops::Trip         m_trip;
    VideoOptions          m_opts;
    std::string           m_mid;
    QString               m_lastLabel;
    QStringList           m_seenStages;
    QElapsedTimer         m_elapsed;
    QThread*              m_thread = nullptr;
    class CollageWorker*  m_worker = nullptr;
};

// ─── ManifestScanJob ──────────────────────────────────────────────────────────
// Runs detectTrips() + saveTripCache() on a background thread.
// completedEntry() returns the resulting ManifestEntry; valid only after
// finished(true) has been emitted.
class ManifestScanJob : public Job {
public:
    ManifestScanJob(const QString& sourcePath, const QString& profileId = {});
    ~ManifestScanJob() override;
    QString description() const override;
    void start()  override;
    void cancel() override;
    CamClops::ManifestEntry completedEntry() const { return m_entry; }
private:
    QString                m_sourcePath;
    QString                m_profileId;
    CamClops::ManifestEntry m_entry;
    QElapsedTimer          m_elapsed;
    QThread*               m_thread      = nullptr;
    QString                m_currentStep;  // name of the open step sub-row
};

// ─── JobQueue ─────────────────────────────────────────────────────────────────
// Singleton. Owns all submitted jobs. Runs one job at a time.
// (Within-trip ffmpeg concats inside CollageJob remain concurrent.)
class JobQueue : public QObject {
    Q_OBJECT
public:
    static JobQueue& instance();
    void enqueue(Job* job);                 // takes ownership; starts if idle
    void removeJob(Job* job);              // remove one finished job and delete it
    const QVector<Job*>& jobs() const { return m_jobs; }
    void clearFinished();                   // remove Done/Failed/Cancelled jobs
signals:
    void jobAdded(Job* job);
    void jobFinished(Job* job, bool ok);
private:
    JobQueue() = default;
    void tryStartNext();
    QVector<Job*>  m_jobs;
    SleepInhibitor m_inhibitor;
};
// SN: 00117
