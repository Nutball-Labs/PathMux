// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
//
// pm_tl cross-platform shims for Qt process launching and PATH setup.
// Mirrors the conventions used in gui/BuildProgressDialog.cpp so all three
// platforms behave consistently across the full suite.
//
#pragma once
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSysInfo>
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

// Read the ffmpegPath key from pathmux's per-host settings file.
// Returns empty string if the file is absent or has no ffmpegPath entry.
inline QString readPathmuxFfmpegPath()
{
    QString host = QSysInfo::machineHostName();
    int dot = host.indexOf('.');
    if (dot > 0) host = host.left(dot);
    QString path = QDir::homePath() + "/.config/pathmux/pathmux_" + host + ".json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return {};
    return doc.object().value("ffmpegPath").toString();
}

// Locate ffmpeg: prefer the path configured in pathmux host settings,
// then fall back to Homebrew candidates (macOS) or bare PATH lookup.
inline QString findFfmpeg()
{
    QString configured = readPathmuxFfmpegPath();
    if (!configured.isEmpty()) return configured;
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

inline QString findFfprobe()
{
#ifdef __APPLE__
    const QStringList candidates = {
        "/usr/local/bin/ffprobe",
        "/opt/homebrew/bin/ffprobe",
    };
    for (const QString& p : candidates) {
        if (QFile::exists(p)) return p;
    }
#endif
    return "ffprobe";
}
// SN: 00107
