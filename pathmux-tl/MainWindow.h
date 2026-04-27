// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QMainWindow>
#include <QMediaPlayer>
#include <QProcess>
#include <QString>
#include "TimelineWidget.h"

class QVideoWidget;
class QLabel;
class QPushButton;
class QSlider;
class QLineEdit;
class QProgressBar;
class QProcess;
class QScrollBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openFile(const QString& path);   // called by main() for CLI arg

protected:
    void keyPressEvent(QKeyEvent*) override;
    void closeEvent(QCloseEvent*) override;

private slots:
    void onOpenFile();
    void onBrowseOutput();
    void onProcess();
    void onCancelProcess();

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

private:
    void    updateTransportButtons();
    void    updateMarksSummary();
    QString buildFfmpegCmd() const;
    double  calcOutputDurationSecs() const;
    QString formatMs(qint64 ms) const;

    qint64  m_outputDurationUs = 0;   // expected output duration for progress %

    // File
    QString m_inputPath;
    QString m_outputPath;

    // Player
    QMediaPlayer* m_player      = nullptr;
    QVideoWidget* m_videoWidget = nullptr;

    // UI widgets
    QLineEdit*    m_inputEdit   = nullptr;
    QLineEdit*    m_outputEdit  = nullptr;
    QPushButton*  m_playBtn     = nullptr;
    QPushButton*  m_processBtn  = nullptr;
    QPushButton*  m_cancelBtn   = nullptr;
    QLabel*       m_posLabel    = nullptr;
    QLabel*       m_marksSummary= nullptr;
    QProgressBar*   m_progressBar = nullptr;
    QLabel*         m_statusLabel = nullptr;
    TimelineWidget* m_timeline    = nullptr;
    QScrollBar*     m_timeScroll  = nullptr;

    // FFmpeg process
    QProcess* m_ffmpegProc = nullptr;
};
// SN: 00106
