// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("pm_tl");
    app.setOrganizationName("Nutball Labs");

    MainWindow w;
    w.show();
    return app.exec();
}
// SN: 00106
