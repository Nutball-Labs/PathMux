// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QWidget>
#include <QPixmap>
#include <QProcess>

// Horizontal filmstrip showing kNumFrames video thumbnails centred on the
// current playhead position.  Click any cell to seek to that frame.
class FrameStrip : public QWidget {
    Q_OBJECT
public:
    explicit FrameStrip(QWidget* parent = nullptr);
    ~FrameStrip() override;

    void setUiScale(double s);
    void loadFrames(const QString& inputPath, qint64 centerMs,
                    qint64 frameDurMs, const QString& ffmpegPath);
    void clearFrames();

signals:
    void frameClicked(qint64 ms);
    void framesLoaded();
    void extractionFailed(const QString& errText);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    QSize sizeHint()        const override;
    QSize minimumSizeHint() const override;

private:
    void killProc();

    static constexpr int kNumFrames = 13;   // 6 before + center + 6 after
    static constexpr int kStripH    = 82;   // base height px at uiScale 1.0

    double    m_uiScale    = 1.0;
    qint64    m_centerMs   = -1;
    qint64    m_frameDurMs = 33;
    QPixmap   m_frames[kNumFrames];
    qint64    m_frameMs[kNumFrames];
    int       m_centerIdx  = 6;
    bool      m_loading    = false;
    QProcess* m_proc       = nullptr;
};
// SN: 00111
