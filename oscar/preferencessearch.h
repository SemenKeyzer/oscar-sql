/* Preferences Search Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PREFERENCESSEARCH_H
#define PREFERENCESSEARCH_H

#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

class QTabWidget;

//! Finds a setting in the Preferences dialog by what it is called or what its tooltip says.
namespace PreferencesSearch {

//! One place a search can lead to: a setting's widget, or a row of a channel list.
struct Entry {
    int tab = -1;
    QString tabTitle;
    QString text;               //!< what the setting is called, as shown
    QString tooltip;
    QPointer<QWidget> widget;   //!< the setting, or the item view holding the row
    QPointer<QWidget> page;     //!< the tab's page, to tell a hidden setting from a shown one
    bool viewRow = false;       //!< text names a row of the item view in widget
};

//! Everything in \a tabs a search can find: the texts of buttons and labels, group titles,
//! tooltips, and the rows of item views.
QList<Entry> index(QTabWidget *tabs);
//! \a text as searched: case and accelerators dropped, HTML stripped, "ё" read as "е".
QString normalized(const QString &text);
//! The entries matching \a query (two characters or more, every word of it): matches in what
//! a setting is called first, then in tooltips; hidden settings left out; at most \a limit.
QList<Entry> find(const QList<Entry> &entries, const QString &query, int limit = 20);
//! "Tab › setting", for the list of results.
QString label(const Entry &entry);
//! Shows \a entry: switches to its tab, scrolls to it and highlights it for a moment, or
//! selects its row.
void reveal(QTabWidget *tabs, const Entry &entry);
//! The style a revealed setting gets while it is highlighted.
QString highlightStyle();

} // namespace PreferencesSearch

#endif // PREFERENCESSEARCH_H
