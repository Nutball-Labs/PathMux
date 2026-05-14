// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "ManifestPanel.h"
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStyleOption>
#include <QWheelEvent>
#include <QMenu>
#include <algorithm>

using namespace CamClops;

// ---------------------------------------------------------------------------
// ManifestItemDelegate — two-line list item: nickname + trip count
// ---------------------------------------------------------------------------
class ManifestItemDelegate : public QStyledItemDelegate {
public:
    explicit ManifestItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override
    {
        p->save();

        bool selected = opt.state & QStyle::State_Selected;
        p->fillRect(opt.rect, selected ? opt.palette.highlight()
                                       : opt.palette.base());

        QRect r = opt.rect.adjusted(10, 4, -10, -4);
        int lineH = opt.fontMetrics.height();

        // Line 1: nickname (left, bold, elided middle) + ID badge (right)
        QFont f1 = opt.font;
        f1.setBold(true);
        p->setFont(f1);
        p->setPen(selected ? opt.palette.highlightedText().color()
                           : opt.palette.text().color());

        QString idStr  = idx.data(Qt::UserRole + 2).toString(); // e.g. "[PM]"
        int     idW    = idStr.isEmpty() ? 0
                       : opt.fontMetrics.horizontalAdvance(idStr) + 6;
        QRect   line1R = r.adjusted(0, 0, 0, -(lineH + 4));

        // Draw ID right-aligned first
        if (idW > 0) {
            QColor idCol = selected ? opt.palette.highlightedText().color()
                                    : opt.palette.placeholderText().color();
            p->setPen(idCol);
            p->drawText(line1R, Qt::AlignBottom | Qt::AlignRight, idStr);
            p->setPen(selected ? opt.palette.highlightedText().color()
                               : opt.palette.text().color());
        }

        // Draw path left-aligned with room for the ID
        QString nick   = idx.data(Qt::DisplayRole).toString();
        QString elided = opt.fontMetrics.elidedText(
            nick, Qt::ElideMiddle, line1R.width() - idW);
        p->drawText(line1R, Qt::AlignBottom | Qt::AlignLeft, elided);

        // Line 2: trip count — small, subdued
        QFont f2 = opt.font;
        f2.setPointSize(qMax(7, f2.pointSize() - 1));
        p->setFont(f2);
        QColor sub = selected ? opt.palette.highlightedText().color()
                              : opt.palette.placeholderText().color();
        p->setPen(sub);
        QString detail = idx.data(Qt::UserRole).toString();
        p->drawText(r.adjusted(0, lineH + 4, 0, 0),
                    Qt::AlignTop | Qt::AlignLeft, detail);

        p->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& opt,
                   const QModelIndex&) const override
    {
        return QSize(200, opt.fontMetrics.height() * 2 + 16);
    }
};

// ---------------------------------------------------------------------------
// ManifestPanel
// ---------------------------------------------------------------------------
ManifestPanel::ManifestPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);

    // Toolbar: sort combobox + add button
    auto* toolRow = new QHBoxLayout;
    toolRow->setContentsMargins(6, 6, 6, 4);
    toolRow->addWidget(new QLabel("Sort:"));
    m_sortCombo = new QComboBox;
    m_sortCombo->addItem("Most Recent", 0);
    m_sortCombo->addItem("Most Trips",  1);
    m_sortCombo->addItem("Name A→Z",    2);
    toolRow->addWidget(m_sortCombo, 1);
    m_addBtn = new QPushButton("+");
    m_addBtn->setFixedSize(26, 26);
    m_addBtn->setToolTip("Scan new source directory");
    toolRow->addWidget(m_addBtn);
    vlay->addLayout(toolRow);

    // Separator
    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    vlay->addWidget(sep);

    // List
    m_list = new QListWidget;
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_delegate = new ManifestItemDelegate(m_list);
    m_list->setItemDelegate(m_delegate);
    vlay->addWidget(m_list, 1);

    connect(m_list,     &QListWidget::itemClicked,
            this,       &ManifestPanel::onItemClicked);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,        &ManifestPanel::onSortChanged);
    connect(m_addBtn,   &QPushButton::clicked,
            this,       &ManifestPanel::onAddClicked);

    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested,
            this,   &ManifestPanel::onContextMenu);

    // Capture base font size for scaling; install filter to intercept Ctrl+wheel
    double pt = m_list->font().pointSizeF();
    m_baseFontPt = (pt > 0) ? pt : 9.0;
    m_list->installEventFilter(this);

    // Transparent list so the watermark logo shows through behind items.
    m_list->setStyleSheet("QListWidget { background: transparent; border: none; }");
    m_list->viewport()->setAutoFillBackground(false);

    m_bgLogo = QPixmap(":/images/Nutball-Labs_logo.png");

    refresh();
}

