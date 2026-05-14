// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "JobQueuePanel.h"
#include "JobQueue.h"
#include "config_manager.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QFont>
#include <QMap>
#include <QPainter>
#include <QWheelEvent>

static QString fmtMs(qint64 ms)
{
    int s = (int)(ms / 1000);
    return s < 60 ? QString("%1s").arg(s)
                  : QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// ─── JobRowWidget ─────────────────────────────────────────────────────────────
// Header row: [dot][desc][curStep/status][expandBtn][bar][elapsed][×]
// Expandable steps area below: one row per step with individual progress bar.
class JobRowWidget : public QWidget {
    Q_OBJECT

    struct StepEntry {
        QWidget*      widget    = nullptr;
        QLabel*       dot       = nullptr;
        QLabel*       nameLbl   = nullptr;
        QProgressBar* bar       = nullptr;
        QLabel*       statusLbl = nullptr;
        QElapsedTimer elapsed;
        QTimer*       tick      = nullptr;
        bool          running   = false;
        bool          finished  = false;
        bool          ok        = true;
    };

public:
    explicit JobRowWidget(Job* job, QWidget* parent = nullptr)
        : QWidget(parent), m_job(job)
    {
        setMinimumHeight(36);
        auto* vlay = new QVBoxLayout(this);
        vlay->setContentsMargins(0, 0, 0, 0);
        vlay->setSpacing(0);

        // ── Header row ────────────────────────────────────────────────────────
        m_mainRow = new QWidget(this);
        m_mainRow->setFixedHeight(36);
        auto* hlay = new QHBoxLayout(m_mainRow);
        hlay->setContentsMargins(8, 4, 8, 4);
        hlay->setSpacing(6);

        m_dot = new QLabel(m_mainRow);
        m_dot->setFixedSize(10, 10);
        m_dot->setStyleSheet(dotStyle(job->state()));
        hlay->addWidget(m_dot, 0, Qt::AlignVCenter);

        m_desc = new QLabel(job->description(), m_mainRow);
        m_desc->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        m_desc->setStyleSheet("color: #222;");
        hlay->addWidget(m_desc, 0, Qt::AlignVCenter);

        // Current step name (collapsed multi-step) or status text (single-step).
        m_curStepLbl = new QLabel(job->statusText(), m_mainRow);
        m_curStepLbl->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        m_curStepLbl->setMinimumWidth(60);
        m_curStepLbl->setMaximumWidth(220);
        {
            QFont f = m_curStepLbl->font();
            f.setPointSizeF(qMax(7.0, f.pointSizeF() - 1.0));
            m_curStepLbl->setFont(f);
        }
        m_curStepLbl->setStyleSheet("color: #555;");
        m_curStepLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_curStepLbl->setToolTip(job->statusText());
        hlay->addWidget(m_curStepLbl, 0, Qt::AlignVCenter);

        // Expand/collapse toggle — only shown once the job emits its first step.
        m_expandBtn = new QPushButton("▶", m_mainRow);
        m_expandBtn->setFixedSize(18, 18);
        m_expandBtn->setVisible(false);
        m_expandBtn->setStyleSheet(
            "QPushButton { border: none; color: #888; font-size: 7pt; background: transparent; }"
            "QPushButton:hover { color: #333; }");
        connect(m_expandBtn, &QPushButton::clicked, this, [this]() {
            m_expanded = !m_expanded;
            m_stepsWidget->setVisible(m_expanded);
            m_expandBtn->setText(m_expanded ? "▼" : "▶");
            m_curStepLbl->setVisible(!m_expanded);
        });
        hlay->addWidget(m_expandBtn, 0, Qt::AlignVCenter);

        // Progress bar — takes all remaining space; the prominent element on the row.
        m_bar = new QProgressBar(m_mainRow);
        m_bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_bar->setFixedHeight(14);
        m_bar->setMinimumWidth(60);
        m_bar->setRange(0, 100);
        m_bar->setValue(0);
        m_bar->setTextVisible(false);
        hlay->addWidget(m_bar, 1, Qt::AlignVCenter);

        // Accumulated job elapsed time — shown while running and frozen on finish.
        m_elapsedLbl = new QLabel(m_mainRow);
        m_elapsedLbl->setFixedWidth(44);
        {
            QFont f = m_elapsedLbl->font();
            f.setPointSizeF(qMax(7.0, f.pointSizeF() - 1.0));
            m_elapsedLbl->setFont(f);
        }
        m_elapsedLbl->setStyleSheet("color: #777;");
        m_elapsedLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hlay->addWidget(m_elapsedLbl, 0, Qt::AlignVCenter);

        m_cancelBtn = new QPushButton("×", m_mainRow);
        m_cancelBtn->setFixedSize(22, 22);
        m_cancelBtn->setStyleSheet(
            "QPushButton { border: none; color: #666; font-size: 12pt; background: transparent; }"
            "QPushButton:hover { color: #dc3545; }");
        connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
            if (m_job->state() == Job::State::Running)
                m_job->cancel();
            else
                emit dismissRequested(this);
        });
        hlay->addWidget(m_cancelBtn, 0, Qt::AlignVCenter);
        vlay->addWidget(m_mainRow);

        // ── Steps area ────────────────────────────────────────────────────────
        m_stepsWidget = new QWidget(this);
        m_stepsLay    = new QVBoxLayout(m_stepsWidget);
        m_stepsLay->setContentsMargins(24, 2, 8, 4);
        m_stepsLay->setSpacing(2);
        m_stepsWidget->setVisible(false);
        vlay->addWidget(m_stepsWidget);

        // ── Job-level elapsed timer ───────────────────────────────────────────
        m_tickTimer = new QTimer(this);
        m_tickTimer->setInterval(1000);
        connect(m_tickTimer, &QTimer::timeout, this, [this]() {
            if (m_jobElapsed.isValid())
                m_elapsedLbl->setText(fmtMs(m_jobElapsed.elapsed()));
        });

        // ── Wire job signals ──────────────────────────────────────────────────
        connect(job, &Job::progressChanged, this, [this](int pct) {
            ensureRunningStarted();
            m_dot->setStyleSheet(dotStyle(m_job->state()));
            if (pct < 0) m_bar->setRange(0, 0);
            else { m_bar->setRange(0, 100); m_bar->setValue(pct); }
        });
        connect(job, &Job::statusChanged, this, [this](const QString& msg) {
            m_dot->setStyleSheet(dotStyle(m_job->state()));
            if (m_stepEntries.isEmpty()) {
                m_curStepLbl->setText(msg);
                m_curStepLbl->setToolTip(msg);  // full text on hover when window is narrow
            }
        });
        connect(job, &Job::finished, this, [this](bool ok) {
            m_tickTimer->stop();
            if (m_jobElapsed.isValid())
                m_elapsedLbl->setText(fmtMs(m_jobElapsed.elapsed()));
            m_dot->setStyleSheet(dotStyle(m_job->state()));
            m_bar->setRange(0, 100);
            if (m_job->state() == Job::State::Done)
                m_bar->setValue(100);
            if (!ok)
                m_curStepLbl->setStyleSheet("color: #dc3545;");
        });
        connect(job, &Job::stepStarted,  this, &JobRowWidget::onStepStarted);
        connect(job, &Job::stepProgress, this, &JobRowWidget::onStepProgress);
        connect(job, &Job::stepDone,     this, &JobRowWidget::onStepDone);
    }

    Job* job() const { return m_job; }

    void setZoom(double factor) {
        int rowH = qRound(36 * factor);
        m_mainRow->setFixedHeight(rowH);
        setMinimumHeight(rowH);

        m_dot->setFixedSize(qRound(10 * factor), qRound(10 * factor));
        m_dot->setStyleSheet(dotStyle(m_job->state()));
        m_bar->setFixedHeight(qRound(14 * factor));
        m_bar->setMinimumWidth(qRound(60 * factor));
        m_cancelBtn->setFixedSize(qRound(22 * factor), qRound(22 * factor));
        m_expandBtn->setFixedSize(qRound(18 * factor), qRound(18 * factor));

        QFont baseFont = QApplication::font();
        baseFont.setPointSizeF(baseFont.pointSizeF() * factor);
        m_desc->setFont(baseFont);

        QFont smFont = baseFont;
        smFont.setPointSizeF(qMax(7.0 * factor, baseFont.pointSizeF() - 1.0));
        m_curStepLbl->setFont(smFont);
        m_curStepLbl->setMinimumWidth(qRound(60 * factor));
        m_curStepLbl->setMaximumWidth(qRound(220 * factor));
        m_elapsedLbl->setFont(smFont);
        m_elapsedLbl->setFixedWidth(qRound(44 * factor));

        for (auto& e : m_stepEntries) {
            if (!e.widget) continue;
            e.widget->setFixedHeight(qRound(28 * factor));
            e.dot->setFixedSize(qRound(8 * factor), qRound(8 * factor));
            e.bar->setMinimumWidth(qRound(40 * factor));
            e.bar->setFixedHeight(qRound(10 * factor));
            e.nameLbl->setFont(smFont);
            e.statusLbl->setFont(smFont);
            e.statusLbl->setFixedWidth(qRound(90 * factor));
        }
    }

