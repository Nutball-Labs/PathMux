// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <QVector>
#include <QMap>
#include <QString>
#include "trip_detection.hpp"
#include "video_build.hpp"

class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

// One row in the stage stack — name, bar, live status (ETA → ✓ when done)
struct StageRow {
    QString       label;
    QProgressBar* bar     = nullptr;
    QLabel*       status  = nullptr;
    bool          started = false;  // true once first progress signal received
    bool          failed  = false;  // true if stage emitted pct == -1
};

class BuildProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit BuildProgressDialog(const Pathmux::Trip& trip,
                                 const VideoOptions&  opts,
                                 QWidget*             parent = nullptr);
    void startBuild();

signals:
    void buildComplete(bool ok);

private slots:
    // msg is non-empty only on failure — carries first line of ffmpeg stderr
    void onProgress(const QString& label, int pct, int etaSecs,
                    const QString& msg = "");
    void onFinished(bool ok, const QString& error);

private:
    void addStageRow(const QString& label);
    void populateExpectedRows();

    Pathmux::Trip     m_trip;
    VideoOptions      m_opts;

    QScrollArea*      m_scrollArea   = nullptr;
    QVBoxLayout*      m_stageLayout  = nullptr;
    QVector<StageRow> m_rows;
    QMap<QString,int> m_rowIndex;    // label → index in m_rows

    QLabel*           m_outputDirLabel = nullptr;  // shows resolved output path
    QLabel*           m_finalLabel     = nullptr;
    QPushButton*      m_closeBtn       = nullptr;
};
// SN: 00094
