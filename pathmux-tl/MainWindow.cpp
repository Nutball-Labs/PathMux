// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "MainWindow.h"
#include "MarkDialog.h"
#include "compat.hpp"
#include <QVideoWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QProgressBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QDialog>
#include <QPixmap>
#include <QPalette>
#include <QDir>
#include <QScrollBar>
#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QKeySequence>
#include <algorithm>
#include <cmath>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle("pathmux-tl \xe2\x80\x94 PathMux Timelapse Editor");
    setMinimumSize(900, 620);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* vlay = new QVBoxLayout(central);
    vlay->setSpacing(6);
    vlay->setContentsMargins(8, 8, 8, 8);

    // ── File row ─────────────────────────────────────────────────────────────
    auto* fileRow = new QHBoxLayout;
    auto* openBtn = new QPushButton("Open MP4\xe2\x80\xa6", this);
    m_inputEdit   = new QLineEdit(this);
    m_inputEdit->setPlaceholderText("Input collage MP4");
    m_inputEdit->setReadOnly(true);
    auto* outLbl = new QLabel("Output:", this);
    m_outputEdit = new QLineEdit(this);
    m_outputEdit->setPlaceholderText("Output MP4 path");
    auto* outBtn = new QPushButton("\xe2\x80\xa6", this);
    outBtn->setFixedWidth(28);
    fileRow->addWidget(openBtn);
    fileRow->addWidget(m_inputEdit, 3);
    fileRow->addWidget(outLbl);
    fileRow->addWidget(m_outputEdit, 3);
    fileRow->addWidget(outBtn);
    vlay->addLayout(fileRow);

    // ── Video player ─────────────────────────────────────────────────────────
    m_player      = new QMediaPlayer(this);
    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setMinimumHeight(280);
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Force the native X11 window to be created immediately rather than lazily.
    // Without this, the window is transparent until the first video frame arrives
    // and the apps behind show through (z-order bleed-through on Linux).
    m_videoWidget->setAttribute(Qt::WA_NativeWindow);
    m_videoWidget->winId();   // triggers real window creation right now
    // QPalette black background — setStyleSheet() does not reach native windows.
    QPalette vpal = m_videoWidget->palette();
    vpal.setColor(QPalette::Window, Qt::black);
    m_videoWidget->setPalette(vpal);
    m_videoWidget->setAutoFillBackground(true);
    m_player->setVideoOutput(m_videoWidget);
    vlay->addWidget(m_videoWidget, 1);

    // ── Transport row ─────────────────────────────────────────────────────────
    auto* trow = new QHBoxLayout;
    m_playBtn  = new QPushButton("\xe2\x96\xb6", this);
    m_playBtn->setFixedWidth(36);
    m_posLabel = new QLabel("--:-- / --:--", this);
    m_posLabel->setMinimumWidth(120);
    auto* hlpLbl = new QLabel(
        "Space=play/pause  \xe2\x80\x92  I=start timelapse  \xe2\x80\x92  "
        "O=stop timelapse  \xe2\x80\x92  right-click timeline for menu", this);
    hlpLbl->setStyleSheet("color: gray; font-size: 8pt;");
    trow->addWidget(m_playBtn);
    trow->addWidget(m_posLabel);
    trow->addStretch();
    trow->addWidget(hlpLbl);
    vlay->addLayout(trow);

    // ── Timeline + scrollbar ──────────────────────────────────────────────────
    m_timeline = new TimelineWidget(this);
    vlay->addWidget(m_timeline);

    m_timeScroll = new QScrollBar(Qt::Horizontal, this);
    m_timeScroll->setRange(0, 0);
    m_timeScroll->setSingleStep(1000);
    m_timeScroll->setPageStep(1000);
    m_timeScroll->setVisible(false);
    vlay->addWidget(m_timeScroll);

    // ── Marks summary ─────────────────────────────────────────────────────────
    m_marksSummary = new QLabel("No timelapse marks. Right-click the timeline to add one.", this);
    m_marksSummary->setStyleSheet("font-size: 8pt; color: gray;");
    vlay->addWidget(m_marksSummary);

    // ── Process row ───────────────────────────────────────────────────────────
    auto* prow   = new QHBoxLayout;
    m_processBtn = new QPushButton("Process\xe2\x80\xa6", this);
    m_processBtn->setEnabled(false);
    m_cancelBtn  = new QPushButton("Cancel", this);
    m_cancelBtn->setEnabled(false);
    m_progressBar= new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(false);
    m_statusLabel= new QLabel(this);
    m_statusLabel->setStyleSheet("font-size: 8pt;");
    prow->addWidget(m_processBtn);
    prow->addWidget(m_cancelBtn);
    prow->addWidget(m_progressBar, 1);
    prow->addWidget(m_statusLabel, 2);
    vlay->addLayout(prow);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(openBtn,       &QPushButton::clicked, this, &MainWindow::onOpenFile);
    connect(outBtn,        &QPushButton::clicked, this, &MainWindow::onBrowseOutput);
    connect(m_playBtn,     &QPushButton::clicked, this, [this]{
        if (m_player->playbackState() == QMediaPlayer::PlayingState)
            m_player->pause();
        else
            m_player->play();
    });
    connect(m_processBtn,  &QPushButton::clicked, this, &MainWindow::onProcess);
    connect(m_cancelBtn,   &QPushButton::clicked, this, &MainWindow::onCancelProcess);

    connect(m_player, &QMediaPlayer::positionChanged,
            this, &MainWindow::onPlayerPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged,
            this, &MainWindow::onPlayerDurationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this, &MainWindow::onPlayerStateChanged);

    connect(m_timeline, &TimelineWidget::seekRequested,
            this, &MainWindow::onSeekRequested);
    connect(m_timeline, &TimelineWidget::frameViewRequested,
            this, &MainWindow::onFrameViewRequested);
    connect(m_timeline, &TimelineWidget::markEditRequested,
            this, &MainWindow::onMarkEditRequested);
    connect(m_timeline, &TimelineWidget::markDeleteRequested,
            this, &MainWindow::onMarkDeleteRequested);
    connect(m_timeline, &TimelineWidget::marksChanged,
            this, &MainWindow::onMarksChanged);
    connect(m_timeline, &TimelineWidget::markClearAllRequested,
            this, [this]{
        m_timeline->clearAllMarks();
        m_timeline->clearPending();
    });
    connect(m_timeline, &TimelineWidget::viewChanged,
            this, [this](qint64 viewStart, qint64 visible, qint64 total){
        bool zoomed = (visible < total && total > 0);
        m_timeScroll->setVisible(zoomed);
        if (zoomed) {
            m_timeScroll->setRange(0, (int)(total - visible));
            m_timeScroll->setPageStep((int)visible);
            m_timeScroll->setSingleStep((int)(visible / 10));
            QSignalBlocker sb(m_timeScroll);
            m_timeScroll->setValue((int)viewStart);
        }
    });
    connect(m_timeScroll, &QScrollBar::valueChanged,
            this, [this](int val){
        m_timeline->setViewStartMs((qint64)val);
    });

    // ── Menu bar ──────────────────────────────────────────────────────────────
    // View → Zoom scales the entire UI (fonts + timeline height).
    // Timeline time-axis zoom is separate: Ctrl+scroll on the timeline widget.
    m_baseFontPt = font().pointSizeF();
    if (m_baseFontPt <= 0) m_baseFontPt = 10.0;   // fallback if px-based

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* zoomInAct = viewMenu->addAction("Zoom &In");
    zoomInAct->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAct, &QAction::triggered, this, [this]{
        applyUiScale(m_uiScale * 1.25);
    });

    auto* zoomOutAct = viewMenu->addAction("Zoom &Out");
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAct, &QAction::triggered, this, [this]{
        applyUiScale(m_uiScale / 1.25);
    });

    auto* zoomResetAct = viewMenu->addAction("&Reset Zoom");
    zoomResetAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(zoomResetAct, &QAction::triggered, this, [this]{
        applyUiScale(1.0);
    });
    connect(m_timeline, &TimelineWidget::pendingStartChanged,
            this, [this](qint64 ms){
        if (ms >= 0)
            m_statusLabel->setText(
                QString("Timelapse start set at %1 — right-click \xe2\x80\x9cStop Timelapse\xe2\x80\x9d to complete the mark")
                .arg(formatMs(ms)));
        else
            m_statusLabel->clear();
    });
}

