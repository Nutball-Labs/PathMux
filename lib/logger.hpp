// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>
#include "compat.hpp"

namespace CamClops {

// ---------------------------------------------------------------------------
// LogLevel — controls what gets written to the log file.
//   OFF    : Nothing written (default).
//   NORMAL : External command invocations + exit codes + key decisions + errors.
//   DEBUG  : Everything in NORMAL plus full ffmpeg/exiftool captured output
//            and per-segment detail.
// ---------------------------------------------------------------------------
enum class LogLevel { OFF, NORMAL, DEBUG };

// ---------------------------------------------------------------------------
// Logger — singleton-style log writer.
// Call Logger::instance() to get the global instance.
// Call open() once at startup after config is loaded.
// Call log() / logCommand() throughout the codebase.
//
// Log file location: ~/.config/camclops/camclops.log
// On each open(), previous log is rotated to camclops.log.1 (1 backup kept).
// ---------------------------------------------------------------------------
class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    // Open log file. Rotates previous log to .1 before opening.
    // No-op if level is OFF.
    void open(const std::string& configDir, LogLevel level) {
        level_    = level;
        if (level_ == LogLevel::OFF) return;

        logPath_  = configDir + "/camclops.log";
        std::string bakPath = configDir + "/camclops.log.1";

        // Rotate: .log -> .log.1 (overwrite previous .1)
        std::rename(logPath_.c_str(), bakPath.c_str());

        ofs_.open(logPath_, std::ios::out | std::ios::trunc);
        if (!ofs_.is_open()) {
            level_ = LogLevel::OFF;  // can't open log — silently disable
            return;
        }
        write("=== CamClops log opened ===");
        write("Log level: " + levelName(level_));
    }

    // Write a message at the given level.
    // Only written if msg_level <= current level.
    void log(LogLevel msgLevel, const std::string& msg) {
        if (level_ == LogLevel::OFF) return;
        if (msgLevel == LogLevel::DEBUG && level_ != LogLevel::DEBUG) return;
        write(msg);
    }

    // Convenience wrappers
    void normal(const std::string& msg) { log(LogLevel::NORMAL, msg); }
    void debug(const std::string& msg)  { log(LogLevel::DEBUG,  msg); }

    // Log an external command invocation and its result.
    // capturedOutput: only populated (and logged) in DEBUG mode.
    void logCommand(const std::string& cmd,
                    int exitCode,
                    const std::string& capturedOutput = "") {
        if (level_ == LogLevel::OFF) return;
        write("CMD: " + cmd);
        write("EXIT: " + std::to_string(exitCode));
        if (level_ == LogLevel::DEBUG && !capturedOutput.empty()) {
            write("--- output ---");
            write(capturedOutput);
            write("--- end output ---");
        }
    }

    LogLevel level() const { return level_; }
    bool isDebug()   const { return level_ == LogLevel::DEBUG; }
    bool isActive()  const { return level_ != LogLevel::OFF; }

private:
    Logger() = default;
    ~Logger() { if (ofs_.is_open()) { write("=== CamClops log closed ==="); } }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void write(const std::string& msg) {
        if (!ofs_.is_open()) return;
        ofs_ << timestamp() << "  " << msg << "\n";
        ofs_.flush();
    }

    std::string timestamp() {
        auto now  = std::chrono::system_clock::now();
        auto tt   = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&tt, &tm);
        char buf[24];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return buf;
    }

    static std::string levelName(LogLevel l) {
        switch (l) {
            case LogLevel::NORMAL: return "NORMAL";
            case LogLevel::DEBUG:  return "DEBUG";
            default:               return "OFF";
        }
    }

    LogLevel     level_   = LogLevel::OFF;
    std::string  logPath_;
    std::ofstream ofs_;
};

} // namespace CamClops

// ---------------------------------------------------------------------------
// Convenience macros — use these throughout the codebase.
// Fully qualified so they work both inside and outside namespace CamClops.
// ---------------------------------------------------------------------------
#define LOG_NORMAL(msg) CamClops::Logger::instance().normal(msg)
#define LOG_DEBUG(msg)  CamClops::Logger::instance().debug(msg)
#define LOG_CMD(cmd, exit, out) CamClops::Logger::instance().logCommand(cmd, exit, out)

#endif
// SN: 00089
