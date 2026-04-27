// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
//
// pm_tl cross-platform shims for Qt process launching and PATH setup.
// Mirrors the conventions used in gui/BuildProgressDialog.cpp so all three
// platforms behave consistently across the full suite.
//
#pragma once
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>

// Apply macOS Homebrew PATH prefix so ffmpeg and python3 are found in GUI apps
// that launch with a minimal PATH (/usr/bin:/bin only).
inline void applyPlatformEnv(QProcess& proc)
{
#ifdef __APPLE__
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PATH",
               "/usr/local/bin:/opt/homebrew/bin:" + env.value("PATH"));
    proc.setProcessEnvironment(env);
#else
    Q_UNUSED(proc)
#endif
}

// Start a shell command via the platform-native shell.
// Linux/macOS: sh -c <cmd>
// Windows:     cmd.exe /c <cmd>  (setNativeArguments bypasses Qt quoting)
inline void startShellCmd(QProcess& proc, const QString& cmd)
{
    applyPlatformEnv(proc);
#ifdef _WIN32
    proc.setProgram("cmd.exe");
    proc.setNativeArguments("/c " + cmd);
    proc.start();
#else
    proc.start("sh", {"-c", cmd});
#endif
}

// Locate ffmpeg: if the user has a configured path use it, otherwise rely on
// PATH. Returns "ffmpeg" as the bare command if nothing better is found.
inline QString findFfmpeg()
{
#ifdef __APPLE__
    const QStringList candidates = {
        "/usr/local/bin/ffmpeg",
        "/opt/homebrew/bin/ffmpeg",
    };
    for (const QString& p : candidates) {
        if (QFile::exists(p)) return p;
    }
#endif
    return "ffmpeg";
}
// SN: 00106
