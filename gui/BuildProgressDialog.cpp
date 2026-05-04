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
#include <QTextEdit>
#include <QThread>
#include <QScrollBar>
#include <QProcess>
#include <atomic>
#include <set>
#include <cctype>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

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

    void cancel() { m_cancel.store(true); }

public slots:
    void start() {
        VideoBuilder builder;

        // Route progress signals through the Qt signal/slot system.
        builder.progressCallback = [this](const std::string& label, int pct, int eta) {
            emit progress(QString::fromStdString(label), pct, eta, "");
        };

        // Use QProcess instead of fork()/system() — fork() is unsafe from a
        // QThread (multithreaded Qt app), and the system() fallback blocks
        // indefinitely with no progress updates.
        builder.ffmpegRunner = [this](const std::string& cmd,
                                      const std::string& label,
                                      int totalSecs) -> bool {
            // Add -loglevel error (keeps errors visible) and -progress pipe:1
            // (machine-readable progress to stdout). -loglevel error is used
            // instead of -loglevel quiet so that failure messages appear in
            // stderr and can be shown in the dialog.
            std::string fullCmd = cmd;
            {
                auto pos = fullCmd.find(' ');
                if (pos != std::string::npos)
                    fullCmd.insert(pos, " -loglevel error -progress pipe:1");
            }

            int64_t totalUs   = (int64_t)totalSecs * 1000000LL;
            int64_t outTimeUs = 0;
            // Rolling average over last 8 speed samples to smooth early-encode
            // warmup spikes (VP9/HEVC init can report 0.05x for the first few
            // seconds, projecting absurd ETAs for short clips).
            std::vector<double> speedHist;
            speedHist.reserve(8);

            // Emit cmd as a debug message (pct=-2 sentinel, etaSecs unused,
            // msg = the actual command line being run).
            emit progress(QString::fromStdString(label), -2, 0,
                          QString::fromStdString(fullCmd));
            emit progress(QString::fromStdString(label), 0, 0, "");

            QProcess proc;
            // Read stdout for -progress key=value output; stderr separately
            // for error messages.
            proc.setReadChannel(QProcess::StandardOutput);
#ifdef _WIN32
            // setNativeArguments bypasses Qt's argument escaping so that the
            // embedded quotes in fullCmd (ffmpeg path, file paths) are passed
            // verbatim to cmd.exe instead of being mangled with \" sequences.
            proc.setProgram("cmd.exe");
            proc.setNativeArguments("/c " + QString::fromStdString(fullCmd));
            proc.start();
#elif defined(__APPLE__)
            // macOS GUI apps launch with a minimal PATH (/usr/bin:/bin …) that
            // excludes Homebrew (/usr/local/bin on Intel, /opt/homebrew/bin on
            // Apple Silicon).  Prepend both so "ffmpeg" resolves regardless of
            // which Homebrew prefix is in use.  An explicit ffmpegPath in prefs
            // takes precedence because the command already contains the full path.
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
                emit progress(QString::fromStdString(label), -1, 0,
                              "Process failed to start");
                return false;
            }

            QByteArray buf;
            QByteArray errBuf;
            QByteArray errParseBuf;   // consumed line-by-line for verbose emit
            while (proc.state() != QProcess::NotRunning) {
                if (m_cancel.load()) {
                    proc.kill();
                    proc.waitForFinished(3000);
                    emit finished(false, "Cancelled");
                    return false;
                }
                proc.waitForReadyRead(100);
                buf += proc.readAllStandardOutput();
                // Drain stderr each iteration — if the stderr pipe fills up
                // ffmpeg blocks writing to it and the process never exits.
                QByteArray newErr = proc.readAllStandardError();
                errBuf     += newErr;
                errParseBuf += newErr;

                // Verbose: emit complete stderr lines in real time.
                if (m_opts.verbose) {
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
                        emit progress(QString::fromStdString(label), pct, eta, "");
                    }
                }
            }

            // Drain remaining stdout and collect stderr for error reporting.
            buf += proc.readAllStandardOutput();
            QByteArray finalErr = proc.readAllStandardError();
            errBuf     += finalErr;
            errParseBuf += finalErr;
            QByteArray& errOut = errBuf;
            proc.waitForFinished(-1);

            // Flush any remaining verbose stderr lines (no trailing newline case).
            if (m_opts.verbose) {
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

            // Build an error message from stderr (first non-empty line).
            QString errMsg;
            if (!success && !errOut.isEmpty()) {
                for (const QByteArray& ln : errOut.split('\n')) {
                    QString s = QString::fromLatin1(ln).trimmed();
                    if (!s.isEmpty()) { errMsg = s; break; }
                }
            }

            // Always emit final state so the dialog shows success or failure
            // even when ffmpeg exited before producing any progress output.
            emit progress(QString::fromStdString(label),
                          success ? 100 : -1, 0, errMsg);

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
    // msg: non-empty on pct==-1 (ffmpeg stderr) or pct==-2 (cmd being run)
    void progress(const QString& label, int pct, int etaSecs,
                  const QString& msg);
    void finished(bool ok, const QString& error);

private:
    Trip               m_trip;
    VideoOptions       m_opts;
    std::atomic<bool>  m_cancel{false};
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
    setMinimumWidth(560);

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

    // Output directory — show where files will be written so the user can
    // find them.  If outputDir is empty the build uses "." (current dir).
    {
        std::string outDir = opts.outputDir.empty() ? "." : opts.outputDir;
        // Resolve "." to an absolute path so the user sees the real location.
        if (outDir == ".") {
#ifdef _WIN32
            char buf[MAX_PATH] = {};
            if (GetCurrentDirectoryA(MAX_PATH, buf))
                outDir = buf;
#else
            char buf[4096] = {};
            if (getcwd(buf, sizeof(buf)))
                outDir = buf;
#endif
        }
        m_outputDirLabel = new QLabel(
            QString("Output: <tt>%1</tt>")
                .arg(QString::fromStdString(outDir).toHtmlEscaped()),
            this);
        m_outputDirLabel->setStyleSheet("font-size: 8pt;");   /* no color — inherits palette */
        m_outputDirLabel->setWordWrap(true);
        vlay->addWidget(m_outputDirLabel);
    }

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

    // Verbose output log — only shown when opts.verbose is set.
    if (m_opts.verbose) {
        auto* logLabel = new QLabel("Build Output:", this);
        logLabel->setStyleSheet("font-size: 8pt; font-weight: bold;");
        vlay->addWidget(logLabel);

        m_verboseLog = new QTextEdit(this);
        m_verboseLog->setReadOnly(true);
        m_verboseLog->setFont(QFont("monospace", 7));
        m_verboseLog->setMinimumHeight(140);
        m_verboseLog->setMaximumHeight(220);
        vlay->addWidget(m_verboseLog);
    }

    // Final status line (shows "Build complete" or error)
    m_finalLabel = new QLabel("Waiting for first stage...", this);
    m_finalLabel->setStyleSheet("font-style: italic;");   /* no color — inherits palette */
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
        "  border: 1px solid #888888;"   /* visible on both light and dark themes */
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
    row.status->setStyleSheet("font-size: 8pt;");   /* no color — inherits palette */

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

    m_worker = new BuildWorker(m_trip, m_opts);
    QThread* thread = new QThread(this);
    m_worker->moveToThread(thread);

    connect(thread,   &QThread::started,      m_worker, &BuildWorker::start);
    connect(m_worker, &BuildWorker::progress, this,     &BuildProgressDialog::onProgress,
            Qt::QueuedConnection);
    connect(m_worker, &BuildWorker::finished, this,     &BuildProgressDialog::onFinished);
    connect(m_worker, &BuildWorker::finished, thread,   &QThread::quit);
    connect(thread,   &QThread::finished,     m_worker, &QObject::deleteLater);
    connect(thread,   &QThread::finished,     thread,   &QObject::deleteLater);

    m_buildRunning = true;
    thread->start();
}

