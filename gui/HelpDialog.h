// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <QString>

class QListWidget;
class QTextBrowser;
class QSplitter;
class QResizeEvent;

class HelpDialog : public QDialog {
    Q_OBJECT
public:
    explicit HelpDialog(QWidget* parent = nullptr);

    // Open the dialog with a specific topic pre-selected.
    // topic: one of "index", "pathmux", "pm_probe", "pm_gpsinfo",
    //        "pm_gpsexport", "pm_ls", "pm_audit", "pm_findgpslock",
    //        "pm_tripdebug", "gui_overview", "About"
    void showTopic(const QString& topic);

protected:
    void resizeEvent(QResizeEvent* e) override;

private slots:
    void onTopicSelected(int row);

private:
    struct Topic {
        QString label;
        QString resourceUrl;  // e.g. "qrc:/help/pathmux.html" or "internal:about"
    };

    void addTopic(const QString& label, const QString& resourceUrl);
    void loadTopic(int index);
    void loadAboutPage();   // generates About HTML dynamically (version + logos)

    QSplitter*    m_splitter;
    QListWidget*  m_topicList;
    QTextBrowser* m_browser;
    QList<Topic>  m_topics;

    QString m_pmLogoB64;        // base64 data URI for pathmux_256.png
    QString m_nlLogoB64;        // base64 data URI for Nutball-Labs_logo.png
    bool    m_showingAbout = false;
};
// SN: 00097
