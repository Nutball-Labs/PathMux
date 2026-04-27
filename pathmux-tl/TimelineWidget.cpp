// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "TimelineWidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <algorithm>
#include <cmath>

TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kRulerH + kTrackH + 8);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void TimelineWidget::setDuration(qint64 ms)
{
    m_duration = ms;
    m_marks.clear();
    m_nextId = 1;
    m_pendingStartMs = -1;
    m_zoom = 1.0;
    m_viewStartMs = 0;
    update();
    emit pendingStartChanged(-1);
    emitViewChanged();
}

void TimelineWidget::setPosition(qint64 ms)
{
    m_position = ms;
    update();
}

void TimelineWidget::addMark(qint64 startMs, qint64 endMs)
{
    if (endMs <= startMs + 500) return;
    bool overlap = std::any_of(m_marks.begin(), m_marks.end(),
        [startMs, endMs](const TLMark& m){
            return startMs < m.endMs && endMs > m.startMs; });
    if (overlap) return;
    TLMark mk;
    mk.id         = m_nextId++;
    mk.startMs    = startMs;
    mk.endMs      = endMs;
    mk.targetSecs = 0;
    m_marks.append(mk);
    std::sort(m_marks.begin(), m_marks.end(),
        [](const TLMark& a, const TLMark& b){ return a.startMs < b.startMs; });
    update();
    emit marksChanged();
}

void TimelineWidget::setMarkTarget(int id, double targetSecs)
{
    for (auto& m : m_marks)
        if (m.id == id) { m.targetSecs = targetSecs; break; }
    update();
    emit marksChanged();
}

void TimelineWidget::deleteMark(int id)
{
    m_marks.erase(std::remove_if(m_marks.begin(), m_marks.end(),
        [id](const TLMark& m){ return m.id == id; }), m_marks.end());
    update();
    emit marksChanged();
}

void TimelineWidget::setPendingStart(qint64 ms)
{
    m_pendingStartMs = ms;
    update();
    emit pendingStartChanged(ms);
}

void TimelineWidget::clearAllMarks()
{
    m_marks.clear();
    m_nextId = 1;
    update();
    emit marksChanged();
}

void TimelineWidget::clearPending()
{
    m_pendingStartMs = -1;
    update();
    emit pendingStartChanged(-1);
}

// ---------------------------------------------------------------------------
// Zoom / scroll
// ---------------------------------------------------------------------------
qint64 TimelineWidget::visibleMs() const
{
    return (m_duration > 0) ? (qint64)(m_duration / m_zoom) : 0;
}

void TimelineWidget::clampView()
{
    if (m_duration <= 0) return;
    qint64 vis = visibleMs();
    m_viewStartMs = std::max((qint64)0,
                             std::min(m_viewStartMs, m_duration - vis));
}

void TimelineWidget::emitViewChanged()
{
    emit viewChanged(m_viewStartMs, visibleMs(), m_duration);
}

void TimelineWidget::zoomIn()
{
    if (m_duration <= 0) return;
    // Zoom centered on the current playhead if visible, else centre of view.
    qint64 pivot = (m_position >= m_viewStartMs &&
                    m_position <= m_viewStartMs + visibleMs())
                   ? m_position
                   : m_viewStartMs + visibleMs() / 2;
    double newZoom = std::min(m_zoom * kZoomStep, kZoomMax);
    // Cap so we don't zoom in past 1 second visible.
    double minVis = 1000.0;
    newZoom = std::min(newZoom, m_duration / minVis);
    if (newZoom <= m_zoom) return;
    m_zoom = newZoom;
    m_viewStartMs = pivot - (qint64)(visibleMs() / 2);
    clampView();
    update();
    emitViewChanged();
}

void TimelineWidget::zoomOut()
{
    if (m_duration <= 0) return;
    double newZoom = std::max(m_zoom / kZoomStep, 1.0);
    if (newZoom >= m_zoom) return;
    qint64 pivot = m_viewStartMs + visibleMs() / 2;
    m_zoom = newZoom;
    m_viewStartMs = pivot - (qint64)(visibleMs() / 2);
    clampView();
    update();
    emitViewChanged();
}

void TimelineWidget::zoomReset()
{
    m_zoom = 1.0;
    m_viewStartMs = 0;
    update();
    emitViewChanged();
}

