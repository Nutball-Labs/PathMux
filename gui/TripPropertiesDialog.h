// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include "trip_detection.hpp"

class TripPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit TripPropertiesDialog(const Pathmux::Trip& trip,
                                  QWidget* parent = nullptr);
};
// SN: 00090
