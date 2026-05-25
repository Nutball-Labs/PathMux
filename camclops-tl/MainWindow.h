// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QMainWindow>
#include <QMediaPlayer>
#include <QProcess>
#include <QString>
#include "TimelineWidget.h"
#include "FrameStrip.h"
#include "MarkListWidget.h"

class QVideoWidget;
class QStackedWidget;
class QLabel;
class QPushButton;
class QSlider;
class QLineEdit;
class QProgressBar;
class QProcess;
class QScrollBar;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openFile(const QString& path);   // called by main() for CLI arg

protected:
    void keyPressEvent(QKeyEvent*) override;
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onOpenFile();
    void onBrowseOutput();
    void onProcess();
    void onCancelProcess();
    void saveMarks();
    bool loadMarks(const QString& path);

    void onPlayerPositionChanged(qint64 ms);
    void onPlayerDurationChanged(qint64 ms);
    void onPlayerStateChanged(QMediaPlayer::PlaybackState state);

    void onSeekRequested(qint64 ms);
    void onFrameViewRequested(qint64 ms);
    void onMarkEditRequested(int id);
    void onMarkDeleteRequested(int id);
    void onMarksChanged();

    void onProcessProgress();     // stdout: -progress pipe:1 key=value
    void onProcessReadyRead();    // stderr: status messages
    void onProcessFinished(int exitCode, QProcess::ExitStatus);

    void reloadFrameStrip();

private:
    void    updateTransportButtons();
    void    updateMarksSummary();
    void    updateInfoBar();
    void    applyUiScale(double scale);
    QString buildFfmpegCmd() const;
    double  calcOutputDurationSecs() const;
    QString formatMs(qint64 ms) const;
    QString formatMsFrame(qint64 ms) const;   // H:MM:SS.FF frame-accurate
    static QString fmtBytes(qint64 bytes);

    qint64  m_outputDurationUs = 0;
    double  m_uiScale          = 1.0;
    double  m_baseFontPt       = 0.0;   // captured once at startup
    bool    m_hasAudio         = false;
    bool    m_frameStripError  = false;
    bool    m_marksLoaded      = false;
    bool    m_suppressSave     = false;

    // File
    QString m_inputPath;
    QString m_outputPath;
    QString m_marksPath;

    // Player
    QMediaPlayer*  m_player      = nullptr;
    QVideoWidget*  m_videoWidget = nullptr;
    QStackedWidget* m_videoStack = nullptr;

    // UI widgets
    QLineEdit*    m_inputEdit   = nullptr;
    QLineEdit*    m_outputEdit  = nullptr;
    QPushButton*  m_setStartBtn  = nullptr;
    QPushButton*  m_setEndBtn    = nullptr;
    QPushButton*  m_skipBackBtn  = nullptr;
    QPushButton*  m_skipFwdBtn   = nullptr;
    QPushButton*  m_frameBackBtn = nullptr;
    QPushButton*  m_frameFwdBtn  = nullptr;
    QPushButton*  m_playBtn      = nullptr;
    QPushButton*  m_processBtn   = nullptr;
    QPushButton*  m_cancelBtn    = nullptr;
    QLabel*       m_posLabel     = nullptr;
    QLabel*       m_hlpLbl       = nullptr;
    qint64        m_frameDurationMs = 33;
    MarkListWidget* m_markList    = nullptr;
    QLabel*       m_srcInfoLabel  = nullptr;
    QLabel*       m_outEstLabel   = nullptr;
    QProgressBar*   m_progressBar = nullptr;
    QLabel*         m_statusLabel = nullptr;
    TimelineWidget* m_timeline    = nullptr;
    QScrollBar*     m_timeScroll  = nullptr;
    FrameStrip*     m_frameStrip  = nullptr;
    QTimer*         m_stripTimer  = nullptr;

    // FFmpeg process
    QProcess* m_ffmpegProc = nullptr;
};
// SN: 00122
