// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once

#if defined(Q_OS_LINUX) && defined(CAMCLOPS_HAVE_DBUS)
#  include <QtDBus/QDBusConnection>
#  include <QtDBus/QDBusInterface>
#  include <QtDBus/QDBusReply>
#  include <QtDBus/QDBusUnixFileDescriptor>
#elif defined(Q_OS_WIN)
#  include <windows.h>
#elif defined(Q_OS_DARWIN)
#  include <IOKit/pwr_mgt/IOPMLib.h>
#endif

// Prevents the OS from sleeping or hibernating while the job queue is active.
// Held for the entire queue run (idle→running on first job, running→idle on
// last job) — not per-job.  All methods must be called from the main thread.
class SleepInhibitor {
public:
    SleepInhibitor() = default;
    ~SleepInhibitor() { release(); }

    SleepInhibitor(const SleepInhibitor&)            = delete;
    SleepInhibitor& operator=(const SleepInhibitor&) = delete;

    void acquire() {
#if defined(Q_OS_LINUX) && defined(CAMCLOPS_HAVE_DBUS)
        if (m_fd.isValid()) return;
        QDBusInterface mgr(
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            QDBusConnection::systemBus());
        if (!mgr.isValid()) return;
        QDBusReply<QDBusUnixFileDescriptor> reply =
            mgr.call("Inhibit", "sleep:idle", "CamClops",
                     "Batch job running", "block");
        if (reply.isValid())
            m_fd = reply.value();
#elif defined(Q_OS_WIN)
        if (m_held) return;
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
        m_held = true;
#elif defined(Q_OS_DARWIN)
        if (m_assertionID != kIOPMNullAssertionID) return;
        IOPMAssertionCreateWithName(
            kIOPMAssertionTypeNoIdleSleep,
            kIOPMAssertionLevelOn,
            CFSTR("CamClops batch job running"),
            &m_assertionID);
#endif
    }

    void release() {
#if defined(Q_OS_LINUX) && defined(CAMCLOPS_HAVE_DBUS)
        m_fd = QDBusUnixFileDescriptor();   // close fd → logind drops the lock
#elif defined(Q_OS_WIN)
        if (m_held) {
            SetThreadExecutionState(ES_CONTINUOUS);
            m_held = false;
        }
#elif defined(Q_OS_DARWIN)
        if (m_assertionID != kIOPMNullAssertionID) {
            IOPMAssertionRelease(m_assertionID);
            m_assertionID = kIOPMNullAssertionID;
        }
#endif
    }

private:
#if defined(Q_OS_LINUX) && defined(CAMCLOPS_HAVE_DBUS)
    QDBusUnixFileDescriptor m_fd;
#elif defined(Q_OS_WIN)
    bool m_held = false;
#elif defined(Q_OS_DARWIN)
    IOPMAssertionID m_assertionID = kIOPMNullAssertionID;
#endif
};
// SN: 00115
