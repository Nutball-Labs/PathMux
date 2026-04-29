// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include <QApplication>
#include <QFile>
#include <QLoggingCategory>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
#ifdef __linux__
    // Qt6 Multimedia's FFmpeg backend probes VA-API and VDPAU for HW decode.
    // On Intel iGPU configs where vaExportSurfaceHandle fails, the fallback
    // path corrupts the heap in the texture upload path and crashes the app.
    // Force software decode — acceptable for a UI preview tool.
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "");
#endif

    // Suppress known-noisy Qt log categories before QApplication so the
    // messages don't appear even during early initialisation.
    //   qt.multimedia.ffmpeg* — VAAPI/VDPAU/CUDA symbol-resolver probes
    //   qt.qpa.xcb*           — Adwaita decoration "not found" noise
    QLoggingCategory::setFilterRules(
        "qt.multimedia.ffmpeg*=false\n"
        "qt.qpa.xcb*=false\n"
    );

    QApplication app(argc, argv);
    app.setApplicationName("pathmux-tl");
    app.setOrganizationName("Nutball Labs");

    // Same Adwaita border fix as pathmux-gui — under the GNOME/Alma 9 theme
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

    if (argc > 1) {
        QString path = QString::fromLocal8Bit(argv[1]);
        if (QFile::exists(path))
            w.openFile(path);
    }

    w.show();
    return app.exec();
}
// SN: 00106
