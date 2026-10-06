/* What a mouse gesture means in manual scoring mode
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SCORINGGESTURE_H
#define SCORINGGESTURE_H

#include <QPair>
#include <QString>
#include <Qt>

//! In manual scoring mode a left drag on the flow graph selects a stretch to score (instead of
//! zooming), and a right click on the flow or event flags graph asks for the event's menu
//! (instead of zooming out). Everything else keeps its usual meaning.
namespace ScoringGesture {

enum class Action { None, SelectRange, ContextMenu };

Action actionFor(bool scoringMode, const QString &graphName, Qt::MouseButton button, bool dragged);

//! The time range between two x positions in a plot \a plotWidth pixels wide showing
//! [\a minX, \a maxX] ms; positions outside the plot are clipped to it.
QPair<qint64, qint64> rangeFor(qint64 minX, qint64 maxX, int plotWidth, int x1, int x2);

} // namespace ScoringGesture

#endif // SCORINGGESTURE_H
