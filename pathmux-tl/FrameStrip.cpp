// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "FrameStrip.h"
#include "compat.hpp"
#include <QPainter>
#include <QPen>
#include <QMouseEvent>
#include <QDir>
#include <algorithm>

FrameStrip::FrameStrip(QWidget* parent) : QWidget(parent)
{
    std::fill(m_frameMs, m_frameMs + kNumFrames, (qint64)0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kStripH);
}

FrameStrip::~FrameStrip()
{
    killProc();
}

void FrameStrip::setUiScale(double s)
{
    m_uiScale = s;
    setFixedHeight((int)(kStripH * m_uiScale));
    update();
}

void FrameStrip::killProc()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
    delete m_proc;
    m_proc = nullptr;
}

void FrameStrip::clearFrames()
{
    killProc();
    for (int i = 0; i < kNumFrames; ++i) m_frames[i] = QPixmap();
    m_loading  = false;
    m_centerMs = -1;
    update();
}

void FrameStrip::loadFrames(const QString& inputPath, qint64 centerMs,
                              qint64 frameDurMs, const QString& ffmpegPath)
{
    killProc();
    m_centerMs   = centerMs;
    m_frameDurMs = frameDurMs > 0 ? frameDurMs : 33;
    m_loading    = true;
    for (int i = 0; i < kNumFrames; ++i) m_frames[i] = QPixmap();

    // Seek to start of the window, clamped to 0; recompute center index.
    qint64 startMs = std::max((qint64)0,
                               centerMs - (qint64)(kNumFrames / 2) * m_frameDurMs);
    m_centerIdx = (int)((centerMs - startMs) / m_frameDurMs);
    m_centerIdx = std::max(0, std::min(kNumFrames - 1, m_centerIdx));
    for (int i = 0; i < kNumFrames; ++i)
        m_frameMs[i] = startMs + (qint64)i * m_frameDurMs;

    update();

    // One ffmpeg invocation extracts all kNumFrames frames starting at startMs.
    QString tmpl = QDir::tempPath() + "/pathmux-tl_strip_%02d.jpg";
    m_proc = new QProcess(this);
    applyPlatformEnv(*m_proc);
    connect(m_proc, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
        m_loading = false;
        if (code == 0) {
            for (int i = 0; i < kNumFrames; ++i) {
                QString p = QString(QDir::tempPath() + "/pathmux-tl_strip_%1.jpg")
                            .arg(i + 1, 2, 10, QChar('0'));
                m_frames[i].load(p);
            }
            emit framesLoaded();
        } else if (status != QProcess::CrashExit) {
            // Genuine failure (not killed by us) — show last meaningful stderr line,
            // not the full ffmpeg banner that precedes every run.
            QString err;
            const QByteArrayList lines = m_proc->readAllStandardError().split('\n');
            for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
                QByteArray t = it->trimmed();
                if (!t.isEmpty()) { err = QString::fromLocal8Bit(t); break; }
            }
            if (err.isEmpty()) err = QString("ffmpeg exited with code %1").arg(code);
            emit extractionFailed(err);
        }
        update();
    });
    m_proc->start(ffmpegPath, {
        "-y",
        "-ss", QString::number(startMs / 1000.0, 'f', 3),
        "-i", inputPath,
        "-frames:v", QString::number(kNumFrames),
        "-q:v", "4",
        tmpl
    });
}

QSize FrameStrip::sizeHint() const
{
    return { 600, (int)(kStripH * m_uiScale) };
}

QSize FrameStrip::minimumSizeHint() const
{
    return { 200, (int)(kStripH * m_uiScale) };
}

void FrameStrip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(28, 28, 28));

    if (m_centerMs < 0 && !m_loading) return;

    int w  = width();
    int h  = height();
    int cw = w / kNumFrames;
    if (cw < 2) return;

    for (int i = 0; i < kNumFrames; ++i) {
        int   x  = i * cw + 1;
        int   iw = (i == kNumFrames - 1) ? (w - x - 1) : (cw - 2);
        QRect cell(x, 2, iw, h - 4);

        if (!m_frames[i].isNull()) {
            QPixmap scaled = m_frames[i].scaled(
                cell.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            int ox = cell.left() + (cell.width()  - scaled.width())  / 2;
            int oy = cell.top()  + (cell.height() - scaled.height()) / 2;
            p.drawPixmap(ox, oy, scaled);
        } else {
            p.fillRect(cell, QColor(48, 48, 48));
        }

        if (i == m_centerIdx) {
            p.setPen(QPen(QColor(255, 200, 0), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(cell.adjusted(-1, -1, 1, 1));
        }
    }

    if (m_loading) {
        p.setPen(QColor(140, 140, 140));
        QFont f = p.font();
        f.setPointSize(7);
        p.setFont(f);
        p.drawText(rect().adjusted(0, 0, -3, -2),
                   Qt::AlignBottom | Qt::AlignRight, "loading\xe2\x80\xa6");
    }
}

void FrameStrip::mousePressEvent(QMouseEvent* ev)
{
    if (m_centerMs < 0) return;
    int cw = width() / kNumFrames;
    if (cw < 1) return;
    int idx = ev->pos().x() / cw;
    idx = std::max(0, std::min(kNumFrames - 1, idx));
    emit frameClicked(m_frameMs[idx]);
}
// SN: 00111
