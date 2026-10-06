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

class QMenu;
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
    static QMenu *forEvent(const ManualScoring::EffectiveEvent &event, QWidget *parent);
    //! For an excluded stretch right-clicked on the graphs.
    static QMenu *forExcluded(qint64 editId, QWidget *parent);
    //! The name of a scored channel in the menus: "Obstructive apnea", …
    static QString typeName(ChannelID channel);
};

#endif // SCORINGMENUS_H
