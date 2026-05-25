// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "MainWindow.h"
#include "MarkDialog.h"
#include "compat.hpp"
#include "version.hpp"
#include <filesystem>
#include <sstream>

static void brandOutputFile(const std::string& path, const std::string& contentType,
                             const std::string& ffmpegPath)
{
    auto dot = path.rfind('.');
    std::string tmp = (dot != std::string::npos)
        ? path.substr(0, dot) + ".clops_brand" + path.substr(dot)
        : path + ".clops_brand";
    std::ostringstream cmd;
    cmd << "\"" << ffmpegPath << "\" -v quiet -y"
        << " -i \"" << path << "\""
        << " -map 0 -c copy"
        << " -metadata vendor=\"Nutball Labs\""
        << " -metadata product=\"CamClops\""
        << " -metadata version=\"" APP_VERSION ", HWM " VERSION_HWM "\""
        << " -metadata created_on=\"" << getShortHostname() << "\""
        << " -metadata content_type=\"" << contentType << "\""
        << " \"" << tmp << "\" " << NULL_REDIRECT;
    FILE* p = popen(cmd.str().c_str(), "r");
    if (!p) return;
    char buf[256]; while (fgets(buf, sizeof(buf), p)) {}
    pclose(p);
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) std::filesystem::remove(tmp, ec);
}
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QVideoWidget>
#include <QMediaMetaData>
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
#include <QWheelEvent>
#include <QCloseEvent>
#include <QDialog>
#include <QPixmap>
#include <QPalette>
#include <QDir>
#include <QFileInfo>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QKeySequence>
#include <QShortcut>
#include <QPainter>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// LogoBackdrop — solid white panel with Nutball-Labs watermark at 33% opacity.
// Shown in the video area before any MP4 is loaded, eliminating the z-order
// bleed-through that occurs when QVideoWidget's native X11 window is transparent.
// ---------------------------------------------------------------------------
class LogoBackdrop : public QWidget {
public:
    explicit LogoBackdrop(QWidget* parent = nullptr) : QWidget(parent) {
        m_logo = QPixmap(":/images/Nutball-Labs_logo.png");
        QPalette pal = palette();
        pal.setColor(QPalette::Window, Qt::white);
        setPalette(pal);
        setAutoFillBackground(true);
    }
protected:
    void paintEvent(QPaintEvent* ev) override {
        QWidget::paintEvent(ev);
        if (m_logo.isNull()) return;
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setOpacity(0.33);
        QPixmap sc = m_logo.scaled(width(), height(),
                                    Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap((width() - sc.width()) / 2,
                     (height() - sc.height()) / 2, sc);
    }
private:
    QPixmap m_logo;
};

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle("camclops-tl \xe2\x80\x94 CamClops Timelapse Editor");
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

    m_videoStack = new QStackedWidget(this);
    m_videoStack->addWidget(new LogoBackdrop(m_videoStack));  // index 0 — no file loaded
    m_videoStack->addWidget(m_videoWidget);                   // index 1 — video playing
    m_videoStack->setCurrentIndex(0);
    vlay->addWidget(m_videoStack, 1);

    // ── Mark control row (above timeline) ────────────────────────────────────
    auto* markRow = new QHBoxLayout;
    m_setStartBtn = new QPushButton("Set Start", this);
    m_setStartBtn->setToolTip("Set timelapse start at current position (I)");
    m_setEndBtn   = new QPushButton("Set End", this);
    m_setEndBtn->setToolTip("Complete timelapse mark at current position (O)");
    m_setEndBtn->setEnabled(false);
    m_posLabel = new QLabel("--:--.-- / --:--.--", this);
    m_posLabel->setMinimumWidth(180);
    m_hlpLbl = new QLabel(
        "Space=play/pause  \xc2\xb7  "
        "\xe2\x86\x90/\xe2\x86\x92=frame step  \xc2\xb7  "
        "Shift+\xe2\x86\x90/\xe2\x86\x92=\xc2\xb1" "5s  \xc2\xb7  "
        "I=set start  O=set end", this);
    m_hlpLbl->setStyleSheet("color: gray;");
    markRow->addWidget(m_setStartBtn);
    markRow->addWidget(m_setEndBtn);
    markRow->addWidget(m_posLabel);
    markRow->addStretch();
    markRow->addWidget(m_hlpLbl);
    vlay->addLayout(markRow);

