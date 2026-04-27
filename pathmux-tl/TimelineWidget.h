// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QWidget>
#include <QVector>
#include <QString>

// One timelapse section defined by the user.
struct TLMark {
    int     id;
    qint64  startMs;
    qint64  endMs;
    double  targetSecs;   // 0 = not yet configured
};

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void    setDuration(qint64 ms);
    void    setPosition(qint64 ms);
    qint64  duration()  const { return m_duration; }
    qint64  pendingStartMs() const { return m_pendingStartMs; }

    // Zoom / scroll — zoom 1.0 = full video visible; higher = zoomed in.
    double  zoom()        const { return m_zoom; }
    qint64  viewStartMs() const { return m_viewStartMs; }
    void    zoomIn();
    void    zoomOut();
    void    zoomReset();
    void    setViewStartMs(qint64 ms);
    void    scrollToPosition();   // ensure playhead is in view

    const QVector<TLMark>& marks() const { return m_marks; }
    void addMark(qint64 startMs, qint64 endMs);
    void setPendingStart(qint64 ms);
    void setMarkTarget(int id, double targetSecs);
    void deleteMark(int id);
    void clearAllMarks();
    void clearPending();

signals:
    void seekRequested(qint64 ms);
    void frameViewRequested(qint64 ms);
    void markEditRequested(int id);
    void markDeleteRequested(int id);
    void marksChanged();
    void pendingStartChanged(qint64 ms);    // -1 = cleared
    void markClearAllRequested();
    void viewChanged(qint64 viewStartMs, qint64 visibleMs, qint64 totalMs);

protected:
    void paintEvent(QPaintEvent*)        override;
    void mousePressEvent(QMouseEvent*)   override;
    void mouseMoveEvent(QMouseEvent*)    override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*)        override;
    void contextMenuEvent(QContextMenuEvent*) override;
    QSize sizeHint()        const override { return {600, 72}; }
    QSize minimumSizeHint() const override { return {200, 64}; }

private:
    qint64  pxToMs(int px) const;
    int     msToPx(qint64 ms) const;
    qint64  visibleMs() const;
    void    clampView();
    void    emitViewChanged();
    int     markAt(int px) const;
    bool    nearEdge(int markIdx, int px, bool& isStart) const;
    void    buildRulerTicks(QVector<qint64>& major, QVector<qint64>& minor) const;
    QString formatMs(qint64 ms) const;

    static constexpr int    kRulerH   = 20;
    static constexpr int    kTrackH   = 32;
    static constexpr int    kEdgeSlop = 6;
    static constexpr double kZoomStep = 1.5;
    static constexpr double kZoomMax  = 200.0;

    qint64         m_duration       = 0;
    qint64         m_position       = 0;
    qint64         m_pendingStartMs = -1;
    double         m_zoom           = 1.0;
    qint64         m_viewStartMs    = 0;
    QVector<TLMark> m_marks;
    int            m_nextId         = 1;

    enum class DragMode { None, Seek, MoveBody, ResizeStart, ResizeEnd };
    DragMode m_drag         = DragMode::None;
    int      m_dragMark     = -1;
    qint64   m_dragBodyOffMs= 0;
};
// SN: 00106