// ---------------------------------------------------------------------------
// Progress update from worker.
// Safe for concurrent stages: each label maps to its own row via m_rowIndex.
// ---------------------------------------------------------------------------
void BuildProgressDialog::onProgress(const QString& label, int pct, int etaSecs,
                                     const QString& msg)
{
    // pct == -3: verbose log line — append to log panel only, no footer update.
    if (pct == -3) {
        if (m_verboseLog && !msg.isEmpty()) {
            m_verboseLog->append(msg);
            auto* sb = m_verboseLog->verticalScrollBar();
            sb->setValue(sb->maximum());
        }
        return;
    }

    // pct == -2: command being run — show briefly in footer; also log it.
    if (pct == -2) {
        if (!msg.isEmpty()) {
            m_finalLabel->setText(msg);
            m_finalLabel->setStyleSheet("color: #999999; font-size: 7pt; font-style: italic;");
            if (m_verboseLog) {
                m_verboseLog->append(
                    QString("\n\u25ba %1\n%2")
                        .arg(stageDisplayName(label))
                        .arg(msg));
                auto* sb = m_verboseLog->verticalScrollBar();
                sb->setValue(sb->maximum());
            }
        }
        return;
    }

    // Add row on-the-fly for stages that weren't pre-populated (e.g. norm:N).
    if (!m_rowIndex.contains(label))
        addStageRow(label);

    auto it = m_rowIndex.find(label);
    if (it == m_rowIndex.end()) return;
    StageRow& row = m_rows[it.value()];

    // pct == -1: stage failed — show error indicator without marking done.
    if (pct < 0) {
        row.failed = true;
        row.bar->setRange(0, 100);
        row.bar->setValue(0);
        row.status->setText("\u2717");
        row.status->setStyleSheet("color: #cc0000; font-weight: bold;");
        QString errText = stageDisplayName(label) + " \u2014 failed";
        if (!msg.isEmpty())
            errText += ": " + msg;
        m_finalLabel->setText(errText);
        m_finalLabel->setStyleSheet("color: #cc0000; font-style: italic;");
        if (m_verboseLog && !msg.isEmpty()) {
            m_verboseLog->append(
                QString("<span style='color:#cc0000'>\u2717 %1</span>")
                    .arg(msg.toHtmlEscaped()));
            auto* sb = m_verboseLog->verticalScrollBar();
            sb->setValue(sb->maximum());
        }
        return;
    }

    // First signal for this stage: switch bar to indeterminate pulsing so the
    // user can see it is active even before we have real percentage data.
    if (!row.started) {
        row.started = true;
        row.bar->setRange(0, 0);   // range(0,0) = Qt indeterminate animation
        row.status->setText("Running\u2026");
        row.status->setStyleSheet("color: #4499ff; font-style: italic; font-size: 8pt;");
        // Scroll so the newly active row is visible.
        QScrollBar* sb = m_scrollArea->verticalScrollBar();
        sb->setValue(sb->maximum());
    }

    if (pct >= 100) {
        row.bar->setRange(0, 100);
        row.bar->setValue(100);
        row.status->setText("\u2713");
        row.status->setStyleSheet("color: #228b22; font-weight: bold;");
    } else if (pct > 0) {
        // Switch from indeterminate to determinate now that we have real data.
        if (row.bar->maximum() == 0)
            row.bar->setRange(0, 100);
        row.bar->setValue(pct);
        if (etaSecs > 0) {
            if (etaSecs >= 60)
                row.status->setText(QString("%1m %2s")
                    .arg(etaSecs / 60).arg(etaSecs % 60, 2, 10, QChar('0')));
            else
                row.status->setText(QString("%1s").arg(etaSecs));
            row.status->setStyleSheet("font-size: 8pt;");   /* no color — inherits palette */
        }
        m_finalLabel->setText(stageDisplayName(label) + "\u2026");
        m_finalLabel->setStyleSheet("font-style: italic;");   /* no color — inherits palette */
    } else {
        // pct == 0: stage just activated (bar already pulsing from above).
        m_finalLabel->setText(stageDisplayName(label) + "\u2026");
        m_finalLabel->setStyleSheet("font-style: italic;");   /* no color — inherits palette */
    }
}

