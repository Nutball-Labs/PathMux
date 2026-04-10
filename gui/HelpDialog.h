// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once
#include <QDialog>
#include <QStringList>

class QListWidget;
class QTextBrowser;
class QSplitter;

class HelpDialog : public QDialog {
    Q_OBJECT
public:
    explicit HelpDialog(QWidget* parent = nullptr);

    // Open the dialog with a specific topic pre-selected.
    // topic: one of "index", "pathmux", "pm_probe", "pm_gpsinfo",
    //        "pm_gpsexport", "pm_ls", "pm_audit", "pm_findgpslock",
    //        "pm_tripdebug", "gui_overview"
    void showTopic(const QString& topic);

private slots:
    void onTopicSelected(int row);

private:
    struct Topic {
        QString label;
        QString resourceUrl;  // e.g. "qrc:/help/pathmux.html"
    };

    void addTopic(const QString& label, const QString& resourceUrl);
    void loadTopic(int index);

    QSplitter*    m_splitter;
    QListWidget*  m_topicList;
    QTextBrowser* m_browser;
    QList<Topic>  m_topics;
};
// SN: 00095
