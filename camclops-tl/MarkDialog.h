// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include "TimelineWidget.h"

class QDoubleSpinBox;
class QLabel;

// Edit dialog for a single TLMark.  Allows changing start/end (seconds) and
// target duration.  Type and Duration labels update live as values change.
// Setting target to 0 turns the mark into a Cut; any other value auto-classifies
// as Slow-Mo, Timelapse, or Normal based on the comparison with source duration.
class MarkDialog : public QDialog {
    Q_OBJECT
public:
    explicit MarkDialog(const TLMark& mark,
                        qint64 videoDurationMs,
                        qint64 frameDurationMs,
                        QWidget* parent = nullptr);

    TLMark result() const;

signals:
    void deleteRequested(int id);

private:
    int             m_id;
    qint64          m_frameDurationMs;
    QDoubleSpinBox* m_startSpin  = nullptr;
    QDoubleSpinBox* m_endSpin    = nullptr;
    QDoubleSpinBox* m_targetSpin = nullptr;
    QLabel*         m_startLbl   = nullptr;
    QLabel*         m_endLbl     = nullptr;
    QLabel*         m_durLbl     = nullptr;
    QLabel*         m_typeLbl    = nullptr;

    QString fmtMs(qint64 ms) const;
    void    refreshDerived();
};
// SN: 00122
