// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include "trip_detection.hpp"
#include "video_build.hpp"

class QLabel;
class QProgressBar;
class QPushButton;

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
    Pathmux::Trip m_trip;
    VideoOptions  m_opts;

    QLabel*       m_stageLabel;
    QProgressBar* m_progressBar;
    QLabel*       m_etaLabel;
    QPushButton*  m_closeBtn;
};
// SN: 00091