// ---------------------------------------------------------------------------
// Build finished
// ---------------------------------------------------------------------------
void BuildProgressDialog::onFinished(bool ok, const QString& error)
{
    if (ok) {
        for (StageRow& row : m_rows) {
            if (row.bar->value() >= 100) continue;  // already marked done
            if (row.failed) continue;               // already showing error
            if (!row.started) {
                // Pre-populated but never activated — stage was skipped.
                row.bar->setRange(0, 100);
                row.bar->setValue(0);
                row.status->setText("\u2014");   // em-dash = skipped
                row.status->setStyleSheet("color: #909090;");
            } else {
                // Started but no final 100% signal (e.g. totalSecs==0 path).
                row.bar->setRange(0, 100);
                row.bar->setValue(100);
                row.status->setText("\u2713");
                row.status->setStyleSheet("color: #228b22; font-weight: bold;");
            }
        }
        // If any stage explicitly failed, say so in the footer.
        bool anyFailed = false;
        for (const StageRow& row : m_rows)
            if (row.failed) { anyFailed = true; break; }

        if (anyFailed) {
            m_finalLabel->setText("\u26a0  Completed with errors — check \u2717 rows above");
            m_finalLabel->setStyleSheet("color: #b35900; font-weight: bold;");
        } else {
            m_finalLabel->setText("\u2713  Build complete.");
            m_finalLabel->setStyleSheet("color: #228b22; font-weight: bold;");
        }
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

    m_buildRunning = false;
    m_closeBtn->setEnabled(true);
    emit buildComplete(ok);
}

void BuildProgressDialog::closeEvent(QCloseEvent* event)
{
    if (m_buildRunning && m_worker) {
        m_worker->cancel();
        // Let the worker finish cancelling; onFinished() will clear m_buildRunning.
        // Accept now — the window closes immediately, the worker cleans up on its thread.
    }
    event->accept();
}

#include "BuildProgressDialog.moc"
// SN: 00106