MainWindow::~MainWindow()
{
    if (m_ffmpegProc && m_ffmpegProc->state() != QProcess::NotRunning)
        m_ffmpegProc->kill();
}

// ---------------------------------------------------------------------------
// File open / output
// ---------------------------------------------------------------------------
void MainWindow::openFile(const QString& path)
{
    if (path.isEmpty()) return;
    m_inputPath = path;
    m_inputEdit->setText(path);
    QFileInfo fi(path);
    m_outputPath = fi.dir().filePath(fi.baseName() + "_tl.mp4");
    m_outputEdit->setText(m_outputPath);
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->pause();
    setWindowTitle("pathmux-tl \xe2\x80\x94 " + fi.fileName());
}

void MainWindow::onOpenFile()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Open Collage MP4", QString(),
        "MP4 files (*.mp4);;All files (*)");
    openFile(path);
}

void MainWindow::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(
        this, "Output MP4", m_outputEdit->text(), "MP4 files (*.mp4)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".mp4", Qt::CaseInsensitive)) path += ".mp4";
    m_outputEdit->setText(path);
    m_outputPath = path;
}

// ---------------------------------------------------------------------------
// Player callbacks
// ---------------------------------------------------------------------------
void MainWindow::onPlayerPositionChanged(qint64 ms)
{
    m_timeline->setPosition(ms);
    m_timeline->scrollToPosition();
    m_posLabel->setText(formatMs(ms) + " / " + formatMs(m_player->duration()));
}

