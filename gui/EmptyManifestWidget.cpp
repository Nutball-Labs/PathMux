// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "EmptyManifestWidget.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QIcon>

EmptyManifestWidget::EmptyManifestWidget(QWidget* parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);

    auto* vlay = new QVBoxLayout(this);
    vlay->setAlignment(Qt::AlignCenter);
    vlay->setSpacing(12);

    // Camera icon — use system theme icon, fall back to large "?" text
    auto* iconLabel = new QLabel;
    iconLabel->setAlignment(Qt::AlignCenter);
    QIcon camIcon = QIcon::fromTheme("camera-photo",
                    QIcon::fromTheme("camera-video",
                    QIcon::fromTheme("camera")));
    if (!camIcon.isNull()) {
        QPixmap px = camIcon.pixmap(96, 96);
        // Composite a "?" over the bottom-right corner
        QPainter painter(&px);
        QFont f = painter.font();
        f.setPointSize(28);
        f.setBold(true);
        painter.setFont(f);
        painter.setPen(QColor(0x0078d4));
        painter.drawText(px.rect().adjusted(40, 40, -4, -4),
                         Qt::AlignBottom | Qt::AlignRight, "?");
        painter.end();
        iconLabel->setPixmap(px);
    } else {
        QFont f;
        f.setPointSize(56);
        iconLabel->setFont(f);
        iconLabel->setText("?");
    }

    auto* titleLabel = new QLabel("No manifests found");
    QFont tf = titleLabel->font();
    tf.setPointSize(14);
    tf.setBold(true);
    titleLabel->setFont(tf);
    titleLabel->setAlignment(Qt::AlignCenter);

    auto* subLabel = new QLabel("Click to scan a source directory");
    subLabel->setAlignment(Qt::AlignCenter);
    subLabel->setEnabled(false);

    vlay->addWidget(iconLabel);
    vlay->addWidget(titleLabel);
    vlay->addWidget(subLabel);
}

void EmptyManifestWidget::mousePressEvent(QMouseEvent*)
{
    emit scanRequested();
}

void EmptyManifestWidget::enterEvent(QEnterEvent*)
{
    setStyleSheet("EmptyManifestWidget { background: #f0f6ff; }");
}

void EmptyManifestWidget::leaveEvent(QEvent*)
{
    setStyleSheet("");
}
// SN: 00090