    // ── Timeline + scrollbar ──────────────────────────────────────────────────
    m_timeline = new TimelineWidget(this);
    vlay->addWidget(m_timeline);

    m_timeScroll = new QScrollBar(Qt::Horizontal, this);
    m_timeScroll->setRange(0, 0);
    m_timeScroll->setSingleStep(1000);
    m_timeScroll->setPageStep(1000);
    m_timeScroll->setVisible(false);
    vlay->addWidget(m_timeScroll);

    // ── Frame strip (below timeline) ─────────────────────────────────────────
    m_frameStrip = new FrameStrip(this);
    vlay->addWidget(m_frameStrip);

    // Debounce: reload strip 100ms after the last position change while paused.
    m_stripTimer = new QTimer(this);
    m_stripTimer->setSingleShot(true);
    m_stripTimer->setInterval(100);
    connect(m_stripTimer, &QTimer::timeout, this, &MainWindow::reloadFrameStrip);

    // ── Transport row (below timeline) ───────────────────────────────────────
    auto* trow = new QHBoxLayout;
    m_skipBackBtn  = new QPushButton("-5s", this);
    m_skipBackBtn->setToolTip("Skip back 5 seconds (Shift+\xe2\x86\x90)");
    m_frameBackBtn = new QPushButton("\xe2\x97\x81", this);   // ◁
    m_frameBackBtn->setToolTip("Step back 1 frame (\xe2\x86\x90)");
    m_playBtn      = new QPushButton("\xe2\x96\xb6", this);   // ▶
    m_frameFwdBtn  = new QPushButton("\xe2\x96\xb7", this);   // ▷
    m_frameFwdBtn->setToolTip("Step forward 1 frame (\xe2\x86\x92)");
    m_skipFwdBtn   = new QPushButton("+5s", this);
    m_skipFwdBtn->setToolTip("Skip forward 5 seconds (Shift+\xe2\x86\x92)");
    trow->addStretch();
    trow->addWidget(m_skipBackBtn);
    trow->addWidget(m_frameBackBtn);
    trow->addWidget(m_playBtn);
    trow->addWidget(m_frameFwdBtn);
    trow->addWidget(m_skipFwdBtn);
    trow->addStretch();
    vlay->addLayout(trow);

    // ── Cut list (VRD-style mark table) ──────────────────────────────────────
    m_markList = new MarkListWidget(this);
    vlay->addWidget(m_markList);

