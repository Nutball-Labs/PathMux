// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "CameraProfilesDialog.h"
#include "platform.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace Pathmux;

CameraProfilesDialog::CameraProfilesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Camera Profiles");
    setMinimumSize(700, 460);
    resize(860, 560);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    auto* splitter = new QSplitter(Qt::Horizontal);

    // Left: profile list
    auto* leftBox = new QGroupBox("Profiles");
    auto* leftLayout = new QVBoxLayout(leftBox);
    leftLayout->setContentsMargins(6, 8, 6, 6);
    m_list = new QListWidget;
    m_list->setMinimumWidth(200);
    m_list->setMaximumWidth(300);
    leftLayout->addWidget(m_list);
    splitter->addWidget(leftBox);

    // Right: detail panel
    auto* rightBox = new QGroupBox("Details");
    auto* rightLayout = new QVBoxLayout(rightBox);
    rightLayout->setContentsMargins(8, 8, 8, 8);
    m_details = new QLabel;
    m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_details->setWordWrap(true);
    m_details->setTextFormat(Qt::RichText);
    m_details->setMinimumWidth(380);
    rightLayout->addWidget(m_details);
    rightLayout->addStretch();
    splitter->addWidget(rightBox);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter, 1);

    // Bottom buttons — left cluster: profile actions; right: close
    auto* btnLayout = new QHBoxLayout;

    m_forkBtn = new QPushButton("Fork as Custom…");
    m_forkBtn->setEnabled(false);
    m_forkBtn->setToolTip("Create an editable user copy of this built-in profile");
    btnLayout->addWidget(m_forkBtn);

    m_openEditorBtn = new QPushButton("Open in Editor");
    m_openEditorBtn->setEnabled(false);
    m_openEditorBtn->setToolTip("Open the profile JSON file in the system default editor");
    btnLayout->addWidget(m_openEditorBtn);

    m_setActiveBtn = new QPushButton("Set as Active");
    m_setActiveBtn->setEnabled(false);
    m_setActiveBtn->setToolTip("Use this profile when scanning new directories");
    btnLayout->addWidget(m_setActiveBtn);

    m_deleteBtn = new QPushButton("Delete");
    m_deleteBtn->setEnabled(false);
    m_deleteBtn->setToolTip("Delete this user profile (built-ins cannot be deleted)");
    btnLayout->addWidget(m_deleteBtn);

    btnLayout->addStretch();

    auto* closeBtn = new QPushButton("Close");
    closeBtn->setDefault(true);
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_list,          &QListWidget::currentRowChanged,
            this,            &CameraProfilesDialog::onSelectionChanged);
    connect(m_forkBtn,       &QPushButton::clicked, this, &CameraProfilesDialog::onFork);
    connect(m_openEditorBtn, &QPushButton::clicked, this, &CameraProfilesDialog::onOpenEditor);
    connect(m_setActiveBtn,  &QPushButton::clicked, this, &CameraProfilesDialog::onSetActive);
    connect(m_deleteBtn,     &QPushButton::clicked, this, &CameraProfilesDialog::onDelete);
    connect(closeBtn,        &QPushButton::clicked, this, &QDialog::accept);

    loadProfiles();
}

void CameraProfilesDialog::loadProfiles()
{
    int prevRow = m_list->currentRow();
    m_list->clear();
    m_details->clear();
    m_forkBtn->setEnabled(false);
    m_openEditorBtn->setEnabled(false);
    m_setActiveBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);

    m_profiles = m_config.loadAllProfiles();
    m_hasUserFile.clear();

    std::string activeId = m_config.getActiveProfileId();
    std::string profileDir = Platform::getConfigDir() + "profiles/";
    for (const auto& p : m_profiles) {
        bool hasFile = fs::exists(profileDir + p.profileId + ".json");
        m_hasUserFile.push_back(hasFile);

        QString label = QString::fromStdString(p.name);
        if (p.profileId == activeId)
            label += "  ★";  // star = active
        if (!hasFile)
            label += "  (built-in)";
        m_list->addItem(label);
    }

    // Restore selection or default to first item
    int row = (prevRow >= 0 && prevRow < m_list->count()) ? prevRow : 0;
    if (m_list->count() > 0)
        m_list->setCurrentRow(row);
}