signals:
    void dismissRequested(JobRowWidget* self);

private slots:
    void onStepStarted(const QString& stepName) {
        ensureRunningStarted();

        if (m_stepEntries.isEmpty()) {
            m_expandBtn->setVisible(true);
            // Default collapsed — header shows current step name.
        }

        auto* rowW = new QWidget(m_stepsWidget);
        rowW->setFixedHeight(28);
        auto* rlay = new QHBoxLayout(rowW);
        rlay->setContentsMargins(0, 2, 0, 2);
        rlay->setSpacing(6);

        auto* dot = new QLabel(rowW);
        dot->setFixedSize(8, 8);
        dot->setStyleSheet(stepDot(true, false, false));
        rlay->addWidget(dot, 0, Qt::AlignVCenter);

        auto* nameLbl = new QLabel(stepName, rowW);
        nameLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        {
            QFont f = nameLbl->font();
            f.setPointSizeF(qMax(7.0, f.pointSizeF() - 1.0));
            nameLbl->setFont(f);
        }
        nameLbl->setStyleSheet("color: #333;");
        rlay->addWidget(nameLbl, 1, Qt::AlignVCenter);

        auto* bar = new QProgressBar(rowW);
        bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        bar->setFixedHeight(10);
        bar->setMinimumWidth(40);
        bar->setRange(0, 0);   // indeterminate until first progress update
        bar->setTextVisible(false);
        bar->setStyleSheet(stepBarStyle("#fd7e14"));
        rlay->addWidget(bar, 1, Qt::AlignVCenter);

        auto* statusLbl = new QLabel(rowW);
        statusLbl->setFixedWidth(90);
        {
            QFont f = statusLbl->font();
            f.setPointSizeF(qMax(6.5, f.pointSizeF() - 1.5));
            statusLbl->setFont(f);
        }
        statusLbl->setStyleSheet("color: #fd7e14;");
        statusLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rlay->addWidget(statusLbl, 0, Qt::AlignVCenter);
        m_stepsLay->addWidget(rowW);

        // Populate entry (append first, then start elapsed on the vector element)
        StepEntry e;
        e.widget    = rowW;
        e.dot       = dot;
        e.nameLbl   = nameLbl;
        e.bar       = bar;
        e.statusLbl = statusLbl;
        e.running   = true;
        m_stepEntries.append(e);
        int idx = m_stepEntries.size() - 1;
        m_stepNameToIdx[stepName] = idx;
        m_stepEntries[idx].elapsed.start();

        auto* tick = new QTimer(this);
        tick->setInterval(1000);
        m_stepEntries[idx].tick = tick;
        connect(tick, &QTimer::timeout, this, [this, idx]() {
            if (idx < m_stepEntries.size() && m_stepEntries[idx].running)
                m_stepEntries[idx].statusLbl->setText(
                    "+" + fmtMs(m_stepEntries[idx].elapsed.elapsed()));
        });
        tick->start();

        // Update collapsed header to show which step is active
        m_curStepLbl->setText(stepName);
        m_curStepLbl->setToolTip(stepName);
    }

    void onStepProgress(const QString& stepName, int pct) {
        int idx = m_stepNameToIdx.value(stepName, -1);
        if (idx < 0 || idx >= m_stepEntries.size()) return;
        auto& e = m_stepEntries[idx];
        if (pct < 0) {
            e.bar->setRange(0, 0);
        } else {
            e.bar->setRange(0, 100);
            e.bar->setValue(pct);
            e.statusLbl->setText(
                QString("%1%  +%2").arg(pct).arg(fmtMs(e.elapsed.elapsed())));
        }
    }

    void onStepDone(const QString& stepName, bool ok) {
        int idx = m_stepNameToIdx.value(stepName, -1);
        if (idx < 0 || idx >= m_stepEntries.size()) return;
        auto& e = m_stepEntries[idx];
        e.running  = false;
        e.finished = true;
        e.ok       = ok;
        if (e.tick) e.tick->stop();

        qint64 ms = e.elapsed.elapsed();
        e.dot->setStyleSheet(stepDot(false, true, ok));
        if (ok) {
            e.bar->setRange(0, 100);
            e.bar->setValue(100);
            e.bar->setStyleSheet(stepBarStyle("#28a745"));
            e.statusLbl->setText("Done  " + fmtMs(ms));
            e.statusLbl->setStyleSheet("color: #28a745;");
        } else {
            e.bar->setStyleSheet(stepBarStyle("#dc3545"));
            e.statusLbl->setText("Failed");
            e.statusLbl->setStyleSheet("color: #dc3545;");
        }
    }

