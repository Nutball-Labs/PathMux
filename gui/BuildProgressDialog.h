// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <QVector>
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
    QProgressBar* bar    = nullptr;
    QLabel*       status = nullptr;
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
    void onProgress(const QString& label, int pct, int etaSecs);
    void onFinished(bool ok, const QString& error);

private:
    void addStageRow(const QString& label);
    void completeCurrentRow();

    Pathmux::Trip     m_trip;
    VideoOptions      m_opts;

    QScrollArea*      m_scrollArea   = nullptr;
    QVBoxLayout*      m_stageLayout  = nullptr;
    QVector<StageRow> m_rows;
    QString           m_currentLabel;

    QLabel*           m_finalLabel   = nullptr;
    QPushButton*      m_closeBtn     = nullptr;
};
// SN: 00091