void ManifestPanel::paintEvent(QPaintEvent* ev)
{
    QWidget::paintEvent(ev);
    if (m_bgLogo.isNull()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(0.25);
    // Scale to fill the full panel width; letterbox vertically.
    QPixmap sc = m_bgLogo.scaledToWidth(width(), Qt::SmoothTransformation);
    int y = (height() - sc.height()) / 2;
    p.drawPixmap(0, y, sc);
}

void ManifestPanel::refresh()
{
    ConfigManager config;
    config.loadSettings();
    m_entries = config.loadManifestIndex();
    applySort();
    populateList();
}

void ManifestPanel::selectEntry(const ManifestEntry& entry)
{
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole + 1).toString()
                == QString::fromStdString(entry.manifestFile)) {
            m_list->setCurrentRow(i);
            return;
        }
    }
}

void ManifestPanel::applySort()
{
    switch (m_sortCombo->currentIndex()) {
    case 1:
        std::sort(m_entries.begin(), m_entries.end(),
            [](const ManifestEntry& a, const ManifestEntry& b){
                return a.tripCount > b.tripCount;
            });
        break;
    case 2:
        std::sort(m_entries.begin(), m_entries.end(),
            [](const ManifestEntry& a, const ManifestEntry& b){
                return a.nickname < b.nickname;
            });
        break;
    default:
        break; // loadManifestIndex() already sorts by lastTrip desc
    }
}

void ManifestPanel::populateList()
{
    m_list->clear();
    for (const auto& e : m_entries) {
        auto* item = new QListWidgetItem(m_list);
        QString nick = QString::fromStdString(
            e.nickname.empty() ? e.path : e.nickname);
        item->setData(Qt::DisplayRole, nick);
        item->setData(Qt::UserRole,
            QString("%1 trip%2").arg(e.tripCount).arg(e.tripCount == 1 ? "" : "s"));
        item->setData(Qt::UserRole + 1,
            QString::fromStdString(e.manifestFile));
        QString id = QString::fromStdString(e.id).toUpper();
        item->setData(Qt::UserRole + 2,
            id.isEmpty() ? QString() : QString("[%1]").arg(id));
    }
}

void ManifestPanel::onItemClicked(QListWidgetItem* item)
{
    if (!item) return;
    int row = m_list->row(item);
    if (row >= 0 && row < (int)m_entries.size())
        emit manifestSelected(m_entries[row]);
}

void ManifestPanel::onSortChanged(int)
{
    applySort();
    populateList();
}

void ManifestPanel::onAddClicked()
{
    emit scanRequested();
}

void ManifestPanel::onContextMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_list->itemAt(pos);
    if (!item) return;
    int row = m_list->row(item);
    if (row < 0 || row >= (int)m_entries.size()) return;

    QMenu menu(this);
    QAction* rebuildAct = menu.addAction("Rebuild Manifest");
    if (menu.exec(m_list->mapToGlobal(pos)) == rebuildAct)
        emit rebuildRequested(m_entries[row]);
}

void ManifestPanel::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const double delta = event->angleDelta().y() > 0 ? ZOOM_STEP : -ZOOM_STEP;
        m_zoomFactor = qBound(ZOOM_MIN, m_zoomFactor + delta, ZOOM_MAX);
        applyListZoom();
        emit zoomChanged(m_zoomFactor);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

bool ManifestPanel::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            wheelEvent(we);
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ManifestPanel::setZoom(double factor)
{
    m_zoomFactor = qBound(ZOOM_MIN, factor, ZOOM_MAX);
    applyListZoom();
}

void ManifestPanel::applyListZoom()
{
    QFont f = m_list->font();
    f.setPointSizeF(m_baseFontPt * m_zoomFactor);
    m_list->setFont(f);
    m_list->update();
}
// SN: 00109
