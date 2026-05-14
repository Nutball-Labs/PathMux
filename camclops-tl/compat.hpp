// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
//
// camclops-tl cross-platform shims for Qt process launching and PATH setup.
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
#include <string>
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#else
#  include <unistd.h>
#endif

// NULL_REDIRECT — suppress stderr in popen() shell commands.
#ifdef _WIN32
#  define NULL_REDIRECT "2>NUL"
#else
#  define NULL_REDIRECT "2>/dev/null"
#endif

// Windows popen/pclose shims: suppress console window on each child process.
#ifdef _WIN32
inline FILE* tlWindowsPopen(const char* cmd, const char* /*mode*/) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return nullptr;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hWrite;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    si.wShowWindow = SW_HIDE;
    std::string cmdStr = std::string("cmd.exe /c ") + cmd;
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(nullptr, cmdStr.data(), nullptr, nullptr,
                             TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (!ok) { CloseHandle(hRead); return nullptr; }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hRead), _O_RDONLY);
    if (fd == -1) { CloseHandle(hRead); return nullptr; }
    return _fdopen(fd, "r");
}
inline int tlWindowsPclose(FILE* f) { if (f) fclose(f); return 0; }
#  undef  popen
#  undef  pclose
#  define popen  tlWindowsPopen
#  define pclose tlWindowsPclose
#endif

// getShortHostname — machine hostname without domain suffix.
inline std::string getShortHostname() {
#ifdef _WIN32
    char buf[256] = {};
    DWORD sz = sizeof(buf);
    if (GetComputerNameA(buf, &sz)) return std::string(buf);
    return "localhost";
#else
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf)) == 0) {
        std::string hn = buf;
        auto dot = hn.find('.');
        if (dot != std::string::npos) hn = hn.substr(0, dot);
        return hn;
    }
    return "localhost";
#endif
}

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

// Read the ffmpegPath key from camclops's per-host settings file.
// Returns empty string if the file is absent or has no ffmpegPath entry.
inline QString readCamClopsFfmpegPath()
{
    QString host = QSysInfo::machineHostName();
    int dot = host.indexOf('.');
    if (dot > 0) host = host.left(dot);
    QString path = QDir::homePath() + "/.config/camclops/camclops_" + host + ".json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return {};
    return doc.object().value("ffmpegPath").toString();
}

// Locate ffmpeg: prefer the path configured in camclops host settings,
// then fall back to Homebrew candidates (macOS) or bare PATH lookup.
inline QString findFfmpeg()
{
    QString configured = readCamClopsFfmpegPath();
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
// SN: 00117