void MainWindow::onPlayerDurationChanged(qint64 ms)
{
    m_timeline->setDuration(ms);
    m_processBtn->setEnabled(ms > 0 && !m_outputEdit->text().isEmpty());
    updateMarksSummary();
}

void MainWindow::onPlayerStateChanged(QMediaPlayer::PlaybackState state)
{
    m_playBtn->setText(state == QMediaPlayer::PlayingState ? "\xe2\x8f\xb8" : "\xe2\x96\xb6");
}

// ---------------------------------------------------------------------------
// Timeline callbacks
// ---------------------------------------------------------------------------
void MainWindow::onSeekRequested(qint64 ms)
{
    m_player->setPosition(ms);
}

void MainWindow::onFrameViewRequested(qint64 ms)
{
    if (m_inputPath.isEmpty()) return;

    QString tmpPath = QDir::tempPath() + "/pathmux-tl_preview.jpg";
    double secs = ms / 1000.0;

    // Pause so the player doesn't fight our seek
    bool wasPlaying = (m_player->playbackState() == QMediaPlayer::PlayingState);
    if (wasPlaying) m_player->pause();

    QProcess ffmpeg;
    applyPlatformEnv(ffmpeg);
    ffmpeg.start(findFfmpeg(), {
        "-y",
        "-ss", QString::number(secs, 'f', 3),
        "-i", m_inputPath,
        "-frames:v", "1",
        "-q:v", "3",
        tmpPath
    });
    ffmpeg.waitForFinished(8000);

    QPixmap px(tmpPath);
    if (px.isNull()) {
        QMessageBox::warning(this, "Frame View",
            "Could not extract frame.\nIs ffmpeg installed and on PATH?");
        return;
    }

    auto* dlg  = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QString("Frame at %1").arg(formatMs(ms)));
    auto* vl   = new QVBoxLayout(dlg);
    auto* lbl  = new QLabel(dlg);
    int maxW = std::min(1200, (int)(screen()->availableGeometry().width() * 0.85));
    lbl->setPixmap(px.scaledToWidth(maxW, Qt::SmoothTransformation));
    vl->addWidget(lbl);
    auto* okBtn = new QPushButton("Close", dlg);
    connect(okBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    vl->addWidget(okBtn, 0, Qt::AlignRight);
    dlg->exec();
}

void MainWindow::onMarkEditRequested(int id)
{
    const TLMark* mk = nullptr;
    for (const auto& m : m_timeline->marks())
        if (m.id == id) { mk = &m; break; }
    if (!mk) return;

    auto* dlg = new MarkDialog(*mk, this);
    connect(dlg, &MarkDialog::deleteRequested,
            this, &MainWindow::onMarkDeleteRequested);
    if (dlg->exec() == QDialog::Accepted)
        m_timeline->setMarkTarget(id, dlg->targetSecs());
    dlg->deleteLater();
}

void MainWindow::onMarkDeleteRequested(int id)
{
    m_timeline->deleteMark(id);
}

void MainWindow::onMarksChanged()
{
    updateMarksSummary();
    m_processBtn->setEnabled(m_player->duration() > 0
                              && !m_outputEdit->text().isEmpty());
}

