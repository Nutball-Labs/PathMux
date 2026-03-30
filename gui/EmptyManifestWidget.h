// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QWidget>

// Shown in the right pane when no manifests exist yet.
// Camera icon + instructional text; clicking anywhere emits scanRequested().
class EmptyManifestWidget : public QWidget {
    Q_OBJECT
public:
    explicit EmptyManifestWidget(QWidget* parent = nullptr);

signals:
    void scanRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
};
// SN: 00090
