// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "LogoMorphWidget.h"
#include <QPainter>
#include <QTimer>
#include <QShowEvent>
#include <QHideEvent>
#include <QResizeEvent>
#include <cmath>

LogoMorphWidget::LogoMorphWidget(QWidget* parent)
    : QWidget(parent)
{
    m_pmxOrig = QPixmap(":/images/pathmux_256.png");
    m_ntbOrig = QPixmap(":/images/Nutball-Labs_logo.png");

    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_timer = new QTimer(this);
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &LogoMorphWidget::onTick);
}

void LogoMorphWidget::rebuildCache()
{
    QSize sz = size();
    if (sz == m_cachedSize || sz.isEmpty()) return;
    m_cachedSize = sz;
    auto fit = [&](const QPixmap& src) -> QPixmap {
        if (src.isNull()) return {};
        return src.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    };
    m_pmxCached = fit(m_pmxOrig);
    m_ntbCached = fit(m_ntbOrig);
}

void LogoMorphWidget::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    rebuildCache();
}

void LogoMorphWidget::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    rebuildCache();
    m_clock.restart();
    m_timer->start();
}

void LogoMorphWidget::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    m_timer->stop();
}

void LogoMorphWidget::onTick()
{
    update();
}

void LogoMorphWidget::paintEvent(QPaintEvent*)
{
    qint64 pos   = m_clock.elapsed() % kCycleMs;
    double half  = kCycleMs / 2.0;

    // phase: 0→1 over first half of cycle, 1→0 over second half
    double phase = (pos < static_cast<qint64>(half))
                   ? static_cast<double>(pos) / half
                   : static_cast<double>(kCycleMs - pos) / half;

    // Sine ease-in-out keeps both logos fully visible at the endpoints
    // and smoothly accelerates/decelerates through the cross-fade.
    double t = 0.5 * (1.0 - std::cos(M_PI * phase));

    QPainter p(this);
    p.setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);

    // Dark background matching the collage tile colour
    p.fillRect(rect(), QColor(0x1e, 0x1e, 0x2e));

    auto drawCentered = [&](const QPixmap& pm, double opacity) {
        if (pm.isNull() || opacity <= 0.0) return;
        p.setOpacity(opacity);
        QRect dst(QPoint(0, 0), pm.size());
        dst.moveCenter(rect().center());
        p.drawPixmap(dst, pm);
    };

    drawCentered(m_pmxCached, 1.0 - t);  // PathMux fades out
    drawCentered(m_ntbCached, t);         // Nutball-Labs fades in

    p.setOpacity(1.0);
}
// SN: 00101