private:
    void ensureRunningStarted() {
        if (!m_jobElapsed.isValid() && m_job->state() == Job::State::Running) {
            m_jobElapsed.start();
            m_tickTimer->start();
        }
    }

    static QString dotStyle(Job::State s) {
        QString c;
        switch (s) {
            case Job::State::Queued:    c = "#6c757d"; break;
            case Job::State::Running:   c = "#fd7e14"; break;
            case Job::State::Done:      c = "#28a745"; break;
            case Job::State::Failed:    c = "#dc3545"; break;
            case Job::State::Cancelled: c = "#6c757d"; break;
        }
        return QString("background: %1; border-radius: 50%;").arg(c);
    }
    static QString stepDot(bool running, bool done, bool ok) {
        QString c = running ? "#fd7e14" : done ? (ok ? "#28a745" : "#dc3545") : "#9aa0a8";
        return QString("background: %1; border-radius: 50%;").arg(c);
    }
    static QString stepBarStyle(const QString& color) {
        return QString(
            "QProgressBar { background: #d8dce2; border: none; border-radius: 3px; }"
            "QProgressBar::chunk { background: %1; border-radius: 3px; }").arg(color);
    }

    Job*          m_job;
    QWidget*      m_mainRow    = nullptr;
    QLabel*       m_dot        = nullptr;
    QLabel*       m_desc       = nullptr;
    QLabel*       m_curStepLbl = nullptr;
    QPushButton*  m_expandBtn  = nullptr;
    QProgressBar* m_bar        = nullptr;
    QLabel*       m_elapsedLbl = nullptr;
    QPushButton*  m_cancelBtn  = nullptr;

    QWidget*     m_stepsWidget = nullptr;
    QVBoxLayout* m_stepsLay    = nullptr;
    bool         m_expanded    = false;

    QVector<StepEntry>  m_stepEntries;
    QMap<QString, int>  m_stepNameToIdx;

    QElapsedTimer m_jobElapsed;
    QTimer*       m_tickTimer = nullptr;
};

