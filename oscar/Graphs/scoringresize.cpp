/* Dragging the edges of scored events and excluded stretches
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "Graphs/scoringresize.h"

#include <cmath>

namespace ScoringResize {

Target hit(const ManualScoring::Result &r, qint64 minX, qint64 maxX, int plotWidth, int x, bool boxesShown)
{
    Target best;
    if (maxX <= minX || plotWidth <= 0) return best;
    const double pxPerMs = double(plotWidth) / double(maxX - minX);
    double bestGap = kGrabPx + 0.5;   // within kGrabPx whole pixels
    auto consider = [&](Target t, qint64 edgeMs) {
        const double gap = std::fabs((edgeMs - minX) * pxPerMs - x);
        // strictly nearer wins; the events come first, so a tie keeps the event
        if (gap < bestGap) {
            bestGap = gap;
            best = t;
        }
    };
    if (boxesShown) {
        for (int i = 0; i < r.events.size(); ++i) {
            const ManualScoring::EffectiveEvent &e = r.events.at(i);
            // inside a stretch only when dragged there: it keeps its box, so it can come back out
            if (e.origin == ManualScoring::Origin::Removed || (e.excluded && e.resizeEditId == 0)) continue;
            Target t;
            t.kind = Target::Event;
            t.eventIndex = i;
            t.startMs = e.endMs - qint64(std::llround(e.durationSec * 1000));
            t.endMs = e.endMs;
            t.leftEdge = true;
            consider(t, t.startMs);
            t.leftEdge = false;
            consider(t, t.endMs);
        }
    }
    for (const ManualScoring::ExcludeEdit &x2 : r.excludeEdits) {
        Target t;
        t.kind = Target::Excluded;
        t.editId = x2.editId;
        t.startMs = x2.startMs;
        t.endMs = x2.endMs;
        t.leftEdge = true;
        consider(t, t.startMs);
        t.leftEdge = false;
        consider(t, t.endMs);
    }
    return best;
}

bool stillTargets(const ManualScoring::Result &r, const Target &t)
{
    if (t.kind == Target::Event) {
        if (t.eventIndex < 0 || t.eventIndex >= r.events.size()) return false;
        const ManualScoring::EffectiveEvent &e = r.events.at(t.eventIndex);
        return e.endMs == t.endMs && e.endMs - qint64(std::llround(e.durationSec * 1000)) == t.startMs;
    }
    if (t.kind == Target::Excluded) {
        for (const ManualScoring::ExcludeEdit &x : r.excludeEdits) {
            if (x.editId == t.editId) return x.startMs == t.startMs && x.endMs == t.endMs;
        }
    }
    return false;
}

QPair<qint64, qint64> dragTo(const Target &t, qint64 timeMs, qint64 lowMs, qint64 highMs)
{
    qint64 start = t.startMs, end = t.endMs;
    if (t.leftEdge) start = qBound(lowMs, qMin(timeMs, end - kMinMs), end - kMinMs);
    else end = qBound(start + kMinMs, qMax(timeMs, start + kMinMs), highMs);
    return { start, end };
}

} // namespace ScoringResize
