/* Dragging the edges of scored events and excluded stretches
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SCORINGRESIZE_H
#define SCORINGRESIZE_H

#include <QPair>
#include <QtGlobal>

#include "SleepLib/manual_scoring.h"

//! In manual scoring mode the edges of the event boxes and of the excluded stretches on the flow
//! graph can be dragged: which edge is under the mouse, and where a drag may take it.
namespace ScoringResize {

constexpr int kGrabPx = 4;       //!< how near an edge the mouse grabs it
constexpr qint64 kMinMs = 1000;  //!< the shortest event or stretch

struct Target {
    enum Kind { None, Event, Excluded };
    Kind kind = None;
    int eventIndex = -1;   //!< Event: its index in the result's events
    qint64 editId = 0;     //!< Excluded: its Exclude edit
    bool leftEdge = false;
    qint64 startMs = 0, endMs = 0;   //!< the bounds when grabbed
};

//! The edge at plot position \a x (pixels) of a plot \a plotWidth wide showing [\a minX, \a maxX] ms:
//! the nearest within kGrabPx; an event's edge only when \a boxesShown, and never of a removed event
//! or one inside an excluded stretch (they have no box); a tie goes to the event.
Target hit(const ManualScoring::Result &r, qint64 minX, qint64 maxX, int plotWidth, int x, bool boxesShown);

//! Whether \a t still names the same event or stretch in \a r (the events may be rebuilt during a drag).
bool stillTargets(const ManualScoring::Result &r, const Target &t);

//! The bounds after the grabbed edge of \a t moved to \a timeMs: kept within [\a lowMs, \a highMs]
//! and at least kMinMs long.
QPair<qint64, qint64> dragTo(const Target &t, qint64 timeMs, qint64 lowMs, qint64 highMs);

} // namespace ScoringResize

#endif // SCORINGRESIZE_H