    // ── Source / output info bar ─────────────────────────────────────────────
    auto* infoRow    = new QHBoxLayout;
    m_srcInfoLabel   = new QLabel(this);
    m_outEstLabel    = new QLabel(this);
    m_outEstLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    infoRow->addWidget(m_srcInfoLabel);
    infoRow->addStretch();
    infoRow->addWidget(m_outEstLabel);
    vlay->addLayout(infoRow);

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
    prow->addWidget(m_processBtn);
    prow->addWidget(m_cancelBtn);
    prow->addWidget(m_progressBar, 1);
    prow->addWidget(m_statusLabel, 2);
    vlay->addLayout(prow);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(openBtn,       &QPushButton::clicked, this, &MainWindow::onOpenFile);
    connect(outBtn,        &QPushButton::clicked, this, &MainWindow::onBrowseOutput);
    connect(m_setStartBtn, &QPushButton::clicked, this, [this]{
        m_timeline->setPendingStart(m_player->position());
    });
    connect(m_setEndBtn, &QPushButton::clicked, this, [this]{
        if (m_timeline->pendingStartMs() >= 0) {
            m_timeline->addMark(m_timeline->pendingStartMs(), m_player->position());
            m_timeline->clearPending();
        }
    });
    connect(m_skipBackBtn, &QPushButton::clicked, this, [this]{
        m_player->setPosition(std::max((qint64)0, m_player->position() - 5000));
    });
    connect(m_skipFwdBtn, &QPushButton::clicked, this, [this]{
        m_player->setPosition(std::min(m_player->duration(), m_player->position() + 5000));
    });
    connect(m_frameBackBtn, &QPushButton::clicked, this, [this]{
        m_player->setPosition(std::max((qint64)0, m_player->position() - m_frameDurationMs));
    });
    connect(m_frameFwdBtn, &QPushButton::clicked, this, [this]{
        m_player->setPosition(std::min(m_player->duration(), m_player->position() + m_frameDurationMs));
    });
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
    connect(m_player, &QMediaPlayer::errorOccurred,
            this, [this](QMediaPlayer::Error, const QString& errStr) {
        m_statusLabel->setText("Media error: " + errStr);
        m_statusLabel->setStyleSheet("color: #ff6060;");
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, [this](QMediaPlayer::MediaStatus status) {
        switch (status) {
        case QMediaPlayer::LoadingMedia:
            m_statusLabel->setText("Loading\xe2\x80\xa6");
            m_statusLabel->setStyleSheet({});
            break;
        case QMediaPlayer::LoadedMedia:
            m_statusLabel->clear();
            m_statusLabel->setStyleSheet({});
            m_hasAudio = !m_player->audioTracks().isEmpty();
            {
                QVariant fr = m_player->metaData().value(QMediaMetaData::VideoFrameRate);
                if (fr.isValid()) {
                    double fps = fr.toDouble();
                    if (fps > 0)
                        m_frameDurationMs = qMax((qint64)1,
                                                 (qint64)(1000.0 / fps + 0.5));
                }
            }
            break;
        case QMediaPlayer::InvalidMedia:
            m_statusLabel->setText("Invalid media \xe2\x80\x94 unsupported format or missing codec");
            m_statusLabel->setStyleSheet("color: #ff6060;");
            break;
        case QMediaPlayer::StalledMedia:
            m_statusLabel->setText("Stalled\xe2\x80\xa6");
            m_statusLabel->setStyleSheet({});
            break;
        default:
            break;
        }
    });

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

    connect(m_markList, &MarkListWidget::markSeekRequested,
            m_player, &QMediaPlayer::setPosition);
    connect(m_markList, &MarkListWidget::markEditRequested,
            this, &MainWindow::onMarkEditRequested);
    connect(m_markList, &MarkListWidget::markDeleteRequested,
            this, &MainWindow::onMarkDeleteRequested);
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
    connect(m_frameStrip, &FrameStrip::frameClicked,
            this, [this](qint64 ms){
        m_player->setPosition(ms);
    });
    connect(m_frameStrip, &FrameStrip::framesLoaded, this, [this]{
        if (m_frameStripError) {
            m_statusLabel->clear();
            m_statusLabel->setStyleSheet({});
            m_frameStripError = false;
        }
    });
    connect(m_frameStrip, &FrameStrip::extractionFailed,
            this, [this](const QString& err){
        m_frameStripError = true;
        m_statusLabel->setText("Frame strip: " + err);
        m_statusLabel->setStyleSheet("color: #ff6060;");
    });

    // ── Transport keyboard shortcuts ──────────────────────────────────────────
    // QShortcut fires at window level regardless of which child has focus.
    // Guard arrow shortcuts so they don't fire while a QLineEdit has focus.
    auto noTextFocus = [this]{ return qobject_cast<QLineEdit*>(focusWidget()) == nullptr; };
    auto* leftSC  = new QShortcut(Qt::Key_Left,  this);
    auto* rightSC = new QShortcut(Qt::Key_Right, this);
    auto* shiftLeftSC  = new QShortcut(Qt::SHIFT | Qt::Key_Left,  this);
    auto* shiftRightSC = new QShortcut(Qt::SHIFT | Qt::Key_Right, this);
    connect(leftSC,  &QShortcut::activated, this, [this, noTextFocus]{
        if (noTextFocus())
            m_player->setPosition(std::max((qint64)0, m_player->position() - m_frameDurationMs));
    });
    connect(rightSC, &QShortcut::activated, this, [this, noTextFocus]{
        if (noTextFocus())
            m_player->setPosition(std::min(m_player->duration(), m_player->position() + m_frameDurationMs));
    });
    connect(shiftLeftSC,  &QShortcut::activated, this, [this, noTextFocus]{
        if (noTextFocus())
            m_player->setPosition(std::max((qint64)0, m_player->position() - 5000));
    });
    connect(shiftRightSC, &QShortcut::activated, this, [this, noTextFocus]{
        if (noTextFocus())
            m_player->setPosition(std::min(m_player->duration(), m_player->position() + 5000));
    });
    auto* spaceSC = new QShortcut(Qt::Key_Space, this);
    connect(spaceSC, &QShortcut::activated, this, [this, noTextFocus]{
        if (noTextFocus()) {
            if (m_player->playbackState() == QMediaPlayer::PlayingState)
                m_player->pause();
            else
                m_player->play();
        }
    });

    // ── Menu bar ──────────────────────────────────────────────────────────────
    // Ctrl+scroll anywhere in the window scales the UI — same mechanism as
    // camclops-gui's tile zoom (step 0.1, range 0.5–3.0). Install an event
    // filter on the central widget so child widgets pass Ctrl+scroll events up.
    m_baseFontPt = font().pointSizeF();
    if (m_baseFontPt <= 0) m_baseFontPt = 10.0;

    central->installEventFilter(this);
    applyUiScale(m_uiScale);

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* loadMarksAct = fileMenu->addAction("&Load Marks\xe2\x80\xa6");
    loadMarksAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    auto* saveMarksAct = fileMenu->addAction("&Save Marks As\xe2\x80\xa6");
    saveMarksAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
    connect(loadMarksAct, &QAction::triggered, this, [this]{
        QString path = QFileDialog::getOpenFileName(
            this, "Load Marks", m_marksPath.isEmpty() ? QString() : m_marksPath,
            "CamClops TL marks (*.cltl.json *.pmtl.json);;All files (*)");
        if (!path.isEmpty()) { loadMarks(path); m_marksLoaded = true; }
    });
    connect(saveMarksAct, &QAction::triggered, this, [this]{
        QString path = QFileDialog::getSaveFileName(
            this, "Save Marks As", m_marksPath.isEmpty() ? QString() : m_marksPath,
            "CamClops TL marks (*.cltl.json);;All files (*)");
        if (path.isEmpty()) return;
        if (!path.endsWith(".cltl.json")) path += ".cltl.json";
        m_marksPath = path;
        saveMarks();
    });

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* zoomInAct  = viewMenu->addAction("Zoom &In");
    zoomInAct->setShortcut(QKeySequence::ZoomIn);
    zoomInAct->setEnabled(false);   // driven by Ctrl+scroll, same as camclops-gui

    auto* zoomOutAct = viewMenu->addAction("Zoom &Out");
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    zoomOutAct->setEnabled(false);

    auto* zoomResetAct = viewMenu->addAction("&Reset Zoom");
    zoomResetAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    zoomResetAct->setEnabled(false);
    connect(m_timeline, &TimelineWidget::pendingStartChanged,
            this, [this](qint64 ms){
        m_setEndBtn->setEnabled(ms >= 0);
        if (ms >= 0)
            m_statusLabel->setText(
                QString("Start set at %1 \xe2\x80\x94 click Set End or press O to complete")
                .arg(formatMsFrame(ms)));
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
    m_outputPath   = fi.dir().filePath(fi.baseName() + "_tl.mp4");
    m_marksPath    = fi.dir().filePath(fi.baseName() + ".cltl.json");
    m_marksLoaded  = false;
    m_outputEdit->setText(m_outputPath);
    m_frameStrip->clearFrames();
    m_statusLabel->clear();
    m_statusLabel->setStyleSheet({});
    m_hasAudio = false;
    m_videoStack->setCurrentIndex(1);   // reveal video widget, hide logo backdrop
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->pause();
    setWindowTitle("camclops-tl \xe2\x80\x94 " + fi.fileName());
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
    m_posLabel->setText(formatMsFrame(ms) + " / " + formatMsFrame(m_player->duration()));
    if (m_player->playbackState() != QMediaPlayer::PlayingState)
        m_stripTimer->start();
}

void MainWindow::onPlayerDurationChanged(qint64 ms)
{
    m_timeline->setDuration(ms);   // clears marks
    if (ms > 0 && !m_marksLoaded && !m_marksPath.isEmpty()) {
        if (QFileInfo::exists(m_marksPath)) {
            loadMarks(m_marksPath);
            m_marksLoaded = true;
        } else {
            // Migrate legacy .pmtl.json → .cltl.json
            QString legacy = m_marksPath;
            legacy.replace(".cltl.json", ".pmtl.json");
            if (QFileInfo::exists(legacy)) {
                loadMarks(legacy);
                m_marksLoaded = true;
                saveMarks();   // re-save under new extension
            }
        }
    }
    m_processBtn->setEnabled(ms > 0 && !m_outputEdit->text().isEmpty());
    updateMarksSummary();
    updateInfoBar();
}

void MainWindow::onPlayerStateChanged(QMediaPlayer::PlaybackState state)
{
    m_playBtn->setText(state == QMediaPlayer::PlayingState ? "\xe2\x8f\xb8" : "\xe2\x96\xb6");
    if (state == QMediaPlayer::PausedState)
        m_stripTimer->start();
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

    QString tmpPath = QDir::tempPath() + "/camclops-tl_preview.jpg";
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
        QMessageBox mb(QMessageBox::Warning, "Frame View",
                       "Could not extract frame.\nIs ffmpeg installed and on PATH?",
                       QMessageBox::Ok, this);
        mb.setWindowModality(Qt::WindowModal);
        mb.exec();
        return;
    }

    auto* dlg  = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QString("Frame at %1").arg(formatMsFrame(ms)));
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

    auto* dlg = new MarkDialog(*mk, m_player->duration(), m_frameDurationMs, this);
    connect(dlg, &MarkDialog::deleteRequested,
            this, &MainWindow::onMarkDeleteRequested);
    if (dlg->exec() == QDialog::Accepted)
        m_timeline->updateMark(dlg->result());
    dlg->deleteLater();
}

void MainWindow::onMarkDeleteRequested(int id)
{
    m_timeline->deleteMark(id);
}

void MainWindow::onMarksChanged()
{
    updateMarksSummary();
    updateInfoBar();
    m_processBtn->setEnabled(m_player->duration() > 0
                              && !m_outputEdit->text().isEmpty());
    if (!m_suppressSave) saveMarks();
}

// ---------------------------------------------------------------------------
// Keyboard shortcuts
// ---------------------------------------------------------------------------
void MainWindow::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
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
// Build an atempo filter chain for any speed factor.
// atempo range per filter is [0.5, 100]; chain multiple stages for extremes.
static QString atempoChain(double factor)
{
    if (factor <= 0) factor = 1.0;
    QStringList f;
    double rem = factor;
    while (rem > 100.0) { f << "atempo=100.0"; rem /= 100.0; }
    while (rem < 0.5)   { f << "atempo=0.5";   rem *= 2.0;   }
    f << QString("atempo=%1").arg(rem, 0, 'f', 6);
    return f.join(",");
}

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
        if (m.targetSecs != 0.0)   // skip cut spans entirely
            secs.push_back({mStart, mEnd, true,
                            m.targetSecs > 0 ? m.targetSecs : 1.0});
        pos = mEnd;
    }
    if (pos < totalS - 0.02)
        secs.push_back({pos, totalS, false, 0.0});

    if (secs.isEmpty()) return {};

    // Compute target output fps once. Applied to ALL sections (both timelapse and
    // passthrough) so concat receives streams with identical declared frame rates.
    // Without -hwaccel, frames are always in CPU memory and CPU filters work correctly.
    double outFps = m_frameDurationMs > 0 ? 1000.0 / m_frameDurationMs : 30.0;

    QString fc;
    int n = secs.size();
    for (int i = 0; i < n; ++i) {
        const auto& s = secs[i];
        double dur = s.endS - s.startS;

        // Video chain
        if (s.isTL) {
            double vFactor = (dur > 0) ? s.targetS / dur : 1.0;
            // fps= after setpts drops frame count to output-fps before concat/encode.
            fc += QString("[0:v]trim=start=%1:end=%2,"
                          "setpts=(PTS-STARTPTS)*%3,"
                          "fps=%4[v%5];")
                  .arg(s.startS, 0, 'f', 3)
                  .arg(s.endS,   0, 'f', 3)
                  .arg(vFactor,  0, 'f', 6)
                  .arg(outFps,   0, 'f', 3)
                  .arg(i);
        } else {
            // Apply fps= to passthrough sections too so all concat inputs agree.
            fc += QString("[0:v]trim=start=%1:end=%2,"
                          "setpts=PTS-STARTPTS,"
                          "fps=%3[v%4];")
                  .arg(s.startS, 0, 'f', 3)
                  .arg(s.endS,   0, 'f', 3)
                  .arg(outFps,   0, 'f', 3)
                  .arg(i);
        }

        // Audio chain — only when input has audio
        if (m_hasAudio) {
            if (s.isTL) {
                double aFactor = (dur > 0 && s.targetS > 0) ? dur / s.targetS : 1.0;
                fc += QString("[0:a]atrim=start=%1:end=%2,"
                              "asetpts=PTS-STARTPTS,%3[a%4];")
                      .arg(s.startS, 0, 'f', 3)
                      .arg(s.endS,   0, 'f', 3)
                      .arg(atempoChain(aFactor))
                      .arg(i);
            } else {
                fc += QString("[0:a]atrim=start=%1:end=%2,"
                              "asetpts=PTS-STARTPTS[a%3];")
                      .arg(s.startS, 0, 'f', 3)
                      .arg(s.endS,   0, 'f', 3)
                      .arg(i);
            }
        }
    }

    QString inputs;
    for (int i = 0; i < n; ++i) {
        inputs += QString("[v%1]").arg(i);
        if (m_hasAudio) inputs += QString("[a%1]").arg(i);
    }

    QString output = m_outputEdit->text().trimmed();
    if (m_hasAudio) {
        fc += inputs + QString("concat=n=%1:v=1:a=1[vout][aout]").arg(n);
        return QString("\"%1\" -y -progress pipe:1 -i \"%2\" "
                       "-filter_complex \"%3\" "
                       "-map [vout] -map [aout] -c:a aac -movflags +faststart \"%4\"")
               .arg(findFfmpeg(), m_inputPath, fc, output);
    } else {
        fc += inputs + QString("concat=n=%1:v=1:a=0[vout]").arg(n);
        return QString("\"%1\" -y -progress pipe:1 -i \"%2\" "
                       "-filter_complex \"%3\" "
                       "-map [vout] -an -movflags +faststart \"%4\"")
               .arg(findFfmpeg(), m_inputPath, fc, output);
    }
}

