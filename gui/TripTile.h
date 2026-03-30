// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QWidget>
#include "trip_detection.hpp"

class QLabel;

class TripTile : public QWidget {
    Q_OBJECT
public:
    explicit TripTile(const Pathmux::Trip& trip, bool imperial,
                      QWidget* parent = nullptr);

    const Pathmux::Trip& trip() const { return m_trip; }
    void setThumbnail(const QString& slot, const QPixmap& pixmap);
    void setTextZoom(double factor);

    // Layout constants — shared with TripGridPanel
    static constexpr int W         = 360;
    static constexpr int H         = 200;
    static constexpr int PADDING   =   8;
    static constexpr int THUMB_W   = 160;
    static constexpr int THUMB_H   =  90;
    static constexpr int THUMB_GAP =   4;
    static constexpr int LEFT_W    = PADDING + THUMB_W + PADDING; // 176
    static constexpr int DIV_X     = LEFT_W;                      // 176
    static constexpr int RIGHT_X   = DIV_X + PADDING;             // 184
    static constexpr int RIGHT_W   = W - RIGHT_X - PADDING;       // 168

signals:
    void doubleClicked();

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    Pathmux::Trip m_trip;
    QLabel*       m_frontThumb;
    QLabel*       m_rearThumb;
    QLabel*       m_startLabel;
    QLabel*       m_durationLabel;
    QLabel*       m_detailLabel;
};
// SN: 00090
