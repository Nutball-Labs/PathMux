// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "AboutDialog.h"
#include "version.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPixmap>
#include <QApplication>

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("About CamClops");
    setFixedWidth(420);

    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(20, 20, 20, 16);
    vbox->setSpacing(0);

    // --- Logo row: Nutball-Labs (left) | CamClops (right) ---
    {
        auto* hbox = new QHBoxLayout;
        hbox->setSpacing(16);
        hbox->addStretch();

        QPixmap nlLogo(":/images/Nutball-Labs_logo.png");
        if (!nlLogo.isNull()) {
            auto* nlLabel = new QLabel(this);
            nlLabel->setPixmap(nlLogo.scaledToHeight(80, Qt::SmoothTransformation));
            nlLabel->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
            hbox->addWidget(nlLabel);
        }

        QPixmap pmLogo(":/images/camclops_logo.png");
        if (!pmLogo.isNull()) {
            auto* pmLabel = new QLabel(this);
            pmLabel->setPixmap(pmLogo.scaledToHeight(80, Qt::SmoothTransformation));
            pmLabel->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
            hbox->addWidget(pmLabel);
        }

        hbox->addStretch();
        vbox->addLayout(hbox);
        vbox->addSpacing(12);
    }

    // --- App name (large, bold) ---
    auto* appLabel = new QLabel("CamClops", this);
    QFont appFont = appLabel->font();
    appFont.setPointSize(18);
    appFont.setBold(true);
    appLabel->setFont(appFont);
    appLabel->setAlignment(Qt::AlignHCenter);
    vbox->addWidget(appLabel);

    // --- Version ---
    auto* verLabel = new QLabel(
        QString("Version %1").arg(APP_VERSION),
        this);
    verLabel->setAlignment(Qt::AlignHCenter);
    vbox->addWidget(verLabel);

    vbox->addSpacing(10);

    // --- Tagline ---
    auto* tagLabel = new QLabel("Dashcam footage organizer &amp; GPS track extractor", this);
    tagLabel->setAlignment(Qt::AlignHCenter);
    tagLabel->setWordWrap(true);
    vbox->addWidget(tagLabel);

    vbox->addSpacing(8);

    // --- Copyright / license ---
    auto* cpLabel = new QLabel(
        "\u00a9 2026 Nutball Labs / Stephen Berg\n"
        "Released under the GNU General Public License v3",
        this);
    cpLabel->setAlignment(Qt::AlignHCenter);
    cpLabel->setWordWrap(true);
    QFont smallFont = cpLabel->font();
    smallFont.setPointSize(smallFont.pointSize() - 1);
    cpLabel->setFont(smallFont);
    cpLabel->setEnabled(false);   // subdued colour
    vbox->addWidget(cpLabel);

    vbox->addSpacing(16);

    // --- Close button ---
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    vbox->addWidget(buttons);
}
// SN: 00119