void TimelineWidget::setViewStartMs(qint64 ms)
{
    m_viewStartMs = ms;
    clampView();
    update();
}

void TimelineWidget::scrollToPosition()
{
    if (m_duration <= 0 || m_zoom <= 1.0) return;
    qint64 vis = visibleMs();
    if (m_position < m_viewStartMs || m_position > m_viewStartMs + vis) {
        m_viewStartMs = m_position - vis / 2;
        clampView();
        update();
        emitViewChanged();
    }
}

// ---------------------------------------------------------------------------
// Coordinate helpers  (zoom-aware)
// ---------------------------------------------------------------------------
qint64 TimelineWidget::pxToMs(int px) const
{
    if (m_duration <= 0 || width() <= 0) return 0;
    qint64 vis = visibleMs();
    double t = (double)std::max(0, std::min(px, width())) / width();
    qint64 ms = m_viewStartMs + (qint64)(t * vis);
    return std::max((qint64)0, std::min(ms, m_duration));
}

int TimelineWidget::msToPx(qint64 ms) const
{
    if (m_duration <= 0) return 0;
    qint64 vis = visibleMs();
    if (vis <= 0) return 0;
    return (int)((double)(ms - m_viewStartMs) / vis * width());
}

int TimelineWidget::markAt(int px) const
{
    for (int i = 0; i < m_marks.size(); ++i) {
        int x0 = msToPx(m_marks[i].startMs);
        int x1 = msToPx(m_marks[i].endMs);
        if (px >= x0 && px <= x1) return i;
    }
    return -1;
}

bool TimelineWidget::nearEdge(int idx, int px, bool& isStart) const
{
    if (idx < 0 || idx >= m_marks.size()) return false;
    int x0 = msToPx(m_marks[idx].startMs);
    int x1 = msToPx(m_marks[idx].endMs);
    if (std::abs(px - x0) <= kEdgeSlop) { isStart = true;  return true; }
    if (std::abs(px - x1) <= kEdgeSlop) { isStart = false; return true; }
    return false;
}

QString TimelineWidget::formatMs(qint64 ms) const
{
    if (ms < 0) ms = 0;
    int h = (int)(ms / 3600000);
    int m = (int)((ms % 3600000) / 60000);
    int s = (int)((ms % 60000)   / 1000);
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
    return QString("%1:%2").arg(m).arg(s,2,10,QChar('0'));
}

