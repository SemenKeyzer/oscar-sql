/* Menus of the manual scoring mode
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SCORINGMENUS_H
#define SCORINGMENUS_H

#include <QCoreApplication>

#include "SleepLib/manual_scoring.h"

class Day;
class QAction;
class QMenu;
class QTreeWidgetItem;
class QWidget;

//! The menus of the Daily manual scoring mode. Each action's data() is a QVariantMap with
//! "action" (add / exclude / remove / retype / undo) and, as fits it, "channel", "newChannel",
//! "timeMs" (the event's end) and "editId".
class ScoringMenus
{
    Q_DECLARE_TR_FUNCTIONS(ScoringMenus)
  public:
    //! After a stretch of \a durationMs was selected on the flow graph.
    static QMenu *forRange(qint64 durationMs, QWidget *parent);
    //! For an event right-clicked on the graphs.
    //! \a countOfType: how many events of its type the night counts; with more than one, the menu
    //! also offers to change or remove all of them.
    static QMenu *forEvent(const ManualScoring::EffectiveEvent &event, QWidget *parent, int countOfType = 0);
    //! For an excluded stretch right-clicked on the graphs.
    static QMenu *forExcluded(qint64 editId, QWidget *parent);
    //! The name of a scored channel in the menus: "Obstructive apnea", …
    static QString typeName(ChannelID channel);
    //! "excluded 17 s" under two minutes, "excluded 14 min" from two minutes on.
    static QString excludedText(qint64 ms);
    //! The item of \a menu whose letter key is \a text (O C A H X R T U; the same keys on the
    //! Russian layout too: Щ С Ф Р Ч К Е Г); null for none.
    static QAction *actionForKey(QMenu *menu, const QString &text);

    //! One edit of a night as listed (Events tab, sidebar): where it is on the graphs and what it did.
    struct EditRow {
        qint64 editId;
        qint64 timeMs;   //!< graph time: an event's end, a stretch's start
        QString text;    //!< "removed: Obstructive apnea", "excluded 20 min", …
    };
    //! The edits of \a day in time order; a stretch kept with several sessions is listed once.
    static QList<EditRow> editRows(Day *day);
    //! The Events tab node "Manual scoring (N)" with one child per row (time in Qt::UserRole, the
    //! edit in kEditIdRole); none for no rows.
    static QTreeWidgetItem *treeNode(const QList<EditRow> &rows);
    static constexpr int kTreeNodeType = 1000 + 10;   //!< QTreeWidgetItem::UserType + 10
    static constexpr int kEditIdRole = Qt::UserRole + 1;
};

#endif // SCORINGMENUS_H
