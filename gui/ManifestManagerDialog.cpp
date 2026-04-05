// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "ManifestManagerDialog.h"
#include "ScanProgressDialog.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QMessageBox>
#include <filesystem>

namespace fs = std::filesystem;
using namespace Pathmux;

// ---------------------------------------------------------------------------
// ManifestManagerDialog
// ---------------------------------------------------------------------------

ManifestManagerDialog::ManifestManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Manage Manifests");
    setMinimumSize(700, 380);
    resize(800, 450);

    auto* vlay = new QVBoxLayout(this);
    vlay->setSpacing(6);
    vlay->setContentsMargins(10, 10, 10, 10);

    vlay->addWidget(new QLabel("Known source manifests. Select one and choose an action."));

    // Table
    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels({"ID", "Nickname / Path", "Trips", "Last Scan", "Manifest File"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    vlay->addWidget(m_table, 1);

    connect(m_table, &QTableWidget::itemSelectionChanged,
            this,    &ManifestManagerDialog::onSelectionChanged);

    // Action buttons
    auto* btnRow = new QHBoxLayout;
    m_refreshBtn = new QPushButton("Refresh");
    m_refreshBtn->setToolTip("Rescan the source directory to update trip list and timestamps");
    m_rewriteBtn = new QPushButton("Rewrite");
    m_rewriteBtn->setToolTip("Force a full rescan, discarding any cached data first");
    m_deleteBtn  = new QPushButton("Delete");
    m_deleteBtn->setToolTip("Remove this manifest from the known-manifests index");
    m_nickBtn    = new QPushButton("Edit Nickname");
    m_nickBtn->setToolTip("Set a friendly display name for this source directory");

    for (auto* b : {m_refreshBtn, m_rewriteBtn, m_deleteBtn, m_nickBtn})
        b->setEnabled(false);

    btnRow->addWidget(m_refreshBtn);
    btnRow->addWidget(m_rewriteBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addWidget(m_nickBtn);
    btnRow->addStretch();
    vlay->addLayout(btnRow);

    // Close button
    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::accept);
    vlay->addWidget(bbox);

    connect(m_refreshBtn, &QPushButton::clicked, this, &ManifestManagerDialog::onRefresh);
    connect(m_rewriteBtn, &QPushButton::clicked, this, &ManifestManagerDialog::onRewrite);
    connect(m_deleteBtn,  &QPushButton::clicked, this, &ManifestManagerDialog::onDelete);
    connect(m_nickBtn,    &QPushButton::clicked, this, &ManifestManagerDialog::onEditNickname);

    loadManifests();
}

void ManifestManagerDialog::loadManifests()
{
    ConfigManager config;
    config.loadSettings();
    m_entries = config.loadManifestIndex();

    m_table->setRowCount(0);
    for (const auto& e : m_entries) {
        int row = m_table->rowCount();
        m_table->insertRow(row);

        auto* idItem = new QTableWidgetItem(QString::fromStdString(e.id));
        idItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, 0, idItem);

        QString nick = QString::fromStdString(e.nickname.empty() ? e.path : e.nickname);
        m_table->setItem(row, 1, new QTableWidgetItem(nick));
        m_table->setItem(row, 2, new QTableWidgetItem(QString::number(e.tripCount)));
        m_table->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(e.lastScan)));
        m_table->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(e.manifestFile)));
    }
    updateButtons();
}

int ManifestManagerDialog::selectedRow() const
{
    auto items = m_table->selectedItems();
    return items.isEmpty() ? -1 : m_table->row(items.first());
}

void ManifestManagerDialog::updateButtons()
{
    bool sel = selectedRow() >= 0;
    m_refreshBtn->setEnabled(sel);
    m_rewriteBtn->setEnabled(sel);
    m_deleteBtn->setEnabled(sel);
    m_nickBtn->setEnabled(sel);
}

void ManifestManagerDialog::onSelectionChanged()
{
    updateButtons();
}

void ManifestManagerDialog::onRefresh()
{
    int row = selectedRow();
    if (row < 0 || row >= (int)m_entries.size()) return;

    const ManifestEntry& e = m_entries[row];
    ScanProgressDialog dlg(this);
    connect(&dlg, &ScanProgressDialog::scanComplete,
            this, &ManifestManagerDialog::onScanComplete);
    dlg.startScan(QString::fromStdString(e.path));
    dlg.exec();
}

void ManifestManagerDialog::onRewrite()
{
    int row = selectedRow();
    if (row < 0 || row >= (int)m_entries.size()) return;

    const ManifestEntry& e = m_entries[row];

    // Delete the manifest file so the scan starts fully fresh.
    if (!e.manifestFile.empty()) {
        std::error_code ec;
        fs::remove(e.manifestFile, ec);
    }

    ScanProgressDialog dlg(this);
    connect(&dlg, &ScanProgressDialog::scanComplete,
            this, &ManifestManagerDialog::onScanComplete);
    dlg.startScan(QString::fromStdString(e.path));
    dlg.exec();
}

void ManifestManagerDialog::onDelete()
{
    int row = selectedRow();
    if (row < 0 || row >= (int)m_entries.size()) return;

    const ManifestEntry& e = m_entries[row];
    QString nick = QString::fromStdString(e.nickname.empty() ? e.path : e.nickname);

    auto answer = QMessageBox::question(
        this, "Delete Manifest",
        "Remove \"" + nick + "\" from the manifest index?\n\n"
        "This does not delete the source footage. The manifest file on disk "
        "will also be removed if it exists.",
        QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    // Remove manifest file from disk
    if (!e.manifestFile.empty()) {
        std::error_code ec;
        fs::remove(e.manifestFile, ec);
    }

    // Update index — rebuild without this entry
    ConfigManager config;
    config.loadSettings();
    auto index = config.loadManifestIndex();
    index.erase(std::remove_if(index.begin(), index.end(),
        [&](const ManifestEntry& x){ return x.id == e.id; }), index.end());
    config.saveManifestIndex(index);

    emit manifestsChanged();
    loadManifests();
}

void ManifestManagerDialog::onEditNickname()
{
    int row = selectedRow();
    if (row < 0 || row >= (int)m_entries.size()) return;

    const ManifestEntry& e = m_entries[row];
    bool ok;
    QString cur = QString::fromStdString(e.nickname);
    QString newNick = QInputDialog::getText(
        this, "Edit Nickname", "Nickname for this source directory:", QLineEdit::Normal, cur, &ok);
    if (!ok) return;

    ConfigManager config;
    config.loadSettings();
    config.setManifestNickname(e.id, newNick.toStdString());

    emit manifestsChanged();
    loadManifests();
}

void ManifestManagerDialog::onScanComplete(const Pathmux::ManifestEntry&)
{
    emit manifestsChanged();
    loadManifests();
}
// SN: 00095
