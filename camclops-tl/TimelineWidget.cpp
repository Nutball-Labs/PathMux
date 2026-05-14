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
    setFixedHeight(rulerH() + trackH() + 8);
}

void TimelineWidget::setUiScale(double s)
{
    m_uiScale = std::max(0.5, s);
    setFixedHeight(rulerH() + trackH() + 8);
    update();
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
    mk.targetSecs = -1;
    m_marks.append(mk);
    std::sort(m_marks.begin(), m_marks.end(),
        [](const TLMark& a, const TLMark& b){ return a.startMs < b.startMs; });
    update();
    emit marksChanged();
}

void TimelineWidget::addMarkFull(qint64 startMs, qint64 endMs, double targetSecs)
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
    mk.targetSecs = targetSecs;
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
    if (std::abs(px - x0) <= edgeSlop()) { isStart = true;  return true; }
    if (std::abs(px - x1) <= edgeSlop()) { isStart = false; return true; }
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
    const int ty = rulerH();

    p.fillRect(rect(), QColor(0x2a, 0x2a, 0x2a));

    if (m_duration <= 0) {
        p.setPen(QColor(0x88, 0x88, 0x88));
        p.drawText(rect(), Qt::AlignCenter, "Open an MP4 file to begin");
        return;
    }

    // Ruler
    p.fillRect(0, ry, W, rulerH(), QColor(0x1a, 0x1a, 0x1a));
    QVector<qint64> major, minor;
    buildRulerTicks(major, minor);
    p.setPen(QColor(0x66, 0x66, 0x66));
    for (qint64 ms : minor) {
        int x = msToPx(ms);
        p.drawLine(x, ry + rulerH() - 4, x, ry + rulerH());
    }
    p.setPen(QColor(0xaa, 0xaa, 0xaa));
    QFont f = font(); f.setPointSizeF(7.0 * m_uiScale); p.setFont(f);
    for (qint64 ms : major) {
        int x = msToPx(ms);
        p.drawLine(x, ry, x, ry + rulerH());
        p.drawText(QRect(x + 2, ry, 60, rulerH() - 2),
                   Qt::AlignVCenter | Qt::AlignLeft, formatMs(ms));
    }

    // Track background
    p.fillRect(0, ty, W, trackH(), QColor(0x3a, 0x3a, 0x3a));

    // Pending start indicator — green shaded region from pending to end of track
    if (m_pendingStartMs >= 0) {
        int px = msToPx(m_pendingStartMs);
        p.fillRect(px, ty, W - px, trackH(), QColor(0x27, 0xae, 0x60, 50));
        p.setPen(QPen(QColor(0x27, 0xae, 0x60), 2, Qt::DashLine));
        p.drawLine(px, ty, px, ty + trackH());
        // Label
        p.setPen(QColor(0x27, 0xae, 0x60));
        f.setPointSizeF(7.0 * m_uiScale); p.setFont(f);
        p.drawText(QRect(px + 4, ty, 120, trackH()),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QString("Start: %1").arg(formatMs(m_pendingStartMs)));
    }

    // Marks — color by span type
    for (const auto& m : m_marks) {
        int x0 = msToPx(m.startMs);
        int x1 = msToPx(m.endMs);
        double srcS = (m.endMs - m.startMs) / 1000.0;
        QColor fill;
        QString lbl;
        if (m.targetSecs < 0) {
            fill = QColor(0x88, 0x88, 0x88, 180);   // gray — unconfigured
            lbl  = "TL (unconfigured)";
        } else if (m.targetSecs == 0.0) {
            fill = QColor(0xc0, 0x39, 0x2b, 200);   // red — cut
            lbl  = "Cut";
        } else if (m.targetSecs > srcS + 0.05) {
            fill = QColor(0x27, 0x6a, 0xae, 200);   // blue — slow-motion
            lbl  = QString("\xe2\x86\x92%1s (%2\xc3\x97 slow)")
                   .arg(m.targetSecs, 0, 'f', 1)
                   .arg(m.targetSecs / srcS, 0, 'f', 1);
        } else if (m.targetSecs < srcS - 0.05) {
            fill = QColor(0xe6, 0x7e, 0x22, 200);   // orange — compressed
            lbl  = QString("\xe2\x86\x92%1s (%2\xc3\x97)")
                   .arg(m.targetSecs, 0, 'f', 1)
                   .arg(srcS / m.targetSecs, 0, 'f', 1);
        } else {
            fill = QColor(0x27, 0xae, 0x60, 200);   // green — real-time
            lbl  = QString("\xe2\x86\x92%1s (real-time)").arg(m.targetSecs, 0, 'f', 1);
        }
        p.fillRect(x0, ty, x1 - x0, trackH(), fill);
        p.fillRect(x0,     ty, 3, trackH(), fill.lighter(150));
        p.fillRect(x1 - 3, ty, 3, trackH(), fill.lighter(150));
        if (x1 - x0 > 30) {
            p.setPen(Qt::white);
            f.setPointSizeF(7.0 * m_uiScale); p.setFont(f);
            p.drawText(QRect(x0 + 4, ty, x1 - x0 - 8, trackH()),
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
// Wheel: plain scroll pans the time axis when zoomed in.
// Ctrl+scroll is NOT handled here — it propagates up to MainWindow which
// applies UI-wide scaling (same as camclops-gui's tile zoom behaviour).
// ---------------------------------------------------------------------------
void TimelineWidget::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        // Let the event bubble to the parent for UI scale handling.
        e->ignore();
        return;
    }

    if (m_duration <= 0 || m_zoom <= 1.0) return;
    int delta = e->angleDelta().y();
    if (delta == 0) return;

    qint64 step = visibleMs() / 10;
    m_viewStartMs += (delta < 0) ? step : -step;
    clampView();
    update();
    emitViewChanged();
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

    // Mark operations
    QAction* editAct  = nullptr;
    QAction* clearAct = nullptr;
    if (mi >= 0) {
        menu.addSeparator();
        editAct  = menu.addAction("Mark Settings\xe2\x80\xa6");
        clearAct = menu.addAction("Clear Mark");
    }
    auto* clearAllAct = menu.addAction("Clear All Marks");
    clearAllAct->setEnabled(!m_marks.isEmpty());

    QAction* chosen = menu.exec(e->globalPos());
    if (!chosen) return;

    if (chosen == frameAct) {
        emit frameViewRequested(ms);
    } else if (chosen == editAct  && mi >= 0) {
        emit markEditRequested(m_marks[mi].id);
    } else if (chosen == clearAct && mi >= 0) {
        emit markDeleteRequested(m_marks[mi].id);
    } else if (chosen == clearAllAct) {
        emit markClearAllRequested();
    }
}
// SN: 00109
