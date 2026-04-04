// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include <QApplication>
#include <QIcon>
#include <QPixmap>
#include "MainWindow.h"
#include "version.hpp"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("PathMux");
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("Nutball Labs");

    // Under the Adwaita GTK theme (GNOME/Alma 9), Qt widget borders are nearly
    // invisible — same color as the background.  Apply explicit borders globally
    // so group boxes, scroll areas, and frames are readable regardless of theme.
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
        "QScrollArea {"
        "  border: 1px solid #b0b0b0;"
        "}"
        "QFrame[frameShape=\"4\"], QFrame[frameShape=\"5\"] {" // HLine / VLine
        "  color: #909090;"
        "}"
    );

    // Build a multi-resolution icon so the OS can pick the best size.
    QIcon appIcon;
    for (int sz : {16, 32, 48, 64, 128, 256, 512})
        appIcon.addPixmap(QPixmap(QString(":/images/pathmux_%1.png").arg(sz)));
    app.setWindowIcon(appIcon);

    MainWindow w;
    w.show();
    return app.exec();
}
// SN: 00092