// ---------------------------------------------------------------------------
// Keyboard shortcuts
// ---------------------------------------------------------------------------
void MainWindow::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Space:
        if (m_player->playbackState() == QMediaPlayer::PlayingState)
            m_player->pause();
        else
            m_player->play();
        break;
    case Qt::Key_I:
        m_timeline->setPendingStart(m_player->position());
        break;
    case Qt::Key_O: {
        if (m_timeline->pendingStartMs() >= 0) {
            qint64 outMs = m_player->position();
            m_timeline->addMark(m_timeline->pendingStartMs(), outMs);
            m_timeline->clearPending();
        } else {
            m_statusLabel->setText("Press I first to set timelapse start.");
        }
        break;
    }
    case Qt::Key_Equal:
    case Qt::Key_Plus:
        if (e->modifiers() & Qt::ControlModifier) applyUiScale(m_uiScale * 1.25);
        break;
    case Qt::Key_Minus:
        if (e->modifiers() & Qt::ControlModifier) applyUiScale(m_uiScale / 1.25);
        break;
    case Qt::Key_0:
        if (e->modifiers() & Qt::ControlModifier) applyUiScale(1.0);
        break;
    case Qt::Key_Left:
        m_player->setPosition(std::max((qint64)0, m_player->position() - 5000));
        break;
    case Qt::Key_Right:
        m_player->setPosition(std::min(m_player->duration(),
                                       m_player->position() + 5000));
        break;
    default:
        QMainWindow::keyPressEvent(e);
    }
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_ffmpegProc && m_ffmpegProc->state() != QProcess::NotRunning) {
        m_ffmpegProc->kill();
        m_ffmpegProc->waitForFinished(3000);
    }
    e->accept();
}

// ---------------------------------------------------------------------------
// Processing
// ---------------------------------------------------------------------------
QString MainWindow::buildFfmpegCmd() const
{
    const auto& marks = m_timeline->marks();
    qint64 totalMs    = m_timeline->duration();
    if (totalMs <= 0) return {};

    struct Section { double startS, endS; bool isTL; double targetS; };
    QVector<Section> secs;

    double pos    = 0.0;
    double totalS = totalMs / 1000.0;

    for (const auto& m : marks) {
        double mStart = m.startMs / 1000.0;
        double mEnd   = m.endMs   / 1000.0;
        if (mStart > pos + 0.02)
            secs.push_back({pos, mStart, false, 0.0});
        secs.push_back({mStart, mEnd, true,
                        m.targetSecs > 0 ? m.targetSecs : 1.0});
        pos = mEnd;
    }
    if (pos < totalS - 0.02)
        secs.push_back({pos, totalS, false, 0.0});

    if (secs.isEmpty()) return {};

    QString fc;
    int n = secs.size();
    for (int i = 0; i < n; ++i) {
        const auto& s = secs[i];
        if (s.isTL) {
            double dur    = s.endS - s.startS;
            double factor = (dur > 0) ? s.targetS / dur : 1.0;
            fc += QString("[0:v]trim=start=%1:end=%2,"
                          "setpts=(PTS-STARTPTS)*%3[v%4];")
                  .arg(s.startS, 0, 'f', 3)
                  .arg(s.endS,   0, 'f', 3)
                  .arg(factor,   0, 'f', 6)
                  .arg(i);
        } else {
            fc += QString("[0:v]trim=start=%1:end=%2,"
                          "setpts=PTS-STARTPTS[v%3];")
                  .arg(s.startS, 0, 'f', 3)
                  .arg(s.endS,   0, 'f', 3)
                  .arg(i);
        }
    }
    QString inputs;
    for (int i = 0; i < n; ++i) inputs += QString("[v%1]").arg(i);
    fc += inputs + QString("concat=n=%1:v=1:a=0[vout]").arg(n);

    QString output = m_outputEdit->text().trimmed();
    return QString("\"%1\" -y -progress pipe:1 -i \"%2\" -filter_complex \"%3\" "
                   "-map [vout] -an \"%4\"")
           .arg(findFfmpeg(), m_inputPath, fc, output);
}

