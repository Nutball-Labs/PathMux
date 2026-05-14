// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <QString>
#include <QStringList>
#include "trip_detection.hpp"

class MapWorker;
class QLabel;
class QProgressBar;
class QPushButton;
class QTextEdit;
class QThread;

class MapProgressDialog : public QDialog {
    Q_OBJECT
public:
    // scriptName: filename of the Python script to invoke (e.g. "clops_dashboard.py").
    // extraArgs:  additional arguments appended after the standard --manifest/--trip/…
    MapProgressDialog(const CamClops::Trip& trip,
                      const QString&       manifestFile,
                      const QString&       outputPath,
                      int                  width,
                      int                  height,
                      QWidget*             parent     = nullptr,
                      const QString&       scriptName = "clops_maprender.py",
                      const QString&       title      = "Generating Map",
                      const QStringList&   extraArgs  = {});

    void startRender();

signals:
    void renderComplete(bool ok);
    void cancelRequested();   // connected to MapWorker::cancel()

protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

private slots:
    void onProgress(int pct, const QString& timeStr, const QString& speedStr);
    void onStatusMessage(const QString& msg);
    void onFinished(bool ok, const QString& error);
    void onCancel();

private:
    QString findScript() const;   // locate m_scriptName next to the binary

    CamClops::Trip m_trip;
    QString       m_manifestFile;
    QString       m_outputPath;
    int           m_width;
    int           m_height;
    QString       m_scriptName;
    QStringList   m_extraArgs;

    bool          m_done        = false;   // set when worker emits finished
    MapWorker*    m_worker      = nullptr;
    QThread*      m_thread      = nullptr;

    QTextEdit*    m_statusLog   = nullptr;
    QProgressBar* m_bar         = nullptr;
    QLabel*       m_timeLabel   = nullptr;
    QLabel*       m_finalLabel  = nullptr;
    QPushButton*  m_cancelBtn   = nullptr;
    QPushButton*  m_closeBtn    = nullptr;
};
// SN: 00104
