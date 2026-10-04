/* Preferences Search
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "preferencessearch.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QScrollArea>
#include <QTabWidget>
#include <QTextDocumentFragment>
#include <QTimer>
#include <algorithm>

namespace PreferencesSearch {

namespace {

// A text as the user sees it: no HTML, accelerator ampersands gone ("&&" is a real one).
QString shown(const QString &text)
{
    QString plain = Qt::mightBeRichText(text) ? QTextDocumentFragment::fromHtml(text).toPlainText() : text;
    plain.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    plain.remove(QLatin1Char('&'));
    plain.replace(QStringLiteral("\x01"), QStringLiteral("&"));
    return plain.simplified();
}

bool hasLetter(const QString &text)
{
    return std::any_of(text.cbegin(), text.cend(), [](QChar c) { return c.isLetter(); });
}

// The rows of an item view's model, all levels, by their first column.
void addRows(QList<Entry> &entries, const Entry &base, const QAbstractItemModel *model, const QModelIndex &parent)
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        Entry e = base;
        e.text = shown(index.data().toString());
        e.tooltip = shown(index.data(Qt::ToolTipRole).toString());
        if (hasLetter(e.text)) entries << e;
        addRows(entries, base, model, index);
    }
}

} // namespace

QList<Entry> index(QTabWidget *tabs)
{
    QList<Entry> entries;
    if (!tabs) return entries;
    for (int tab = 0; tab < tabs->count(); ++tab) {
        QWidget *page = tabs->widget(tab);
        Entry base;
        base.tab = tab;
        base.tabTitle = shown(tabs->tabText(tab));
        base.page = page;
        for (QWidget *w : page->findChildren<QWidget *>()) {
            if (w->window() != tabs->window()) continue;   // a combo box's popup list and the like
            if (qobject_cast<QHeaderView *>(w)) continue;  // a view's header shares its model
            Entry e = base;
            e.widget = w;
            e.tooltip = shown(w->toolTip());
            if (auto *view = qobject_cast<QAbstractItemView *>(w)) {
                if (view->model()) {
                    e.viewRow = true;
                    addRows(entries, e, view->model(), QModelIndex());
                }
                continue;
            }
            if (auto *button = qobject_cast<QAbstractButton *>(w)) e.text = shown(button->text());
            else if (auto *label = qobject_cast<QLabel *>(w)) e.text = shown(label->text());
            else if (auto *group = qobject_cast<QGroupBox *>(w)) e.text = shown(group->title());
            if (!hasLetter(e.text)) continue;
            entries << e;
        }
    }
    return entries;
}

QString normalized(const QString &text)
{
    QString out = shown(text).toLower();
    out.replace(QChar(0x0451), QChar(0x0435));   // ё → е
    return out;
}

QList<Entry> find(const QList<Entry> &entries, const QString &query, int limit)
{
    const QString q = normalized(query);
    if (q.size() < 2) return {};
    const QStringList words = q.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    auto matches = [&words](const QString &text) {
        return std::all_of(words.cbegin(), words.cend(), [&text](const QString &w) { return text.contains(w); });
    };

    QList<Entry> byName, byTooltip;
    for (const Entry &e : entries) {
        if (!e.widget || (e.page && !e.widget->isVisibleTo(e.page))) continue;
        const QString name = normalized(e.text);
        if (matches(name)) byName << e;
        else if (matches(name + QLatin1Char(' ') + normalized(e.tooltip))) byTooltip << e;
    }
    QList<Entry> found = byName + byTooltip;
    if (found.size() > limit) found = found.mid(0, limit);
    return found;
}

QString label(const Entry &entry)
{
    return entry.tabTitle + QStringLiteral(" › ") + entry.text;
}

QString highlightStyle()
{
    return QStringLiteral("background-color: #fff2a8;");
}

void reveal(QTabWidget *tabs, const Entry &entry)
{
    if (!tabs || !entry.widget) return;
    tabs->setCurrentIndex(entry.tab);
    QWidget *w = entry.widget;

    // inside a scroll area: bring it into view
    for (QWidget *p = w->parentWidget(); p; p = p->parentWidget()) {
        if (auto *scroll = qobject_cast<QScrollArea *>(p)) {
            scroll->ensureWidgetVisible(w);
            break;
        }
    }

    if (entry.viewRow) {
        auto *view = qobject_cast<QAbstractItemView *>(w);
        if (!view || !view->model()) return;
        const QModelIndexList hits = view->model()->match(view->model()->index(0, 0), Qt::DisplayRole, entry.text, 1,
                                                          Qt::MatchFixedString | Qt::MatchRecursive);
        if (hits.isEmpty()) return;
        view->setCurrentIndex(hits.first());
        view->scrollTo(hits.first());
        return;
    }

    // a yellow background for two seconds, then the widget's own style back
    const QString own = w->styleSheet();
    if (!own.contains(highlightStyle())) w->setStyleSheet(own + highlightStyle());
    QPointer<QWidget> guard(w);
    QTimer::singleShot(2000, w, [guard, own]() {
        if (guard) guard->setStyleSheet(own);
    });
}

} // namespace PreferencesSearch