void MainWindow::onProcess()
{
    m_outputPath = m_outputEdit->text().trimmed();
    if (m_outputPath.isEmpty()) {
        QMessageBox mb(QMessageBox::Warning, "No Output Path",
                       "Set an output file path first.", QMessageBox::Ok, this);
        mb.setWindowModality(Qt::WindowModal);
        mb.exec();
        return;
    }
    for (const auto& m : m_timeline->marks()) {
        if (m.targetSecs < 0) {
            QMessageBox mb(QMessageBox::Question, "Unconfigured Marks",
                "Some marks have no target duration set and will be condensed to 1 second.\n\nContinue?",
                QMessageBox::Yes | QMessageBox::Cancel, this);
            mb.setWindowModality(Qt::WindowModal);
            mb.setDefaultButton(QMessageBox::Cancel);
            if (mb.exec() != QMessageBox::Yes) return;
            break;
        }
    }

    QString cmd = buildFfmpegCmd();
    if (cmd.isEmpty()) {
        QMessageBox mb(QMessageBox::Warning, "Nothing to process",
                       "No timelapse marks defined.", QMessageBox::Ok, this);
        mb.setWindowModality(Qt::WindowModal);
        mb.exec();
        return;
    }

    if (QFileInfo::exists(m_outputPath)) {
        QMessageBox mb(QMessageBox::Warning, "Overwrite File?",
                       QString("Output file already exists:\n%1\n\nOverwrite it?")
                       .arg(m_outputPath),
                       QMessageBox::Yes | QMessageBox::Cancel, this);
        mb.setWindowModality(Qt::WindowModal);
        mb.setDefaultButton(QMessageBox::Cancel);
        if (mb.exec() != QMessageBox::Yes) return;
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
        QMessageBox mb(QMessageBox::Warning, "Processing Failed",
            QString("ffmpeg exited with code %1.\n"
                    "Ensure ffmpeg is installed and the output path is writable.")
            .arg(exitCode), QMessageBox::Ok, this);
        mb.setWindowModality(Qt::WindowModal);
        mb.exec();
    } else {
        // Brand the output file with provenance metadata (silent stream-copy pass).
        brandOutputFile(m_outputPath.toStdString(), "timelapse",
                        findFfmpeg().toStdString());
        m_statusLabel->setText("Done: " + m_outputPath);
        QMessageBox mb(QMessageBox::Information, "Done",
                       "Output written to:\n" + m_outputPath, QMessageBox::Ok, this);
        mb.setWindowModality(Qt::WindowModal);
        mb.exec();
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void MainWindow::updateMarksSummary()
{
    m_markList->rebuild(m_timeline->marks(), m_frameDurationMs);
}

void MainWindow::updateTransportButtons()
{
    m_setEndBtn->setEnabled(m_timeline->pendingStartMs() >= 0);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            const double delta = we->angleDelta().y() > 0 ? 0.1 : -0.1;
            applyUiScale(m_uiScale + delta);
            we->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::applyUiScale(double scale)
{
    m_uiScale = qBound(0.5, scale, 3.0);
    if (m_baseFontPt > 0) {
        QFont f = qApp->font();
        f.setPointSizeF(m_baseFontPt * m_uiScale);
        qApp->setFont(f);
        // Buttons don't reliably reflow on ApplicationFontChanged — set explicitly.
        for (auto* btn : findChildren<QPushButton*>())
            btn->setFont(f);
        // Small-text labels use a reduced point size.
        QFont small = f;
        small.setPointSizeF(m_baseFontPt * m_uiScale * 0.8);
        m_hlpLbl->setFont(small);
        m_srcInfoLabel->setFont(small);
        m_outEstLabel->setFont(small);
        m_statusLabel->setFont(small);
    }
    m_timeline->setUiScale(m_uiScale);
    m_frameStrip->setUiScale(m_uiScale);
    m_markList->setUiScale(m_uiScale);
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
        if (m.targetSecs > 0)       outputS += m.targetSecs;
        else if (m.targetSecs < 0)  outputS += 1.0;   // unconfigured fallback
        // cut (== 0): contributes nothing
        pos = mEnd;
    }
    if (pos < totalS - 0.02) outputS += totalS - pos;
    return outputS;
}

QString MainWindow::fmtBytes(qint64 bytes)
{
    if (bytes >= (1LL << 30))
        return QString("%1 GB").arg((double)bytes / (1LL << 30), 0, 'f', 2);
    if (bytes >= (1LL << 20))
        return QString("%1 MB").arg((double)bytes / (1LL << 20), 0, 'f', 0);
    return QString("%1 KB").arg(bytes >> 10);
}

void MainWindow::updateInfoBar()
{
    qint64 srcMs = m_player->duration();
    if (m_inputPath.isEmpty() || srcMs <= 0) {
        m_srcInfoLabel->clear();
        m_outEstLabel->clear();
        return;
    }

    qint64 srcBytes = QFileInfo(m_inputPath).size();
    m_srcInfoLabel->setText(
        QString("Source:  %1  \xc2\xb7  %2")
        .arg(fmtBytes(srcBytes))
        .arg(formatMs(srcMs)));

    double outSecs = calcOutputDurationSecs();
    if (outSecs <= 0) { m_outEstLabel->clear(); return; }

    // Estimate output size proportional to source bitrate × output duration.
    double srcSecs  = srcMs / 1000.0;
    qint64 estBytes = (qint64)((double)srcBytes / srcSecs * outSecs);
    qint64 outMs    = (qint64)(outSecs * 1000.0);

    QString outText = QString("Est. output:  ~%1  \xc2\xb7  %2")
                      .arg(fmtBytes(estBytes))
                      .arg(formatMs(outMs));
    // Highlight when there's a meaningful size reduction.
    if (estBytes < (qint64)(srcBytes * 0.95))
        outText = "\xe2\x86\x93 " + outText;   // ↓ prefix
    m_outEstLabel->setText(outText);
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

QString MainWindow::formatMsFrame(qint64 ms) const
{
    if (ms < 0) ms = 0;
    int h = (int)(ms / 3600000);
    int m = (int)((ms % 3600000) / 60000);
    int s = (int)((ms % 60000) / 1000);
    int f = m_frameDurationMs > 0 ? (int)((ms % 1000) / m_frameDurationMs) : 0;
    if (h > 0)
        return QString("%1:%2:%3.%4")
               .arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0')).arg(f,2,10,QChar('0'));
    return QString("%1:%2.%3").arg(m).arg(s,2,10,QChar('0')).arg(f,2,10,QChar('0'));
}

// ---------------------------------------------------------------------------
// Marks persistence
// ---------------------------------------------------------------------------
void MainWindow::saveMarks()
{
    if (m_marksPath.isEmpty() || m_inputPath.isEmpty()) return;
    QJsonArray jarr;
    for (const auto& mk : m_timeline->marks()) {
        QJsonObject obj;
        obj["startMs"]    = mk.startMs;
        obj["start"]      = formatMsFrame(mk.startMs);
        obj["endMs"]      = mk.endMs;
        obj["end"]        = formatMsFrame(mk.endMs);
        obj["targetSecs"] = mk.targetSecs;
        jarr.append(obj);
    }
    QJsonObject root;
    root["version"] = 1;
    root["source"]  = m_inputPath;
    root["marks"]   = jarr;
    QFile f(m_marksPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool MainWindow::loadMarks(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    QJsonArray jarr = doc.object()["marks"].toArray();
    if (jarr.isEmpty()) return true;

    m_suppressSave = true;
    m_timeline->clearAllMarks();
    m_timeline->clearPending();
    for (const QJsonValue& v : jarr) {
        QJsonObject obj = v.toObject();
        qint64 startMs  = (qint64)obj["startMs"].toDouble();
        qint64 endMs    = (qint64)obj["endMs"].toDouble();
        double tgt      = obj["targetSecs"].toDouble(-1.0);
        if (endMs > startMs)
            m_timeline->addMarkFull(startMs, endMs, tgt);
    }
    m_suppressSave = false;
    updateMarksSummary();
    updateInfoBar();
    return true;
}

void MainWindow::reloadFrameStrip()
{
    if (m_inputPath.isEmpty() || m_player->duration() <= 0) return;
    m_frameStrip->loadFrames(m_inputPath, m_player->position(),
                              m_frameDurationMs, findFfmpeg());
}
// SN: 00122
