/* Synthetic Signals for Sleep Analysis Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_SYNTH_H
#define ANALYSIS_SYNTH_H

#include <QPair>

#include "SleepLib/analysis/flow_analyzer.h"
#include "SleepLib/analysis/signal_utils.h"

//! Builders of synthetic 1 Hz SpO2/pulse grids for the analysis tests. Values are rounded
//! to whole numbers, as oximeters report them. Seconds count from the grid start.
namespace synth {

constexpr qint64 kStart = 1700000000000LL;   // an arbitrary night, in ms

//! \a seconds samples of \a value.
analysis::Grid flat(int seconds, float value, qint64 start = kStart);
//! Sets seconds [from, to) to \a value.
void fill(analysis::Grid &g, int from, int to, float value);
//! Linear change from \a a at \a from to \a b at \a to (exclusive).
void ramp(analysis::Grid &g, int from, int to, float a, float b);
//! A dip from the level at \a at: falls by \a drop over \a fall s, holds \a hold s,
//! recovers over \a rise s.
void dip(analysis::Grid &g, int at, float drop, int fall, int hold, int rise);

//! Sinusoidal breathing at \a fs Hz with a \a period s breath (inspiration positive):
//! consecutive (seconds, amplitude) segments; the phase runs on across them. \a drift
//! adds a slow offset of that size (period 300 s), like a device's leak compensation.
analysis::FlowChunk breathing(double fs, double period, const QVector<QPair<double, double>> &segments,
                              double drift = 0, qint64 start = kStart);

} // namespace synth

#endif // ANALYSIS_SYNTH_H