void TimelineWidget::buildRulerTicks(QVector<qint64>& major,
                                     QVector<qint64>& minor) const
{
    if (m_duration <= 0) return;
    double secPerPx    = (double)m_duration / 1000.0 / width();
    double targetIntS  = 80.0 * secPerPx;
    const double steps[] = {5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600};
    double chosen = 3600;
    for (double s : steps) {
        if (s >= targetIntS) { chosen = s; break; }
    }
    double minorInt = chosen / 5.0;
    for (double t = 0; t * 1000 <= m_duration; t += chosen)
        major.append((qint64)(t * 1000));
    for (double t = 0; t * 1000 <= m_duration; t += minorInt) {
        qint64 ms = (qint64)(t * 1000);
        bool isMajor = std::any_of(major.begin(), major.end(),
            [ms](qint64 maj){ return std::abs(maj - ms) < 100; });
        if (!isMajor) minor.append(ms);
    }
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------
void TimelineWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int W  = width();
    const int ry = 0;
    const int ty = kRulerH;

    p.fillRect(rect(), QColor(0x2a, 0x2a, 0x2a));

    if (m_duration <= 0) {
        p.setPen(QColor(0x88, 0x88, 0x88));
        p.drawText(rect(), Qt::AlignCenter, "Open an MP4 file to begin");
        return;
    }

    // Ruler
    p.fillRect(0, ry, W, kRulerH, QColor(0x1a, 0x1a, 0x1a));
    QVector<qint64> major, minor;
    buildRulerTicks(major, minor);
    p.setPen(QColor(0x66, 0x66, 0x66));
    for (qint64 ms : minor) {
        int x = msToPx(ms);
        p.drawLine(x, ry + kRulerH - 4, x, ry + kRulerH);
    }
    p.setPen(QColor(0xaa, 0xaa, 0xaa));
    QFont f = font(); f.setPointSize(7); p.setFont(f);
    for (qint64 ms : major) {
        int x = msToPx(ms);
        p.drawLine(x, ry, x, ry + kRulerH);
        p.drawText(QRect(x + 2, ry, 60, kRulerH - 2),
                   Qt::AlignVCenter | Qt::AlignLeft, formatMs(ms));
    }

    // Track background
    p.fillRect(0, ty, W, kTrackH, QColor(0x3a, 0x3a, 0x3a));

    // Pending start indicator — green shaded region from pending to end of track
    if (m_pendingStartMs >= 0) {
        int px = msToPx(m_pendingStartMs);
        p.fillRect(px, ty, W - px, kTrackH, QColor(0x27, 0xae, 0x60, 50));
        p.setPen(QPen(QColor(0x27, 0xae, 0x60), 2, Qt::DashLine));
        p.drawLine(px, ty, px, ty + kTrackH);
        // Label
        p.setPen(QColor(0x27, 0xae, 0x60));
        f.setPointSize(7); p.setFont(f);
        p.drawText(QRect(px + 4, ty, 120, kTrackH),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QString("Start: %1").arg(formatMs(m_pendingStartMs)));
    }

    // Marks
    for (const auto& m : m_marks) {
        int x0 = msToPx(m.startMs);
        int x1 = msToPx(m.endMs);
        bool configured = (m.targetSecs > 0);
        QColor fill = configured ? QColor(0xe6, 0x7e, 0x22, 200)
                                 : QColor(0x27, 0xae, 0x60, 160);
        p.fillRect(x0, ty, x1 - x0, kTrackH, fill);
        p.fillRect(x0,      ty, 3, kTrackH, fill.lighter(150));
        p.fillRect(x1 - 3,  ty, 3, kTrackH, fill.lighter(150));
        if (x1 - x0 > 30) {
            p.setPen(Qt::white);
            f.setPointSize(7); p.setFont(f);
            QString lbl = configured
                ? QString("\xe2\x86\x92%1s").arg(m.targetSecs, 0, 'f', 1)
                : "TL (unconfigured)";
            p.drawText(QRect(x0 + 4, ty, x1 - x0 - 8, kTrackH),
                       Qt::AlignVCenter | Qt::AlignLeft, lbl);
        }
    }

    // Playhead
    int hx = msToPx(m_position);
    p.setPen(QPen(QColor(0xff, 0xdd, 0x00), 2));
    p.drawLine(hx, 0, hx, height());
    QPolygon tri;
    tri << QPoint(hx - 5, 0) << QPoint(hx + 5, 0) << QPoint(hx, 8);
    p.setBrush(QColor(0xff, 0xdd, 0x00));
    p.setPen(Qt::NoPen);
    p.drawPolygon(tri);
}

