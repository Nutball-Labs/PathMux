// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QWidget>
#include <string>
#include "trip_detection.hpp"

class QLabel;
class QPushButton;

class TripTile : public QWidget {
    Q_OBJECT
public:
    explicit TripTile(const CamClops::Trip& trip, bool imperial,
                      const std::string& manifestFile,
                      const std::string& mid,
                      QWidget* parent = nullptr);

    const CamClops::Trip& trip() const { return m_trip; }
    void setThumbnail(const QString& slot, const QPixmap& pixmap);
    void setZoom(double factor);
    void refreshFrom(const CamClops::Trip& trip);

    // Layout constants — shared with TripGridPanel
    static constexpr int W         = 360;
    static constexpr int BADGE_H   =  22;   // full-width ID badge at top
    static constexpr int H         = 222;   // 200 + BADGE_H
    static constexpr int PADDING   =   8;
    static constexpr int THUMB_W   = 160;
    static constexpr int THUMB_H   =  90;
    static constexpr int THUMB_GAP =   4;
    static constexpr int LEFT_W    = PADDING + THUMB_W + PADDING; // 176
    static constexpr int DIV_X     = LEFT_W;                      // 176
    static constexpr int RIGHT_X   = DIV_X + PADDING;             // 184
    static constexpr int RIGHT_W   = W - RIGHT_X - PADDING;       // 168

    // Layout constants for right-column elements (all absolute Y positions)
    static constexpr int BUILD_BTN_Y    = 100;  // Build Video button
    static constexpr int BUILD_BTN_H    =  28;
    static constexpr int NOTE_Y         =  72;  // relative to (BADGE_H + PADDING)
    static constexpr int NOTE_H         =  16;
    static constexpr int DETAIL_LABEL_Y = 160;  // segs · distance label
    static constexpr int EXTRAS_BTN_Y   = 186;  // Extras… button

signals:
    void doubleClicked();
    void buildRequested();
    void tripChanged();   // emitted after whole-trip or segment delete/archive

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onExtrasClicked();

private:
    void archiveWholeTrip();
    void deleteWholeTrip();
    std::string archiveDestPath(const std::string& absPath) const;
    void drawStatusIndicators(QPainter& p) const;
    void updateTitleBar();

    CamClops::Trip m_trip;
    std::string   m_sourcePath;
    std::string   m_mid;
    double        m_zoomFactor = 1.0;

    QLabel*       m_frontThumb;
    QLabel*       m_rearThumb;
    QLabel*       m_idLabel;
    QLabel*       m_startLabel;
    QLabel*       m_durationLabel;
    QLabel*       m_noteLabel;
    QLabel*       m_detailLabel;
    QPushButton*  m_buildBtn;
    QPushButton*  m_extrasBtn = nullptr;
};
// SN: 00113
