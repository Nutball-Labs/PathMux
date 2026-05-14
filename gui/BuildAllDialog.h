// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include "trip_detection.hpp"

class QLabel;
class QProgressBar;
class QPushButton;
class QTextEdit;
class QThread;
class BuildAllWorker;

// Descriptor for one render stage passed in from TripPropertiesDialog.
struct BuildAllStage {
    enum class Type { Script, GpsExtract } type = Type::Script;

    QString     name;           // display: "Extract GPS", "Map Video", etc.
    // Script fields:
    QString     scriptName;     // "clops_maprender.py", etc.
    QString     outputPath;
    int         width  = 3840;
    int         height = 2160;
    QStringList extraArgs;
    QString     manifestKey;    // "mapVideos", "dashVideos", "hudVideos"
    // GpsExtract field:
    QString     exiftoolPath;   // only used when type == GpsExtract
};

class BuildAllDialog : public QDialog {
    Q_OBJECT
public:
    BuildAllDialog(const CamClops::Trip&        trip,
                   const QString&              manifestFile,
                   const QList<BuildAllStage>& stages,
                   QWidget*                    parent = nullptr);
    void startBuild();

signals:
    // Emitted after each stage completes (ok may be false on failure/cancel).
    // Connect to TripPropertiesDialog::appendVideoToManifest via lambda.
    void stageComplete(int idx, const QString& outputPath, bool ok);
    void cancelRequested();

protected:
    void closeEvent(QCloseEvent*) override;
    void reject() override;

private slots:
    void onStageStarted(int idx);
    void onProgress(int idx, int pct, const QString& timeStr, const QString& speedStr);
    void onStageMessage(int idx, const QString& msg);
    void onStageFinished(int idx, bool ok, const QString& error);
    void onAllFinished(bool allOk);
    void onCancel();

private:
    CamClops::Trip          m_trip;
    QString                m_manifestFile;
    QList<BuildAllStage>   m_stages;
    bool                   m_done = false;

    struct StageRow {
        QProgressBar* bar    = nullptr;
        QLabel*       status = nullptr;
    };
    QVector<StageRow>  m_rows;
    QTextEdit*         m_log        = nullptr;
    QPushButton*       m_cancelBtn  = nullptr;
    QPushButton*       m_closeBtn   = nullptr;
    QLabel*            m_finalLabel = nullptr;

    BuildAllWorker*    m_worker  = nullptr;
    QThread*           m_thread  = nullptr;
};
// SN: 00106
