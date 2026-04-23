// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "HelpDialog.h"
#include "version.hpp"
#include <QSplitter>
#include <QListWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QUrl>
#include <QFile>
#include <QResizeEvent>

HelpDialog::HelpDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("PathMux Help");
    resize(960, 680);

    m_splitter   = new QSplitter(Qt::Horizontal, this);
    m_topicList  = new QListWidget(m_splitter);
    m_browser    = new QTextBrowser(m_splitter);

    m_topicList->setFixedWidth(190);
    m_topicList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_browser->setOpenExternalLinks(false);
    m_browser->setOpenLinks(false);  // handle internally via anchorClicked

    m_splitter->addWidget(m_topicList);
    m_splitter->addWidget(m_browser);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    // ---- Topic list ----
    addTopic("Overview",        "qrc:/help/index.html");
    addTopic("GUI Overview",    "qrc:/help/gui_overview.html");
    addTopic("pathmux",         "qrc:/help/pathmux.html");
    addTopic("pm_probe",        "qrc:/help/pm_probe.html");
    addTopic("pm_gpsinfo",      "qrc:/help/pm_gpsinfo.html");
    addTopic("pm_gpsexport",    "qrc:/help/pm_gpsexport.html");
    addTopic("pm_ls",           "qrc:/help/pm_ls.html");
    addTopic("pm_audit",        "qrc:/help/pm_audit.html");
    addTopic("pm_findgpslock",  "qrc:/help/pm_findgpslock.html");
    addTopic("pm_tripdebug",    "qrc:/help/pm_tripdebug.html");
    addTopic("About",           "internal:about");

    // Pre-cache logo images as base64 data URIs (used by the About page).
    auto encodeImg = [](const QString& path) -> QString {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return "data:image/png;base64," + QString::fromLatin1(f.readAll().toBase64());
    };
    m_pmLogoB64 = encodeImg(":/images/pathmux_256.png");
    m_nlLogoB64 = encodeImg(":/images/Nutball-Labs_logo.png");

    // ---- Layout ----
    auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(m_splitter, 1);
    layout->addWidget(closeBox, 0);
    setLayout(layout);

    connect(m_topicList, &QListWidget::currentRowChanged,
            this,         &HelpDialog::onTopicSelected);

    // Intercept anchor clicks so cross-topic links (e.g. "See Also") work.
    connect(m_browser, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        QString s = url.toString();
        // Strip leading qrc:/help/ if present, treat bare filename as resource.
        if (s.endsWith(".html")) {
            QString stem = url.fileName();
            stem.chop(5); // remove ".html"
            showTopic(stem);
        }
    });

    // Pre-select Overview so the browser is populated immediately on open.
    m_topicList->setCurrentRow(0);
}

void HelpDialog::addTopic(const QString& label, const QString& resourceUrl)
{
    m_topics.append({label, resourceUrl});
    m_topicList->addItem(label);
}

void HelpDialog::loadTopic(int index)
{
    if (index < 0 || index >= m_topics.size()) return;
    if (m_topics[index].resourceUrl == "internal:about") {
        loadAboutPage();
        return;
    }
    m_showingAbout = false;
    // setSource() handles qrc: URLs natively: loads the resource, sets the
    // document base URL to qrc:/help/ automatically so relative CSS/image
    // references resolve correctly, and respects setOpenLinks(false).
    m_browser->setSource(QUrl(m_topics[index].resourceUrl));
}

void HelpDialog::loadAboutPage()
{
    m_showingAbout = true;

    // Size both logos to fill the browser content area.  Qt's HTML renderer
    // does not support percentage widths on <img>, so we pass pixel widths
    // derived from the current viewport and regenerate on resize.
    int bw = m_browser->viewport()->width();
    if (bw < 200) bw = 700;   // fallback before first paint
    int padding = 40;          // body padding left+right
    int avail   = bw - padding;

    // PathMux logo is square-ish (256×256); cap at 220px so it doesn't dwarf text.
    int pmW = qMin(avail, 220);
    // Nutball Labs logo is wide; let it fill most of the available width.
    int nlW = qMin(avail, 500);

    QString html =
        "<!DOCTYPE html><html><head><style>"
        "body  { font-family: sans-serif; text-align: center;"
        "        padding: 20px " + QString::number(padding / 2) + "px;"
        "        background: #f8f8fa; }"
        "h1    { font-size: 22px; margin: 10px 0 4px 0; }"
        ".ver  { font-size: 13px; color: #444; margin: 0 0 6px 0; }"
        ".tag  { margin: 0 0 14px 0; }"
        ".meta { font-size: 12px; color: #556677; line-height: 1.9; }"
        "hr    { border: none; border-top: 1px solid #d0d4da;"
        "        margin: 20px auto; width: 70%; }"
        "</style></head><body>"
        "<img src=\"" + m_pmLogoB64 + "\" width=\"" + QString::number(pmW) + "\">"
        "<h1>PathMux</h1>"
        "<p class=\"ver\">Version " APP_VERSION "</p>"
        "<p class=\"tag\">Dashcam footage organizer &amp; GPS track extractor</p>"
        "<p class=\"meta\">"
        "&copy; 2026 Nutball Labs / Stephen Berg<br>"
        "Licensed under the GNU General Public License v3 or later<br><br>"
        "PathMux: https://github.com/Nutball-Labs/PathMux<br>"
        "Nutball Labs: https://github.com/Nutball-Labs"
        "</p>"
        "<hr>"
        "<img src=\"" + m_nlLogoB64 + "\" width=\"" + QString::number(nlW) + "\">"
        "</body></html>";

    m_browser->setHtml(html);
}

void HelpDialog::resizeEvent(QResizeEvent* e)
{
    QDialog::resizeEvent(e);
    if (m_showingAbout)
        loadAboutPage();
}

void HelpDialog::onTopicSelected(int row)
{
    loadTopic(row);
}

void HelpDialog::showTopic(const QString& topic)
{
    // Match by resource filename stem (e.g. "pathmux" → "qrc:/help/pathmux.html")
    // or by label (e.g. "Overview", "About").
    for (int i = 0; i < m_topics.size(); ++i) {
        const Topic& t = m_topics[i];
        QString stem = QUrl(t.resourceUrl).fileName();
        if (stem.endsWith(".html")) stem.chop(5);
        if (stem == topic || t.label == topic) {
            m_topicList->setCurrentRow(i);
            return;
        }
    }
    // Fallback: load Overview
    m_topicList->setCurrentRow(0);
}
// SN: 00098
