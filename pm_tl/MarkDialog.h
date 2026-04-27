// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include "TimelineWidget.h"

class QDoubleSpinBox;
class QLabel;

class MarkDialog : public QDialog {
    Q_OBJECT
public:
    explicit MarkDialog(const TLMark& mark, QWidget* parent = nullptr);

    double targetSecs() const;

signals:
    void deleteRequested(int id);

private:
    int            m_id;
    QDoubleSpinBox* m_spin = nullptr;
};
// SN: 00106
