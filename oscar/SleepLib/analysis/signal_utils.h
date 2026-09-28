/* Sleep Analysis Signal Utilities Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_SIGNAL_UTILS_H
#define ANALYSIS_SIGNAL_UTILS_H

#include <QVector>
#include <cmath>
#include <limits>

namespace analysis {

//! Missing or invalid samples are NaN throughout the analysis.
constexpr float kNoData = std::numeric_limits<float>::quiet_NaN();
inline bool hasData(float v) { return !std::isnan(v); }

//! The samples of one EventList, in milliseconds since the epoch. \a end is the list's
//! last time (EventList::last()); the last sample is held until then.
struct TimedSamples {
    QVector<qint64> t;
    QVector<float> v;
    qint64 end = 0;
};

//! A signal on a regular time grid: v[i] is the value at start + i * stepMs.
struct Grid {
    qint64 start = 0;
    qint64 stepMs = 1000;
    QVector<float> v;

    int size() const { return v.size(); }
    qint64 timeAt(int i) const { return start + qint64(i) * stepMs; }
    qint64 end() const { return timeAt(v.size()); }
    //! Index of the cell containing \a t (may be out of range).
    int indexOf(qint64 t) const;
    //! Value at \a t, or NaN outside the grid.
    float at(qint64 t) const;
};

//! Resamples SpO2 or pulse lists to a 1 Hz grid (spec §3.1.2-3). Each sample holds until
//! the next sample of the same list (both "store on change" lists and fixed-rate ones);
//! the last one until the list's end, or for one second. A cell takes the value in effect
//! at its centre. Values outside [minValid, maxValid] (and so zeros, the loaders' gap
//! marker) and the time between lists are NaN.
Grid toOneHz(const QVector<TimedSamples> &lists, float minValid, float maxValid);

//! Centred median over \a window samples (odd), ignoring NaN; NaN where the input is NaN.
QVector<float> medianFilter(const QVector<float> &v, int window);

//! Centred mean over \a window samples (odd), ignoring NaN; NaN where the input is NaN.
QVector<float> movingAverage(const QVector<float> &v, int window);

//! The \a p-th percentile (0-100, nearest rank) of the values that are not NaN; NaN if none.
float percentile(QVector<float> values, double p);

//! For each i, the \a p-th percentile of v[i - back .. i - 1] (not NaN), or NaN when fewer
//! than \a minCount of those values exist.
QVector<float> trailingPercentile(const QVector<float> &v, int back, double p, int minCount);

//! Means of consecutive blocks of \a factor samples (the last block may be shorter).
QVector<float> decimateMean(const QVector<float> &v, int factor);

} // namespace analysis

#endif // ANALYSIS_SIGNAL_UTILS_H
