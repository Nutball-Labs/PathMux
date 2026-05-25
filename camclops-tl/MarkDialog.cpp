// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "MarkDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <algorithm>
#include <cmath>

MarkDialog::MarkDialog(const TLMark& mark,
                       qint64 videoDurationMs,
                       qint64 frameDurationMs,
                       QWidget* parent)
    : QDialog(parent), m_id(mark.id), m_frameDurationMs(frameDurationMs)
{
    setWindowTitle("Mark Settings");
    setMinimumWidth(400);

    double videoS  = videoDurationMs / 1000.0;
    double startS  = mark.startMs / 1000.0;
    double endS    = mark.endMs   / 1000.0;
    double rawS    = endS - startS;
    double frameS  = frameDurationMs > 0 ? frameDurationMs / 1000.0 : 1.0 / 30.0;

    auto* vlay = new QVBoxLayout(this);
    vlay->setSpacing(10);
    vlay->setContentsMargins(14, 14, 14, 14);

    auto* form = new QFormLayout;
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);

    // ── Start ──────────────────────────────────────────────────────────────
    auto* startRow = new QHBoxLayout;
    m_startSpin = new QDoubleSpinBox(this);
    m_startSpin->setRange(0.0, endS - frameS);
    m_startSpin->setSingleStep(frameS);
    m_startSpin->setDecimals(3);
    m_startSpin->setSuffix(" s");
    m_startSpin->setValue(startS);
    m_startLbl = new QLabel(this);
    m_startLbl->setMinimumWidth(90);
    m_startLbl->setStyleSheet("color: gray;");
    startRow->addWidget(m_startSpin);
    startRow->addWidget(m_startLbl);
    startRow->addStretch();
    form->addRow("Start:", startRow);

    // ── End ────────────────────────────────────────────────────────────────
    auto* endRow = new QHBoxLayout;
    m_endSpin = new QDoubleSpinBox(this);
    m_endSpin->setRange(startS + frameS, videoS);
    m_endSpin->setSingleStep(frameS);
    m_endSpin->setDecimals(3);
    m_endSpin->setSuffix(" s");
    m_endSpin->setValue(endS);
    m_endLbl = new QLabel(this);
    m_endLbl->setMinimumWidth(90);
    m_endLbl->setStyleSheet("color: gray;");
    endRow->addWidget(m_endSpin);
    endRow->addWidget(m_endLbl);
    endRow->addStretch();
    form->addRow("End:", endRow);

    // ── Duration (read-only) ───────────────────────────────────────────────
    m_durLbl = new QLabel(this);
    m_durLbl->setStyleSheet("color: gray;");
    form->addRow("Duration:", m_durLbl);

    // ── Separator ─────────────────────────────────────────────────────────
    auto* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFrameShadow(QFrame::Sunken);
    form->addRow(sep1);

    // ── Target ────────────────────────────────────────────────────────────
    double maxTarget = std::max(rawS * 20.0, 3600.0);
    m_targetSpin = new QDoubleSpinBox(this);
    m_targetSpin->setRange(0.0, maxTarget);
    m_targetSpin->setSingleStep(0.5);
    m_targetSpin->setDecimals(1);
    m_targetSpin->setSuffix(" s  (0 = cut)");
    double initTarget = mark.targetSecs >= 0.0 ? mark.targetSecs
                                               : std::max(0.5, rawS / 10.0);
    m_targetSpin->setValue(initTarget);
    form->addRow("Target:", m_targetSpin);

    // ── Type (read-only, color-coded) ─────────────────────────────────────
    m_typeLbl = new QLabel(this);
    m_typeLbl->setWordWrap(true);
    form->addRow("Type:", m_typeLbl);

    vlay->addLayout(form);

    // ── Buttons ────────────────────────────────────────────────────────────
    auto* hlay   = new QHBoxLayout;
    auto* delBtn = new QPushButton("Delete Mark", this);
    delBtn->setStyleSheet("color: #c0392b;");
    auto* cancelBtn = new QPushButton("Cancel", this);
    auto* okBtn     = new QPushButton("OK", this);
    okBtn->setDefault(true);
    hlay->addWidget(delBtn);
    hlay->addStretch();
    hlay->addWidget(cancelBtn);
    hlay->addWidget(okBtn);
    vlay->addLayout(hlay);

    // ── Connections ────────────────────────────────────────────────────────
    // Cross-constraint: keep start < end (use QSignalBlocker to prevent loops)
    connect(m_startSpin,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, frameS](double v) {
        QSignalBlocker sb(m_endSpin);
        m_endSpin->setMinimum(v + frameS);
        if (m_endSpin->value() < v + frameS)
            m_endSpin->setValue(v + frameS);
    });
    connect(m_endSpin,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, frameS](double v) {
        QSignalBlocker sb(m_startSpin);
        m_startSpin->setMaximum(v - frameS);
        if (m_startSpin->value() > v - frameS)
            m_startSpin->setValue(v - frameS);
    });

    // Live refresh of derived labels after range constraints settle
    auto refresh = [this] { refreshDerived(); };
    connect(m_startSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, refresh);
    connect(m_endSpin,    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, refresh);
    connect(m_targetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, refresh);

    connect(okBtn,     &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(delBtn,    &QPushButton::clicked, this, [this] {
        emit deleteRequested(m_id);
        reject();
    });

    refreshDerived();
}