// ─── LogoBackgroundWidget ─────────────────────────────────────────────────────
// Plain QWidget that paints the Nutball-Labs logo as a very subtle watermark.
class LogoBackgroundWidget : public QWidget {
public:
    explicit LogoBackgroundWidget(QWidget* parent = nullptr) : QWidget(parent) {
        m_logo.load(":/images/Nutball-Labs_logo.png");
        setStyleSheet("background: #f8f8f8;");
    }
protected:
    void paintEvent(QPaintEvent* e) override {
        QWidget::paintEvent(e);
        if (m_logo.isNull()) return;
        QPainter p(this);
        p.setOpacity(0.05);
        int maxW = width()  / 2;
        int maxH = height() / 2;
        QSize scaled = m_logo.size().scaled(maxW, maxH, Qt::KeepAspectRatio);
        QRect r((width()  - scaled.width())  / 2,
                (height() - scaled.height()) / 2,
                scaled.width(), scaled.height());
        p.drawPixmap(r, m_logo.scaled(scaled, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation));
    }
private:
    QPixmap m_logo;
};

// ─── ToggleSwitch ─────────────────────────────────────────────────────────────
// Pill-shaped slider toggle: red (off) → slides to green (on).
class ToggleSwitch : public QWidget {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(38, 20);
        setCursor(Qt::PointingHandCursor);
    }
    bool isChecked() const { return m_on; }
    void setChecked(bool on) {
        if (m_on == on) return;
        m_on = on;
        update();
        emit toggled(on);
    }
