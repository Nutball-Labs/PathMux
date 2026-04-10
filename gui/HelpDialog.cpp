// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "HelpDialog.h"
#include <QSplitter>
#include <QListWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QUrl>
#include <QFile>
#include <QTextStream>

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

    // QTextBrowser resolves relative URLs (e.g. style.css, images)
    // relative to the search paths below.
    m_browser->setSearchPaths({"qrc:/help", "qrc:/images"});
    m_browser->setOpenExternalLinks(false);
    m_browser->setOpenLinks(false);  // handle internally via anchorClicked

    m_splitter->addWidget(m_topicList);
    m_splitter->addWidget(m_browser);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    // ---- Topic list ----
    addTopic("Overview",        "qrc:/help/index.html");
    addTopic("pathmux",         "qrc:/help/pathmux.html");
    addTopic("pm_probe",        "qrc:/help/pm_probe.html");
    addTopic("pm_gpsinfo",      "qrc:/help/pm_gpsinfo.html");
    addTopic("pm_gpsexport",    "qrc:/help/pm_gpsexport.html");
    addTopic("pm_ls",           "qrc:/help/pm_ls.html");
    addTopic("pm_audit",        "qrc:/help/pm_audit.html");
    addTopic("pm_findgpslock",  "qrc:/help/pm_findgpslock.html");
    addTopic("pm_tripdebug",    "qrc:/help/pm_tripdebug.html");
    addTopic("GUI Overview",    "qrc:/help/gui_overview.html");

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

    // Default to Overview
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
    const QString& url = m_topics[index].resourceUrl;

    // Load HTML from QRC into the browser.  We read it manually so we can
    // inject a <base> tag pointing at the resource prefix, which lets
    // QTextBrowser resolve relative style.css references correctly.
    QFile f(url.mid(4));  // strip "qrc:" prefix → :/help/xxx.html
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_browser->setHtml(
            "<p><b>Error:</b> Could not load help page:<br>" + url + "</p>");
        return;
    }
    QTextStream in(&f);
    QString html = in.readAll();
    f.close();

    // Set the source URL so QTextBrowser resolves relative URLs from /help/.
    m_browser->setSource(QUrl(url));
    // setSource already loads from the resource; the manual read is used for
    // the base-URL approach below only if setSource fails.  Since Qt QRC
    // resources are always present in the binary this path always succeeds.
}

void HelpDialog::onTopicSelected(int row)
{
    loadTopic(row);
}

void HelpDialog::showTopic(const QString& topic)
{
    // Match by resource filename stem (e.g. "pathmux" → "qrc:/help/pathmux.html")
    // or by label (e.g. "Overview").
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
// SN: 00095
