// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "MarkDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QDialogButtonBox>

static QString fmtDuration(qint64 ms)
{
    int h  = (int)(ms / 3600000);
    int m  = (int)((ms % 3600000) / 60000);
    int s  = (int)((ms % 60000) / 1000);
    int fr = (int)((ms % 1000) / 100);
    if (h > 0)
        return QString("%1h %2m %3.%4s").arg(h).arg(m).arg(s).arg(fr);
    if (m > 0)
        return QString("%1m %2.%3s").arg(m).arg(s).arg(fr);
    return QString("%1.%2s").arg(s).arg(fr);
}

MarkDialog::MarkDialog(const TLMark& mark, QWidget* parent)
    : QDialog(parent), m_id(mark.id)
{
    setWindowTitle("Timelapse Mark Settings");

    auto* vlay = new QVBoxLayout(this);
    vlay->setSpacing(10);

    qint64 rawMs = mark.endMs - mark.startMs;
    auto* rawLbl = new QLabel(
        QString("Raw section:  <b>%1</b>").arg(fmtDuration(rawMs)), this);
    vlay->addWidget(rawLbl);

    double rawS  = rawMs / 1000.0;
    double maxS  = std::max(rawS * 20.0, 3600.0);

    auto* form = new QFormLayout;
    m_spin = new QDoubleSpinBox(this);
    m_spin->setRange(0.0, maxS);
    m_spin->setSingleStep(0.5);
    m_spin->setDecimals(1);
    m_spin->setSuffix(" s");
    double initVal = mark.targetSecs >= 0.0 ? mark.targetSecs
                                             : std::max(0.5, rawS / 10.0);
    m_spin->setValue(initVal);
    form->addRow("Duration:", m_spin);
    vlay->addLayout(form);

    auto* infoLbl = new QLabel(this);
    auto updateInfo = [=]() {
        double v = m_spin->value();
        if (v == 0.0) {
            infoLbl->setStyleSheet("color: #c0392b;");
            infoLbl->setText("Cut \xe2\x80\x94 this span will be removed from the output");
        } else if (v > rawS + 0.05) {
            infoLbl->setStyleSheet("color: #2980b9;");
            infoLbl->setText(QString("Slow-motion: %1\xc3\x97 slower")
                             .arg(v / rawS, 0, 'f', 1));
        } else if (v < rawS - 0.05) {
            infoLbl->setStyleSheet("color: #e67e22;");
            infoLbl->setText(QString("Speed-up: %1\xc3\x97 realtime")
                             .arg(rawS / v, 0, 'f', 1));
        } else {
            infoLbl->setStyleSheet("color: #27ae60;");
            infoLbl->setText("Real-time \xe2\x80\x94 no change");
        }
    };
    connect(m_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [=](double){ updateInfo(); });
    updateInfo();
    vlay->addWidget(infoLbl);

    auto* hlay = new QHBoxLayout;
    auto* delBtn = new QPushButton("Delete Mark", this);
    delBtn->setStyleSheet("color: #c0392b;");
    auto* okBtn  = new QPushButton("OK", this);
    okBtn->setDefault(true);
    hlay->addWidget(delBtn);
    hlay->addStretch();
    hlay->addWidget(okBtn);
    vlay->addLayout(hlay);

    connect(okBtn,  &QPushButton::clicked, this, &QDialog::accept);
    connect(delBtn, &QPushButton::clicked, this, [this]{
        emit deleteRequested(m_id);
        reject();
    });
}

double MarkDialog::targetSecs() const { return m_spin->value(); }
// SN: 00107
