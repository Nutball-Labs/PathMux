// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QWidget>
#include <QPixmap>
#include <QElapsedTimer>
class QTimer;

// Cross-fades between the CamClops and Nutball-Labs logos on a 6-second sine
// ease-in-out cycle.  Timer only runs while the widget is visible.
class LogoMorphWidget : public QWidget {
    Q_OBJECT
public:
    explicit LogoMorphWidget(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {200, 200}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*)   override;
    void hideEvent(QHideEvent*)   override;
    void resizeEvent(QResizeEvent*) override;

private slots:
    void onTick();

private:
    void rebuildCache();

    QPixmap       m_pmxOrig;     // CamClops logo — original resolution
    QPixmap       m_ntbOrig;     // Nutball-Labs logo — original resolution
    QPixmap       m_pmxCached;   // scaled to current widget size
    QPixmap       m_ntbCached;
    QSize         m_cachedSize;
    QTimer*       m_timer = nullptr;
    QElapsedTimer m_clock;

    static constexpr int kCycleMs = 6000;  // full morph cycle (ms)
    static constexpr int kTickMs  = 33;    // ~30 fps
};
// SN: 00101
