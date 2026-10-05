/* Glasgow Index
 *
 * Copyright (c) 2026 The OSCAR Team
 * The Glasgow Index is by DaveSkvn (https://github.com/DaveSkvn/GlasgowIndex, GPL-3.0-or-later).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GLASGOW_INDEX_H
#define GLASGOW_INDEX_H

#include "day_scorer.h"    // TimedValue
#include "flow_analyzer.h"
#include "glasgow_counts.h"

namespace analysis {

//! The Glasgow Index as FlowLimits.js computes it, on the session's raw flow (L/min). Windows
//! the author sets in samples at 25 Hz are scaled to the recording's rate.
GlasgowResult glasgowOriginal(const QVector<FlowChunk> &chunks);

//! The Glasgow Index on our own breaths (\a breaths from segmentBreaths on \a chunks), with the
//! thresholds the author sets in L/min taken relative to the breath's peak. Breaths overlapping
//! \a blocked (events, unscoreable time) are not counted.
GlasgowResult glasgowAdapted(const QVector<FlowChunk> &chunks, const QVector<Breath> &breaths,
                             const QVector<Span> &blocked);

//! At every counted breath, the index over the counted breaths that started in the 5 minutes
//! up to it.
QVector<TimedValue> glasgowSeries(const QVector<GlasgowBreath> &breaths);

} // namespace analysis

#endif // GLASGOW_INDEX_H