signals:
    void toggled(bool on);
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor track = m_on ? QColor("#28a745") : QColor("#cc2222");
        p.setPen(Qt::NoPen);
        p.setBrush(track);
        p.drawRoundedRect(rect(), height() / 2, height() / 2);
        int d = height() - 4;
        int x = m_on ? width() - d - 2 : 2;
        p.setBrush(Qt::white);
        p.drawEllipse(x, 2, d, d);
    }
    void mousePressEvent(QMouseEvent*) override { setChecked(!m_on); }
private:
    bool m_on = false;
};

// ─── JobQueuePanel ────────────────────────────────────────────────────────────
JobQueuePanel::JobQueuePanel(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(80);

    auto* vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);

    // Header bar
    auto* header = new QWidget(this);
    header->setFixedHeight(28);
    header->setStyleSheet("background: #2c3e50;");
    auto* hlay = new QHBoxLayout(header);
    hlay->setContentsMargins(10, 0, 6, 0);
    hlay->setSpacing(6);

    auto* headerTitle = new QLabel("Job Queue", header);
    {
        QFont f = headerTitle->font();
        f.setBold(true);
        f.setPointSizeF(qMax(7.0, f.pointSizeF() - 0.5));
        headerTitle->setFont(f);
    }
    headerTitle->setStyleSheet("color: white;");
    hlay->addWidget(headerTitle, 1);

    m_detachBtn = new QPushButton("Detach", header);
    m_detachBtn->setFixedHeight(20);
    m_detachBtn->setStyleSheet(
        "QPushButton { border: 1px solid #aaa; border-radius: 2px; "
        "color: #ddd; background: transparent; font-size: 8pt; padding: 0 6px; }"
        "QPushButton:hover { background: #3d5166; }");
    connect(m_detachBtn, &QPushButton::clicked, this, &JobQueuePanel::detachRequested);
    hlay->addWidget(m_detachBtn, 0);

    vlay->addWidget(header);

    // Scrollable job list
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    auto* content = new LogoBackgroundWidget;
    m_rowLayout = new QVBoxLayout(content);
    m_rowLayout->setContentsMargins(0, 0, 0, 0);
    m_rowLayout->setSpacing(1);
    m_rowLayout->addStretch();
    m_scrollArea->setWidget(content);
    vlay->addWidget(m_scrollArea, 1);

    // Force arrow cursor on scroll area and scrollbar — prevents the vertical
    // scrollbar's bottom corner from leaking Qt::SizeFDiagCursor into the
    // bottom-right of the detached float window.
    m_scrollArea->setCursor(Qt::ArrowCursor);
    m_scrollArea->viewport()->setCursor(Qt::ArrowCursor);
    m_scrollArea->verticalScrollBar()->setCursor(Qt::ArrowCursor);

    // Intercept Ctrl+scroll on the scroll area and its viewport.
    m_scrollArea->installEventFilter(this);
    m_scrollArea->viewport()->installEventFilter(this);

    // ── Status bar (bottom): [counters | toggle + url] ───────────────────────
    auto* statusBar = new QWidget(this);
    statusBar->setFixedHeight(26);
    statusBar->setStyleSheet("background: #2c3e50;");
    auto* slay = new QHBoxLayout(statusBar);
    slay->setContentsMargins(10, 0, 10, 0);
    slay->setSpacing(0);

    // Left half — job counters
    m_titleLbl = new QLabel(statusBar);
    {
        QFont f = m_titleLbl->font();
        f.setPointSizeF(qMax(7.0, f.pointSizeF() - 0.5));
        m_titleLbl->setFont(f);
    }
    m_titleLbl->setStyleSheet("color: #ccc;");
    slay->addWidget(m_titleLbl, 1, Qt::AlignVCenter);

    // Right half — toggle + url
    m_monitorToggle = new ToggleSwitch(statusBar);
    connect(m_monitorToggle, &ToggleSwitch::toggled,
            this, &JobQueuePanel::onMonitorToggled);
    slay->addWidget(m_monitorToggle, 0, Qt::AlignVCenter);

    m_monitorLabel = new QLabel(statusBar);
    {
        QFont f = m_monitorLabel->font();
        f.setPointSizeF(qMax(6.5, f.pointSizeF() - 1.0));
        m_monitorLabel->setFont(f);
    }
    m_monitorLabel->setStyleSheet("color: #8fa8bf;");
    m_monitorLabel->setContentsMargins(6, 0, 0, 0);
    m_monitorLabel->hide();
    slay->addWidget(m_monitorLabel, 0, Qt::AlignVCenter);

    vlay->addWidget(statusBar);

    // Kill monitor cleanly if the app quits while it's running.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        if (m_monitorProc && m_monitorProc->state() != QProcess::NotRunning) {
            m_monitorProc->kill();
            m_monitorProc->waitForFinished(2000);
        }
    });

    connect(&JobQueue::instance(), &JobQueue::jobAdded,
            this, &JobQueuePanel::onJobAdded);
    connect(&JobQueue::instance(), &JobQueue::jobFinished,
            this, [this](Job*, bool) { updateTitle(); });
}