// ---------------------------------------------------------------------------
// Mouse — left-click seeks; drag on mark body/edge moves/resizes
// ---------------------------------------------------------------------------
void TimelineWidget::mousePressEvent(QMouseEvent* e)
{
    if (m_duration <= 0 || e->button() != Qt::LeftButton) return;
    int px = e->pos().x();
    qint64 ms = pxToMs(px);
    int mi = markAt(px);
    if (mi >= 0) {
        bool isStart = false;
        if (nearEdge(mi, px, isStart)) {
            m_drag = isStart ? DragMode::ResizeStart : DragMode::ResizeEnd;
        } else {
            m_drag = DragMode::MoveBody;
            m_dragBodyOffMs = ms - m_marks[mi].startMs;
        }
        m_dragMark = mi;
    } else {
        m_drag = DragMode::Seek;
        emit seekRequested(ms);
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (m_duration <= 0) return;
    int px = e->pos().x();
    qint64 ms = pxToMs(px);

    if (m_drag == DragMode::Seek) {
        emit seekRequested(ms);
    } else if (m_drag == DragMode::ResizeStart && m_dragMark >= 0) {
        auto& mk = m_marks[m_dragMark];
        mk.startMs = std::clamp(ms, (qint64)0, mk.endMs - 500);
        update();
    } else if (m_drag == DragMode::ResizeEnd && m_dragMark >= 0) {
        auto& mk = m_marks[m_dragMark];
        mk.endMs = std::clamp(ms, mk.startMs + 500, m_duration);
        update();
    } else if (m_drag == DragMode::MoveBody && m_dragMark >= 0) {
        auto& mk = m_marks[m_dragMark];
        qint64 dur = mk.endMs - mk.startMs;
        mk.startMs = std::clamp(ms - m_dragBodyOffMs, (qint64)0, m_duration - dur);
        mk.endMs   = mk.startMs + dur;
        update();
    } else {
        int mi = markAt(px);
        if (mi >= 0) {
            bool isStart = false;
            setCursor(nearEdge(mi, px, isStart) ? Qt::SizeHorCursor : Qt::SizeAllCursor);
        } else {
            setCursor(Qt::CrossCursor);
        }
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    if (m_drag == DragMode::ResizeStart || m_drag == DragMode::ResizeEnd ||
        m_drag == DragMode::MoveBody)
        emit marksChanged();
    m_drag     = DragMode::None;
    m_dragMark = -1;
}

// ---------------------------------------------------------------------------
// Wheel: Ctrl+scroll zooms centred on cursor; plain scroll pans
// ---------------------------------------------------------------------------
void TimelineWidget::wheelEvent(QWheelEvent* e)
{
    if (m_duration <= 0) return;
    int delta = e->angleDelta().y();
    if (delta == 0) return;

    if (e->modifiers() & Qt::ControlModifier) {
        // Zoom centred on cursor position
        qint64 pivot = pxToMs(e->position().x());
        double factor = (delta > 0) ? kZoomStep : 1.0 / kZoomStep;
        double newZoom = std::clamp(m_zoom * factor, 1.0, kZoomMax);
        double minVis = 1000.0;
        newZoom = std::min(newZoom, m_duration / minVis);
        if (newZoom == m_zoom) return;
        m_zoom = newZoom;
        m_viewStartMs = pivot - (qint64)(visibleMs() / 2);
        clampView();
        update();
        emitViewChanged();
    } else {
        // Pan — scroll 10 % of visible range per notch
        if (m_zoom <= 1.0) return;
        qint64 step = visibleMs() / 10;
        m_viewStartMs += (delta < 0) ? step : -step;
        clampView();
        update();
        emitViewChanged();
    }
    e->accept();
}

// ---------------------------------------------------------------------------
// Right-click context menu
// ---------------------------------------------------------------------------
void TimelineWidget::contextMenuEvent(QContextMenuEvent* e)
{
    if (m_duration <= 0) return;

    int    px = e->pos().x();
    qint64 ms = pxToMs(px);
    int    mi = markAt(px);

    QMenu menu(this);

    // Frame View — always
    auto* frameAct = menu.addAction(
        QString("Frame View at %1").arg(formatMs(ms)));
    menu.addSeparator();

    // Timelapse mark workflow
    auto* startAct = menu.addAction("Start Timelapse Here");
    auto* stopAct  = menu.addAction("Stop Timelapse Here");
    stopAct->setEnabled(m_pendingStartMs >= 0 && ms > m_pendingStartMs + 500);
    if (m_pendingStartMs >= 0) {
        startAct->setText(
            QString("Restart Timelapse (was %1)").arg(formatMs(m_pendingStartMs)));
    }

    // Mark operations
    menu.addSeparator();
    QAction* editAct     = nullptr;
    QAction* clearAct    = nullptr;
    if (mi >= 0) {
        editAct  = menu.addAction("Mark Settings\xe2\x80\xa6");
        clearAct = menu.addAction("Clear Mark");
    }
    auto* clearAllAct = menu.addAction("Clear All Marks");
    clearAllAct->setEnabled(!m_marks.isEmpty());

    QAction* chosen = menu.exec(e->globalPos());
    if (!chosen) return;

    if (chosen == frameAct) {
        emit frameViewRequested(ms);
    } else if (chosen == startAct) {
        m_pendingStartMs = ms;
        update();
        emit pendingStartChanged(ms);
    } else if (chosen == stopAct) {
        addMark(m_pendingStartMs, ms);
        m_pendingStartMs = -1;
        update();
        emit pendingStartChanged(-1);
    } else if (chosen == editAct  && mi >= 0) {
        emit markEditRequested(m_marks[mi].id);
    } else if (chosen == clearAct && mi >= 0) {
        emit markDeleteRequested(m_marks[mi].id);
    } else if (chosen == clearAllAct) {
        emit markClearAllRequested();
    }
}
// SN: 00106
