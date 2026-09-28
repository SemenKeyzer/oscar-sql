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

//! Inspiratory shapes on tau in [0, 1] with a peak of 1.
double sineShape(double tau);    //!< normal
double flatShape(double tau);    //!< flat top: a sine clipped at 70 %, rescaled
double mShape(double tau);       //!< a dip in the middle
double chairShape(double tau);   //!< early peak, then a plateau at 70 %

//! One synthetic breath: \a seconds long, inspiration shaped by \a shape over the first
//! half, a sine expiration over the second; flow scaled by \a amplitude.
struct SynthBreath {
    double seconds = 4;
    double amplitude = 1;
    double (*shape)(double) = sineShape;
};

//! The flow of \a breaths one after another at \a fs Hz.
analysis::FlowChunk breathSequence(double fs, const QVector<SynthBreath> &breaths, qint64 start = kStart);
//! \a count identical breaths.
QVector<SynthBreath> repeat(int count, SynthBreath breath);
//! Adds a sine of \a hz and \a amplitude to the flow between the two times (s from start).
void addOscillation(analysis::FlowChunk &chunk, double from, double to, double hz, double amplitude);

} // namespace synth

#endif // ANALYSIS_SYNTH_H