void JobQueuePanel::updateTitle()
{
    int completed = 0, queued = 0;
    for (Job* j : JobQueue::instance().jobs()) {
        auto s = j->state();
        if (s == Job::State::Done || s == Job::State::Failed || s == Job::State::Cancelled)
            ++completed;
        else
            ++queued;
    }
    m_titleLbl->setText(
        QString("Completed: %1   —   Queued: %2").arg(completed).arg(queued));
}

void JobQueuePanel::dismissRow(JobRowWidget* row)
{
    JobQueue::instance().removeJob(row->job());

    QLayout* lay = m_rowLayout;
    int idx = -1;
    for (int i = 0; i < lay->count(); ++i) {
        if (lay->itemAt(i) && lay->itemAt(i)->widget() == row) { idx = i; break; }
    }
    if (idx >= 0) {
        // Remove separator immediately before this row (if present)
        if (idx > 0) {
            auto* sepItem = lay->itemAt(idx - 1);
            if (QWidget* sep = sepItem ? sepItem->widget() : nullptr) {
                lay->removeWidget(sep);
                sep->deleteLater();
            }
        }
        lay->removeWidget(row);
        row->deleteLater();
    }
    updateTitle();
}

void JobQueuePanel::onDockStateChanged(bool floating)
{
    m_isDetached = floating;
    m_detachBtn->setVisible(!floating);
}

void JobQueuePanel::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const double delta = event->angleDelta().y() > 0 ? ZOOM_STEP : -ZOOM_STEP;
        m_zoomFactor = qBound(ZOOM_MIN, m_zoomFactor + delta, ZOOM_MAX);
        applyZoom();
        if (!m_isDetached)
            emit zoomChanged(m_zoomFactor);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

