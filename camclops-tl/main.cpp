// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include <QApplication>
#include <QFile>
#include <QLoggingCategory>
#include <cstdio>
#include "MainWindow.h"

static void verboseHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    const char* t = "DBG";
    switch (type) {
        case QtInfoMsg:     t = "INF"; break;
        case QtWarningMsg:  t = "WRN"; break;
        case QtCriticalMsg: t = "CRT"; break;
        case QtFatalMsg:    t = "FAT"; break;
        default: break;
    }
    fprintf(stderr, "[%s] %s\n", t, msg.toLocal8Bit().constData());
    if (type == QtFatalMsg) abort();
}

int main(int argc, char* argv[])
{
    // Scan for --debug / --verbose before QApplication.
    bool debugMode = false;
    for (int i = 1; i < argc; ++i) {
        QByteArray a(argv[i]);
        if (a == "--debug" || a == "--verbose") { debugMode = true; break; }
    }

#ifdef __linux__
    // Qt6 Multimedia's FFmpeg backend probes VA-API and VDPAU for HW decode.
    // On Intel iGPU configs where vaExportSurfaceHandle fails, the fallback
    // path corrupts the heap in the texture upload path and crashes the app.
    // Force software decode — acceptable for a UI preview tool.
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "");
#endif

    if (debugMode) {
        qInstallMessageHandler(verboseHandler);
        QLoggingCategory::setFilterRules("*.debug=true\n");
        fprintf(stderr, "[INF] camclops-tl verbose mode\n");
    } else {
        // Suppress known-noisy Qt log categories before QApplication.
        //   qt.multimedia.ffmpeg* — VAAPI/VDPAU/CUDA symbol-resolver probes
        //   qt.qpa.xcb*           — Adwaita decoration "not found" noise
        QLoggingCategory::setFilterRules(
            "qt.multimedia.ffmpeg*=false\n"
            "qt.qpa.xcb*=false\n"
        );
    }

    QApplication app(argc, argv);
    app.setApplicationName("camclops-tl");
    app.setOrganizationName("Nutball Labs");

    // Same Adwaita border fix as camclops-gui — under the GNOME/Alma 9 theme
    // widget borders are nearly invisible without explicit styling.
    app.setStyleSheet(
        "QGroupBox {"
        "  border: 1px solid #909090;"
        "  border-radius: 4px;"
        "  margin-top: 8px;"
        "  font-weight: bold;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  subcontrol-position: top left;"
        "  left: 8px;"
        "  padding: 0 4px;"
        "}"
        "QScrollArea { border: 1px solid #b0b0b0; }"
        "QFrame[frameShape=\"4\"], QFrame[frameShape=\"5\"] { color: #909090; }"
    );

    MainWindow w;

    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith("--")) continue;
        if (QFile::exists(arg)) { w.openFile(arg); break; }
    }

    w.show();
    return app.exec();
}
// SN: 00109
