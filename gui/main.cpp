// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include <QApplication>
#include <QIcon>
#include <QPixmap>
#include <QLoggingCategory>
#include <cstdio>
#include <string>
#include "MainWindow.h"
#include "SetupWizard.h"
#include "config_manager.hpp"
#include "version.hpp"

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
    // Scan for --verbose before QApplication so the handler is in place early.
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) == "--verbose") {
            qInstallMessageHandler(verboseHandler);
            QLoggingCategory::setFilterRules("*.debug=true\n");
            fprintf(stderr, "[INF] pathmux-gui verbose mode\n");
            break;
        }
    }

    // Apply UI scale BEFORE QApplication — Qt bakes DPI metrics at startup.
    // Priority: QT_SCALE_FACTOR env var > --scale arg > uiScale in settings.
    if (!qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) {
        double scale = 0.0;

        // Check argv for --scale N or --scale=N
        for (int i = 1; i < argc && scale == 0.0; ++i) {
            std::string a = argv[i];
            if (a == "--scale" && i + 1 < argc) {
                try { scale = std::stod(argv[i + 1]); } catch (...) {}
            } else if (a.size() > 8 && a.substr(0, 8) == "--scale=") {
                try { scale = std::stod(a.substr(8)); } catch (...) {}
            }
        }

        // Fall back to uiScale in settings (ConfigManager is Qt-free; safe before QApplication)
        if (scale == 0.0) {
            Pathmux::ConfigManager preConfig;
            scale = preConfig.getUiScale();
        }

        if (scale > 0.5 && scale != 1.0) {
            // Format cleanly: "1.5" not "1.500000"
            std::string s = std::to_string(scale);
            auto dot = s.find('.');
            if (dot != std::string::npos) {
                auto last = s.find_last_not_of('0');
                s = (last > dot) ? s.substr(0, last + 1) : s.substr(0, dot + 2);
            }
            qputenv("QT_SCALE_FACTOR", s.c_str());
        }
    }

    QLoggingCategory::setFilterRules("qt.qpa.xcb*=false\n");

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
        // Give all top-level windows a visible inner border so window edges are
        // clear under GNOME/Adwaita compositing which renders nearly-invisible frames.
        "QDialog, QMainWindow {"
        "  border: 2px solid #5a5a5a;"
        "}"
    );

    // Build a multi-resolution icon so the OS can pick the best size.
    QIcon appIcon;
    for (int sz : {16, 32, 48, 64, 128, 256, 512})
        appIcon.addPixmap(QPixmap(QString(":/images/pathmux_%1.png").arg(sz)));
    app.setWindowIcon(appIcon);

    // First-run wizard: launch if no valid config exists yet.
    {
        Pathmux::ConfigManager cfg;
        cfg.loadSettings();
        if (cfg.isFirstRun()) {
            SetupWizard wiz;
            wiz.exec();   // saves settings internally on Finish; no-op if cancelled
        }
    }

    MainWindow w;
    w.show();
    return app.exec();
}
// SN: 00109
