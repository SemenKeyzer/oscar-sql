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

#include "flow_analyzer.h"
#include "glasgow_counts.h"

namespace analysis {

//! The Glasgow Index as FlowLimits.js computes it, on the session's raw flow (L/min). Windows
//! the author sets in samples at 25 Hz are scaled to the recording's rate.
GlasgowResult glasgowOriginal(const QVector<FlowChunk> &chunks);

} // namespace analysis

#endif // GLASGOW_INDEX_H
