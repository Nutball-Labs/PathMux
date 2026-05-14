// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "JobQueueMonitor.h"
#include "JobQueue.h"
#include "json.hpp"
#include <QFile>
#include <QTimer>
#include <QDateTime>

using json = nlohmann::json;

JobQueueMonitor::JobQueueMonitor(const QString& statusFile, QObject* parent)
    : QObject(parent), m_path(statusFile)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(DEBOUNCE_MS);
    connect(m_timer, &QTimer::timeout, this, &JobQueueMonitor::doWrite);

    JobQueue& q = JobQueue::instance();

    // Wire every signal that indicates queue or job state has changed.
    connect(&q, &JobQueue::jobAdded,    this, &JobQueueMonitor::scheduleWrite);
    connect(&q, &JobQueue::jobFinished, this, [this](Job*, bool) { scheduleWrite(); });

    // Per-job progress / status / steps: connected when a job is added.
    connect(&q, &JobQueue::jobAdded, this, [this](Job* job) {
        connect(job, &Job::progressChanged, this, [this](int)           { scheduleWrite(); });
        connect(job, &Job::statusChanged,   this, [this](const QString&){ scheduleWrite(); });
        connect(job, &Job::stepStarted,     this, [this](const QString&){ scheduleWrite(); });
        connect(job, &Job::stepProgress,    this, [this](const QString&, int){ scheduleWrite(); });
        connect(job, &Job::stepDone,        this, [this](const QString&, bool){ scheduleWrite(); });
    });

    // Write immediately on construction so stale data from a previous run is cleared.
    doWrite();
}

void JobQueueMonitor::scheduleWrite()
{
    if (!m_timer->isActive())
        m_timer->start();
}

void JobQueueMonitor::doWrite()
{
    json root;
    root["updated"] = QDateTime::currentDateTime()
                          .toString(Qt::ISODate).toStdString();

    json jobs = json::array();
    for (const Job* job : JobQueue::instance().jobs()) {
        json j;
        j["desc"]   = job->description().toStdString();
        j["pct"]    = job->pct();          // 0-100 or -1 for indeterminate

        QString stateStr;
        switch (job->state()) {
            case Job::State::Queued:    stateStr = "queued";    break;
            case Job::State::Running:   stateStr = "running";   break;
            case Job::State::Done:      stateStr = "done";      break;
            case Job::State::Failed:    stateStr = "failed";    break;
            case Job::State::Cancelled: stateStr = "cancelled"; break;
        }
        j["state"]  = stateStr.toStdString();
        j["status"] = job->statusText().toStdString();

        if (!job->steps().isEmpty()) {
            json steps = json::array();
            for (const auto& s : job->steps()) {
                json step;
                step["name"] = s.name.toStdString();
                QString ss;
                switch (s.state) {
                    case Job::StepInfo::StepState::Waiting: ss = "waiting"; break;
                    case Job::StepInfo::StepState::Running: ss = "running"; break;
                    case Job::StepInfo::StepState::Done:    ss = "done";    break;
                    case Job::StepInfo::StepState::Failed:  ss = "failed";  break;
                }
                step["state"] = ss.toStdString();
                step["pct"]   = s.pct;
                steps.push_back(step);
            }
            j["steps"] = steps;
        }

        jobs.push_back(j);
    }
    root["jobs"] = jobs;

    QFile f(m_path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        f.write(QByteArray::fromStdString(root.dump(2)));
}
// SN: 00117
