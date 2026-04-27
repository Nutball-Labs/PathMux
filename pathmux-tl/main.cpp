// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("pathmux-tl");
    app.setOrganizationName("Nutball Labs");

    MainWindow w;

    // Optional: open a file passed on the command line so pathmux-gui (or
    // the shell) can launch directly into a specific collage MP4.
    if (argc > 1) {
        QString path = QString::fromLocal8Bit(argv[1]);
        if (QFile::exists(path))
            w.openFile(path);
    }

    w.show();
    return app.exec();
}
// SN: 00106