void MainWindow::onProcess()
{
    m_outputPath = m_outputEdit->text().trimmed();
    if (m_outputPath.isEmpty()) {
        QMessageBox::warning(this, "No Output Path", "Set an output file path first.");
        return;
    }
    for (const auto& m : m_timeline->marks()) {
        if (m.targetSecs <= 0) {
            auto btn = QMessageBox::question(this, "Unconfigured Marks",
                "Some marks have no target duration set and will be condensed to 1 second.\n\nContinue?",
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (btn != QMessageBox::Yes) return;
            break;
        }
    }

    QString cmd = buildFfmpegCmd();
    if (cmd.isEmpty()) {
        QMessageBox::warning(this, "Nothing to process",
            "No timelapse marks defined.");
        return;
    }

    m_outputDurationUs = (qint64)(calcOutputDurationSecs() * 1e6);
    m_processBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
    m_statusLabel->setText("Processing\xe2\x80\xa6");

    m_ffmpegProc = new QProcess(this);
    connect(m_ffmpegProc, &QProcess::readyReadStandardOutput,
            this, &MainWindow::onProcessProgress);
    connect(m_ffmpegProc, &QProcess::readyReadStandardError,
            this, &MainWindow::onProcessReadyRead);
    connect(m_ffmpegProc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onProcessFinished);
    startShellCmd(*m_ffmpegProc, cmd);
}

void MainWindow::onCancelProcess()
{
    if (m_ffmpegProc && m_ffmpegProc->state() != QProcess::NotRunning) {
        m_ffmpegProc->kill();
        m_statusLabel->setText("Cancelled.");
    }
}

void MainWindow::onProcessProgress()
{
    if (!m_ffmpegProc || m_outputDurationUs <= 0) return;
    const QByteArrayList lines = m_ffmpegProc->readAllStandardOutput().split('\n');
    for (const QByteArray& line : lines) {
        if (line.startsWith("out_time_us=")) {
            bool ok;
            qint64 us = line.mid(12).trimmed().toLongLong(&ok);
            if (ok && us >= 0) {
                int pct = (int)(std::min(1.0, (double)us / m_outputDurationUs) * 100);
                m_progressBar->setValue(pct);
            }
        }
    }
}

void MainWindow::onProcessReadyRead()
{
    if (!m_ffmpegProc) return;
    QByteArrayList lines = m_ffmpegProc->readAllStandardError().split('\r');
    for (const auto& l : std::as_const(lines)) {
        QByteArray t = l.trimmed();
        if (!t.isEmpty()) m_statusLabel->setText(QString::fromLocal8Bit(t));
    }
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    m_processBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
    m_ffmpegProc->deleteLater();
    m_ffmpegProc = nullptr;

    if (status == QProcess::NormalExit && exitCode == 0)
        m_progressBar->setValue(100);

    if (status == QProcess::CrashExit || exitCode != 0) {
        m_statusLabel->setText(QString("ffmpeg exited with code %1").arg(exitCode));
        QMessageBox::warning(this, "Processing Failed",
            QString("ffmpeg exited with code %1.\n"
                    "Ensure ffmpeg is installed and the output path is writable.")
            .arg(exitCode));
    } else {
        m_statusLabel->setText("Done: " + m_outputPath);
        QMessageBox::information(this, "Done", "Output written to:\n" + m_outputPath);
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void MainWindow::updateMarksSummary()
{
    const auto& marks = m_timeline->marks();
    if (marks.isEmpty()) {
        m_marksSummary->setText(
            "No timelapse marks. Right-click the timeline \xe2\x80\x94 "
            "Start Timelapse, then Stop Timelapse.");
        return;
    }
    QStringList parts;
    for (const auto& m : marks) {
        if (m.targetSecs > 0)
            parts << QString("[%1\xe2\x80\x93%2 \xe2\x86\x92 %3s]")
                         .arg(formatMs(m.startMs))
                         .arg(formatMs(m.endMs))
                         .arg(m.targetSecs, 0, 'f', 1);
        else
            parts << QString("[%1\xe2\x80\x93%2 unconfigured]")
                         .arg(formatMs(m.startMs))
                         .arg(formatMs(m.endMs));
    }
    m_marksSummary->setText(
        QString("%1 timelapse mark(s):  ").arg(marks.size()) + parts.join("   "));
}

void MainWindow::applyUiScale(double scale)
{
    m_uiScale = std::clamp(scale, 0.5, 3.0);
    if (m_baseFontPt > 0) {
        QFont f = qApp->font();
        f.setPointSizeF(m_baseFontPt * m_uiScale);
        qApp->setFont(f);
    }
    m_timeline->setUiScale(m_uiScale);
    // Force layout to pick up the new font metrics.
    if (centralWidget()) centralWidget()->updateGeometry();
    adjustSize();
}

double MainWindow::calcOutputDurationSecs() const
{
    const auto& marks = m_timeline->marks();
    qint64 totalMs = m_timeline->duration();
    if (totalMs <= 0) return 0;

    double pos = 0, totalS = totalMs / 1000.0, outputS = 0;
    for (const auto& m : marks) {
        double mStart = m.startMs / 1000.0;
        double mEnd   = m.endMs   / 1000.0;
        if (mStart > pos + 0.02) outputS += mStart - pos;
        outputS += (m.targetSecs > 0 ? m.targetSecs : 1.0);
        pos = mEnd;
    }
    if (pos < totalS - 0.02) outputS += totalS - pos;
    return outputS;
}

QString MainWindow::formatMs(qint64 ms) const
{
    if (ms < 0) ms = 0;
    int h = (int)(ms / 3600000);
    int m = (int)((ms % 3600000) / 60000);
    int s = (int)((ms % 60000) / 1000);
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
    return QString("%1:%2").arg(m).arg(s,2,10,QChar('0'));
}
// SN: 00106