bool JobQueuePanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            wheelEvent(we);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void JobQueuePanel::setZoom(double factor)
{
    m_zoomFactor = qBound(ZOOM_MIN, factor, ZOOM_MAX);
    applyZoom();
}

void JobQueuePanel::applyZoom()
{
    QLayout* lay = m_rowLayout;
    for (int i = 0; i < lay->count(); ++i) {
        auto* item = lay->itemAt(i);
        if (!item) continue;
        if (auto* row = qobject_cast<JobRowWidget*>(item->widget()))
            row->setZoom(m_zoomFactor);
    }
}

void JobQueuePanel::onJobAdded(Job* job)
{
    // Insert before the trailing stretch
    int insertPos = m_rowLayout->count() - 1;
    auto* row = new JobRowWidget(job, m_scrollArea->widget());
    connect(row, &JobRowWidget::dismissRequested, this, &JobQueuePanel::dismissRow);
    if (m_zoomFactor != 1.0)
        row->setZoom(m_zoomFactor);
    m_rowLayout->insertWidget(insertPos, row);

    // Separate rows with a thin line
    if (insertPos > 0) {
        auto* sep = new QFrame(m_scrollArea->widget());
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("color: #e0e0e0;");
        m_rowLayout->insertWidget(insertPos, sep);
    }

    // Scroll to bottom so new jobs are visible
    QMetaObject::invokeMethod(this, [this]() {
        m_scrollArea->verticalScrollBar()->setValue(
            m_scrollArea->verticalScrollBar()->maximum());
    }, Qt::QueuedConnection);

    updateTitle();
}


void JobQueuePanel::onMonitorToggled(bool on)
{
    if (on) {
        // Find clops_monitor.py alongside the binary.
        QString script = MapRenderJob::findScript("clops_monitor.py");
        if (script.isEmpty()) {
            m_monitorToggle->setChecked(false);
            return;
        }

        // Kill any stale instance left over from a previous crashed session.
        // pkill returns 1 if no process matched — that's fine, ignore it.
        QProcess::execute("pkill", {"-f", "clops_monitor.py"});

        m_monitorProc = new QProcess(this);
        m_monitorProc->setProcessChannelMode(QProcess::MergedChannels);

        // Reset toggle when the script exits; show any error output inline.
        connect(m_monitorProc, &QProcess::finished,
                this, [this](int exitCode, QProcess::ExitStatus) {
            QString out = QString::fromUtf8(m_monitorProc->readAll()).trimmed();
            m_monitorToggle->setChecked(false);
            if (!out.isEmpty()) {
                // Show first line of output in red so the user can see why it failed.
                m_monitorLabel->setText(out.split('\n').first().left(80));
                m_monitorLabel->setStyleSheet("color: #ff6b6b;");
                m_monitorLabel->show();
            } else {
                m_monitorLabel->hide();
            }
            m_monitorProc->deleteLater();
            m_monitorProc = nullptr;
            Q_UNUSED(exitCode)
        });

        CamClops::ConfigManager _cfg;
        _cfg.loadSettings();
        int port = _cfg.getSettings().monitorPort;

        m_monitorProc->start("python3", {script, "--port", QString::number(port)});
        if (!m_monitorProc->waitForStarted(3000)) {
            m_monitorToggle->setChecked(false);
            m_monitorLabel->setText("failed to start python3");
            m_monitorLabel->setStyleSheet("color: #ff6b6b;");
            m_monitorLabel->show();
            m_monitorProc->deleteLater();
            m_monitorProc = nullptr;
            return;
        }

        QString host = QSysInfo::machineHostName();
        m_monitorLabel->setText(QString("http://%1:%2").arg(host).arg(port));
        m_monitorLabel->setStyleSheet("color: #8fa8bf;");
        m_monitorLabel->show();

    } else {
        if (m_monitorProc) {
            m_monitorProc->kill();
            m_monitorProc->waitForFinished(2000);
            m_monitorProc->deleteLater();
            m_monitorProc = nullptr;
        }
        m_monitorLabel->hide();
    }
}

#include "JobQueuePanel.moc"
// SN: 00117