void CameraProfilesDialog::showDetails(int index)
{
    if (index < 0 || index >= (int)m_profiles.size()) {
        m_details->clear();
        return;
    }
    const auto& p = m_profiles[index];
    bool isBuiltin = !m_hasUserFile[index];
    std::string activeId = m_config.getActiveProfileId();

    QString html;
    html += "<table cellspacing='4'>";
    auto row = [&](const char* label, const QString& val) {
        html += "<tr><td><b>" + QString(label) + "</b></td><td>" + val + "</td></tr>";
    };
    row("Name",      QString::fromStdString(p.name));
    row("Profile ID","<tt>" + QString::fromStdString(p.profileId) + "</tt>"
                     + (p.profileId == activeId ? " &nbsp;<b>(active)</b>" : ""));
    row("GPS method",QString::fromStdString(p.gpsMethod));
    if (!p.gpsExiftoolArgs.empty())
        row("GPS args",  "<tt>" + QString::fromStdString(p.gpsExiftoolArgs).toHtmlEscaped() + "</tt>");
    row("Timestamp", QString::fromStdString(p.timestampSource));
    row("Container", "<tt>" + QString::fromStdString(p.containerExt) + "</tt>");
    row("Layout",    QString::fromStdString(p.defaultLayout));
    html += "</table>";

    html += "<p><b>Camera slots:</b></p><ul style='margin-top:0'>";
    for (const auto& s : p.cameraSlots) {
        html += "<li><b>" + QString::fromStdString(s.name) + "</b>";
        if (!s.displayName.empty() && s.displayName != s.name)
            html += " (" + QString::fromStdString(s.displayName) + ")";
        if (s.isPrimary)
            html += " &nbsp;<span style='color:#888'><i>primary</i></span>";
        if (!s.scanSubdir.empty())
            html += "<br>&nbsp;&nbsp;Subdir: <tt>" + QString::fromStdString(s.scanSubdir) + "</tt>";
        if (!s.scanSubdirCandidates.empty()) {
            html += "<br>&nbsp;&nbsp;Fallbacks: ";
            for (size_t i = 0; i < s.scanSubdirCandidates.size(); ++i) {
                if (i) html += ", ";
                html += "<tt>" + QString::fromStdString(s.scanSubdirCandidates[i]) + "</tt>";
            }
        }
        if (!s.filenameToken.empty())
            html += "<br>&nbsp;&nbsp;Token: <tt>" + QString::fromStdString(s.filenameToken) + "</tt>";
        html += "</li>";
    }
    html += "</ul>";

    if (isBuiltin) {
        html += "<p><i>Source: built-in — click <b>Fork as Custom</b> to create an editable copy</i></p>";
    } else {
        std::string profileDir2 = Platform::getConfigDir() + "profiles/";
        html += "<p>Source: <tt>" + QString::fromStdString(profileDir2 + p.profileId + ".json") + "</tt></p>";
    }

    m_details->setText(html);
}

void CameraProfilesDialog::onSelectionChanged()
{
    int idx = m_list->currentRow();
    showDetails(idx);
    bool valid    = (idx >= 0 && idx < (int)m_profiles.size());
    bool hasFile  = valid && m_hasUserFile[idx];
    bool isBuiltin = valid && !hasFile;

    m_forkBtn->setEnabled(isBuiltin);
    m_openEditorBtn->setEnabled(hasFile);
    m_setActiveBtn->setEnabled(valid);
    m_deleteBtn->setEnabled(hasFile);
}

void CameraProfilesDialog::onFork()
{
    int idx = m_list->currentRow();
    if (idx < 0 || idx >= (int)m_profiles.size() || m_hasUserFile[idx]) return;

    const auto& p = m_profiles[idx];
    std::string profileDir = Platform::getConfigDir() + "profiles/";
    std::error_code ec;
    fs::create_directories(profileDir, ec);
    std::string destFile = profileDir + p.profileId + ".json";

    try {
        p.saveToFile(destFile);
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "Fork Failed",
            QString("Could not write profile file:\n%1\n\n%2")
                .arg(QString::fromStdString(destFile))
                .arg(QString::fromLatin1(ex.what())));
        return;
    }

    QMessageBox::information(this, "Profile Forked",
        QString("Profile saved to:\n%1\n\n"
                "Edit the JSON file to customise this profile, then click\n"
                "Open in Editor or use any text editor.\n\n"
                "Changes take effect after you rescan the footage directory.")
            .arg(QString::fromStdString(destFile)));

    loadProfiles();
    // Re-select the same profile (now shows as user file)
    for (int i = 0; i < (int)m_profiles.size(); ++i)
        if (m_profiles[i].profileId == p.profileId)
            { m_list->setCurrentRow(i); break; }
}

void CameraProfilesDialog::onOpenEditor()
{
    int idx = m_list->currentRow();
    if (idx < 0 || idx >= (int)m_profiles.size() || !m_hasUserFile[idx]) return;

    std::string profileDir = Platform::getConfigDir() + "profiles/";
    QString filePath = QString::fromStdString(profileDir + m_profiles[idx].profileId + ".json");
    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}

void CameraProfilesDialog::onSetActive()
{
    int idx = m_list->currentRow();
    if (idx < 0 || idx >= (int)m_profiles.size()) return;

    const std::string& pid = m_profiles[idx].profileId;
    auto s = m_config.getSettings();
    s.activeProfileId = pid;
    m_config.applySettings(s);
    m_config.saveSettings();

    loadProfiles();  // refreshes the star indicator
}

void CameraProfilesDialog::onDelete()
{
    int idx = m_list->currentRow();
    if (idx < 0 || idx >= (int)m_profiles.size() || !m_hasUserFile[idx]) return;

    const auto& p = m_profiles[idx];
    auto ans = QMessageBox::question(
        this, "Delete Profile",
        QString("Delete profile \"%1\" (%2)?\n\n"
                "Manifests that embedded this profile will continue to work — "
                "the snapshot is stored inside each manifest file.")
            .arg(QString::fromStdString(p.name))
            .arg(QString::fromStdString(p.profileId)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes) return;

    std::string userFile = Platform::getConfigDir() + "profiles/" + p.profileId + ".json";
    std::error_code ec;
    fs::remove(userFile, ec);
    if (ec) {
        QMessageBox::warning(this, "Delete Failed",
            QString("Could not delete:\n%1\n\n%2")
                .arg(QString::fromStdString(userFile))
                .arg(QString::fromStdString(ec.message())));
        return;
    }

    loadProfiles();
}
// SN: 00104
