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
#include <QProcess>
#include <set>
#include <cctype>

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
    if (label == "collage:4K")                              return "Collage 4K";
    if (label == "collage:1080" || label == "collage:1080p") return "Collage 1080p";
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

        // Route progress signals through the Qt signal/slot system.
        builder.progressCallback = [this](const std::string& label, int pct, int eta) {
            emit progress(QString::fromStdString(label), pct, eta);
        };

        // Use QProcess instead of fork()/system() — fork() is unsafe from a
        // QThread (multithreaded Qt app), and the system() fallback blocks
        // indefinitely with no progress updates.
        builder.ffmpegRunner = [this](const std::string& cmd,
                                      const std::string& label,
                                      int totalSecs) -> bool {
            // Add -loglevel quiet and -progress pipe:1 (write to stdout).
            std::string fullCmd = cmd;
            {
                auto pos = fullCmd.find(' ');
                if (pos != std::string::npos)
                    fullCmd.insert(pos, " -loglevel quiet -progress pipe:1");
            }

            int64_t totalUs   = (int64_t)totalSecs * 1000000LL;
            int64_t outTimeUs = 0;
            double  speed     = 1.0;

            emit progress(QString::fromStdString(label), 0, 0);

            QProcess proc;
            proc.setReadChannel(QProcess::StandardOutput);
            proc.start("sh", {"-c", QString::fromStdString(fullCmd)});
            if (!proc.waitForStarted(10000)) return false;

            QByteArray buf;
            while (proc.state() != QProcess::NotRunning) {
                proc.waitForReadyRead(100);
                buf += proc.readAllStandardOutput();

                int nl;
                while ((nl = buf.indexOf('\n')) >= 0) {
                    QByteArray line = buf.left(nl).trimmed();
                    buf.remove(0, nl + 1);
                    int eq = line.indexOf('=');
                    if (eq < 0) continue;
                    QByteArray key = line.left(eq);
                    QByteArray val = line.mid(eq + 1);

                    if (key == "out_time_us" && totalUs > 0) {
                        bool ok;
                        int64_t t = val.toLongLong(&ok);
                        if (ok && t > 0) outTimeUs = t;
                    } else if (key == "speed") {
                        if (!val.isEmpty() && val.back() == 'x') val.chop(1);
                        bool ok;
                        double s = val.toDouble(&ok);
                        if (ok && s > 0.001) speed = s;
                    }

                    if (totalUs > 0) {
                        int pct = (int)(std::min(1.0, (double)outTimeUs / (double)totalUs) * 100);
                        int eta = speed > 0.001
                            ? (int)((totalUs - outTimeUs) / (speed * 1e6)) : 0;
                        emit progress(QString::fromStdString(label), pct, eta);
                    }
                }
            }

            // Drain any remaining stdout before reading exit status.
            buf += proc.readAllStandardOutput();
            proc.waitForFinished(-1);

            if (totalUs > 0)
                emit progress(QString::fromStdString(label), 100, 0);

            return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
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
// Add a stage row to the stack.  Skips silently if label already present.
// ---------------------------------------------------------------------------
void BuildProgressDialog::addStageRow(const QString& label)
{
    if (m_rowIndex.contains(label)) return;

    StageRow row;
    row.label = label;

    // Each row lives in a QFrame so we can draw a visible border around it.
    auto* rowFrame = new QFrame;
    rowFrame->setObjectName("stageRow");
    rowFrame->setStyleSheet(
        "QFrame#stageRow {"
        "  border: 1px solid #c8c8c8;"
        "  border-radius: 3px;"
        "}"
        "QFrame#stageRow QLabel { border: none; background: transparent; }"
    );

    auto* hbox = new QHBoxLayout(rowFrame);
    hbox->setContentsMargins(6, 3, 6, 3);
    hbox->setSpacing(8);

    auto* nameLabel = new QLabel(stageDisplayName(label), rowFrame);
    nameLabel->setMinimumWidth(180);
    nameLabel->setMaximumWidth(180);

    row.bar = new QProgressBar(rowFrame);
    row.bar->setRange(0, 100);
    row.bar->setValue(0);
    row.bar->setTextVisible(false);
    row.bar->setFixedHeight(16);

    row.status = new QLabel(rowFrame);
    row.status->setMinimumWidth(90);
    row.status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row.status->setStyleSheet("color: #505050; font-size: 8pt;");

    hbox->addWidget(nameLabel);
    hbox->addWidget(row.bar, 1);
    hbox->addWidget(row.status);

    // Insert before the trailing stretch (always at last position)
    m_stageLayout->insertWidget(m_stageLayout->count() - 1, rowFrame);

    int idx = m_rows.size();
    m_rows.append(row);
    m_rowIndex[label] = idx;

    // Scroll to bottom so the newest stage is visible
    QScrollBar* sb = m_scrollArea->verticalScrollBar();
    sb->setValue(sb->maximum());
}

// ---------------------------------------------------------------------------
// Pre-populate rows for all stages we know will run before the build starts.
// norm:N stages (inside collage) are added dynamically as they arrive.
// ---------------------------------------------------------------------------
void BuildProgressDialog::populateExpectedRows()
{
    // Which camera slots have at least one real segment?
    std::set<std::string> present;
    for (const auto& seg : m_trip.segments)
        for (const auto& [slot, path] : seg.cameras)
            if (!path.empty() && path != "-")
                present.insert(slot);

    auto addConcat = [&](bool opt, const char* slot, const char* cap) {
        if (opt && present.count(slot))
            addStageRow(QString("concat:") + cap);
    };
    addConcat(m_opts.buildFront,  "front", "Front");
    addConcat(m_opts.buildRear,   "rear",  "Rear");
    addConcat(m_opts.buildLeft,   "left",  "Left");
    addConcat(m_opts.buildRight,  "right", "Right");

    if (m_opts.buildCollage4K)   addStageRow("collage:4K");
    if (m_opts.buildCollage1080) addStageRow("collage:1080p");

    if (m_opts.buildAudio && !m_opts.audioExtractCamera.empty()) {
        std::string cam = m_opts.audioExtractCamera;
        cam[0] = (char)std::toupper((unsigned char)cam[0]);
        addStageRow(QString("audio:") + QString::fromStdString(cam));
    }
}

// ---------------------------------------------------------------------------
// Start the background build thread
// ---------------------------------------------------------------------------
void BuildProgressDialog::startBuild()
{
    // Show all expected stage rows before the first progress signal arrives.
    populateExpectedRows();

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
// Progress update from worker.
// Safe for concurrent stages: each label maps to its own row via m_rowIndex.
// ---------------------------------------------------------------------------
void BuildProgressDialog::onProgress(const QString& label, int pct, int etaSecs)
{
    // Add row on-the-fly for stages that weren't pre-populated (e.g. norm:N).
    if (!m_rowIndex.contains(label))
        addStageRow(label);

    auto it = m_rowIndex.find(label);
    if (it == m_rowIndex.end()) return;
    StageRow& row = m_rows[it.value()];
    row.bar->setValue(pct);

    if (pct >= 100) {
        row.status->setText("\u2713");
        row.status->setStyleSheet("color: #228b22; font-weight: bold;");
    } else {
        if (etaSecs > 0) {
            if (etaSecs >= 60)
                row.status->setText(QString("%1m %2s")
                    .arg(etaSecs / 60).arg(etaSecs % 60, 2, 10, QChar('0')));
            else
                row.status->setText(QString("%1s").arg(etaSecs));
            row.status->setStyleSheet("color: #505050; font-size: 8pt;");
        }
        // Update active-stage footer with the most recently seen in-flight label.
        m_finalLabel->setText(stageDisplayName(label) + "\u2026");
        m_finalLabel->setStyleSheet("color: #606060; font-style: italic;");
    }
}

// ---------------------------------------------------------------------------
// Build finished
// ---------------------------------------------------------------------------
void BuildProgressDialog::onFinished(bool ok, const QString& error)
{
    if (ok) {
        // Complete any row that didn't receive a final 100% update.
        for (StageRow& row : m_rows) {
            if (row.bar->value() < 100) {
                row.bar->setValue(100);
                row.status->setText("\u2713");
                row.status->setStyleSheet("color: #228b22; font-weight: bold;");
            }
        }
        m_finalLabel->setText("\u2713  Build complete.");
        m_finalLabel->setStyleSheet("color: #228b22; font-weight: bold;");
    } else {
        // Mark any in-flight row as errored.
        for (StageRow& row : m_rows) {
            if (row.bar->value() > 0 && row.bar->value() < 100) {
                row.status->setText("\u2717");
                row.status->setStyleSheet("color: #cc0000; font-weight: bold;");
            }
        }
        m_finalLabel->setText("\u2717  Error: " + error);
        m_finalLabel->setStyleSheet("color: #cc0000; font-weight: bold;");
    }

    m_closeBtn->setEnabled(true);
    emit buildComplete(ok);
}

#include "BuildProgressDialog.moc"
// SN: 00092
