// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <QCloseEvent>
#include <QVector>
#include <QMap>
#include <QString>
#include "trip_detection.hpp"
#include "video_build.hpp"

class BuildWorker;

class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QTextEdit;
class QVBoxLayout;

// One row in the stage stack — name, bar, live status (ETA → ✓ when done).
// Group rows (isGroup == true) own a collapsible child area for chunk detail.
struct StageRow {
    QString       label;
    QProgressBar* bar          = nullptr;
    QLabel*       status       = nullptr;
    bool          started      = false;
    bool          failed       = false;
    // Group-row fields — null / 0 for normal rows
    bool          isGroup      = false;
    QPushButton*  toggleBtn    = nullptr;  // ▶ / ▼
    QWidget*      childArea    = nullptr;  // collapsible child container
    QVBoxLayout*  childLayout  = nullptr;
    int           totalChunks  = 0;
    int           doneChunks   = 0;
};

class BuildProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit BuildProgressDialog(const CamClops::Trip& trip,
                                 const VideoOptions&  opts,
                                 QWidget*             parent = nullptr);
    void startBuild();

protected:
    void closeEvent(QCloseEvent* event) override;

signals:
    void buildComplete(bool ok);

private slots:
    // msg is non-empty only on failure — carries first line of ffmpeg stderr
    void onProgress(const QString& label, int pct, int etaSecs,
                    const QString& msg = "");
    void onFinished(bool ok, const QString& error);

private:
    void addStageRow(const QString& label,
                     QVBoxLayout* parentLayout = nullptr,
                     bool indented = false);
    void addGroupRow(const QString& camera, int nChunks);
    void addChildRow(const QString& groupCamera, const QString& label);
    void updateGroupAggregate(const QString& groupCamera);
    void populateExpectedRows();

    CamClops::Trip     m_trip;
    VideoOptions      m_opts;

    QScrollArea*      m_scrollArea   = nullptr;
    QVBoxLayout*      m_stageLayout  = nullptr;
    QVector<StageRow> m_rows;
    QMap<QString,int> m_rowIndex;    // label → index in m_rows
    QMap<QString,int> m_groupIndex;  // camera → index of group header row in m_rows
    QMap<QString,QString> m_childToGroup; // child label → group camera

    QLabel*           m_outputDirLabel = nullptr;  // shows resolved output path
    QLabel*           m_finalLabel     = nullptr;
    QPushButton*      m_closeBtn       = nullptr;
    QTextEdit*        m_verboseLog     = nullptr;  // non-null only when opts.verbose

    BuildWorker*      m_worker        = nullptr;
    bool              m_buildRunning  = false;
};
// SN: 00116