// ---------------------------------------------------------------------------
// Live-update derived labels
// ---------------------------------------------------------------------------
void MarkDialog::refreshDerived()
{
    double startS  = m_startSpin->value();
    double endS    = m_endSpin->value();
    double srcS    = endS - startS;
    double targetS = m_targetSpin->value();

    m_startLbl->setText(fmtMs((qint64)(startS * 1000.0)));
    m_endLbl->setText(  fmtMs((qint64)(endS   * 1000.0)));
    m_durLbl->setText(  fmtMs((qint64)(srcS   * 1000.0)));

    // Update target maximum to track source duration changes
    {
        QSignalBlocker sb(m_targetSpin);
        m_targetSpin->setMaximum(std::max(srcS * 20.0, 3600.0));
    }

    // Type label — color and text follow target vs source duration
    if (targetS == 0.0) {
        m_typeLbl->setText("Cut \xe2\x80\x94 this section will be removed");
        m_typeLbl->setStyleSheet("color: #c0392b; font-weight: bold;");
    } else if (targetS > srcS + 0.05) {
        double ratio = srcS > 0 ? targetS / srcS : 1.0;
        m_typeLbl->setText(QString("Slow-Mo \xe2\x80\x94 %1\xc3\x97 slower")
                           .arg(ratio, 0, 'f', 1));
        m_typeLbl->setStyleSheet("color: #2980b9; font-weight: bold;");
    } else if (targetS < srcS - 0.05) {
        double ratio = targetS > 0 ? srcS / targetS : 0.0;
        m_typeLbl->setText(QString("Timelapse \xe2\x80\x94 %1\xc3\x97 realtime")
                           .arg(ratio, 0, 'f', 1));
        m_typeLbl->setStyleSheet("color: #e67e22; font-weight: bold;");
    } else {
        m_typeLbl->setText("Normal \xe2\x80\x94 real-time playback");
        m_typeLbl->setStyleSheet("color: #27ae60; font-weight: bold;");
    }
}

// ---------------------------------------------------------------------------
// Result and helpers
// ---------------------------------------------------------------------------
TLMark MarkDialog::result() const
{
    TLMark mk;
    mk.id         = m_id;
    mk.startMs    = (qint64)(m_startSpin->value() * 1000.0);
    mk.endMs      = (qint64)(m_endSpin->value()   * 1000.0);
    mk.targetSecs = m_targetSpin->value();
    return mk;
}

QString MarkDialog::fmtMs(qint64 ms) const
{
    if (ms < 0) ms = 0;
    int h = (int)(ms / 3600000);
    int m = (int)((ms % 3600000) / 60000);
    int s = (int)((ms % 60000)   / 1000);
    int f = m_frameDurationMs > 0 ? (int)((ms % 1000) / m_frameDurationMs) : 0;
    if (h > 0)
        return QString("%1:%2:%3.%4")
               .arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0')).arg(f,2,10,QChar('0'));
    return QString("%1:%2.%3").arg(m).arg(s,2,10,QChar('0')).arg(f,2,10,QChar('0'));
}
// SN: 00122
