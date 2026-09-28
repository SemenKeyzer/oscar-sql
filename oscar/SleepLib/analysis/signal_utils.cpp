/* Sleep Analysis Signal Utilities
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "signal_utils.h"

#include <algorithm>

namespace analysis {

int Grid::indexOf(qint64 t) const
{
    if (stepMs <= 0) return -1;
    const qint64 d = t - start;
    // floor division, also for times before the start
    return int(d >= 0 ? d / stepMs : -((-d + stepMs - 1) / stepMs));
}

float Grid::at(qint64 t) const
{
    const int i = indexOf(t);
    return (i >= 0 && i < v.size()) ? v[i] : kNoData;
}

Grid toOneHz(const QVector<TimedSamples> &lists, float minValid, float maxValid)
{
    Grid grid;
    qint64 first = std::numeric_limits<qint64>::max();
    qint64 last = std::numeric_limits<qint64>::min();
    for (const TimedSamples &list : lists) {
        if (list.t.isEmpty()) continue;
        first = qMin(first, list.t.first());
        last = qMax(last, qMax(list.end, list.t.last() + 1000));
    }
    if (first > last) return grid;

    grid.start = (first / 1000) * 1000;
    const qint64 end = ((last + 999) / 1000) * 1000;
    grid.v.fill(kNoData, int((end - grid.start) / 1000));

    for (const TimedSamples &list : lists) {
        const int n = qMin(list.t.size(), list.v.size());
        for (int i = 0; i < n; ++i) {
            const qint64 from = list.t[i];
            qint64 to = (i + 1 < n) ? list.t[i + 1] : list.end;
            if (to <= from) to = from + 1000;   // last sample, list end unknown
            const float value = list.v[i];
            const bool valid = hasData(value) && value >= minValid && value <= maxValid;

            // cells whose centre (start + k*1000 + 500) lies in [from, to)
            qint64 k0 = (from - grid.start - 500 + 999) / 1000;
            if (from - grid.start - 500 < 0) k0 = 0;
            for (qint64 k = k0; k < grid.v.size(); ++k) {
                const qint64 centre = grid.start + k * 1000 + 500;
                if (centre >= to) break;
                if (centre >= from) grid.v[int(k)] = valid ? value : kNoData;
            }
        }
    }
    return grid;
}

QVector<float> medianFilter(const QVector<float> &v, int window)
{
    const int half = qMax(0, window / 2);
    QVector<float> out(v.size(), kNoData);
    QVector<float> buf;
    buf.reserve(2 * half + 1);
    for (int i = 0; i < v.size(); ++i) {
        if (!hasData(v[i])) continue;
        buf.clear();
        for (int j = qMax(0, i - half); j <= qMin(int(v.size()) - 1, i + half); ++j) {
            if (hasData(v[j])) buf.append(v[j]);
        }
        auto mid = buf.begin() + buf.size() / 2;
        std::nth_element(buf.begin(), mid, buf.end());
        out[i] = *mid;
    }
    return out;
}

QVector<float> movingAverage(const QVector<float> &v, int window)
{
    const int half = qMax(0, window / 2);
    const int n = v.size();
    // prefix sums of the valid values and of their count
    QVector<double> sum(n + 1, 0.0);
    QVector<int> count(n + 1, 0);
    for (int i = 0; i < n; ++i) {
        const bool ok = hasData(v[i]);
        sum[i + 1] = sum[i] + (ok ? v[i] : 0.0);
        count[i + 1] = count[i] + (ok ? 1 : 0);
    }
    QVector<float> out(n, kNoData);
    for (int i = 0; i < n; ++i) {
        if (!hasData(v[i])) continue;
        const int a = qMax(0, i - half);
        const int b = qMin(n, i + half + 1);
        out[i] = float((sum[b] - sum[a]) / (count[b] - count[a]));
    }
    return out;
}

float percentile(QVector<float> values, double p)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](float x) { return !hasData(x); }),
                 values.end());
    if (values.isEmpty()) return kNoData;
    const double clamped = qBound(0.0, p, 100.0);
    const int idx = int(std::lround(clamped / 100.0 * (values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + idx, values.end());
    return values[idx];
}

QVector<float> trailingPercentile(const QVector<float> &v, int back, double p, int minCount)
{
    QVector<float> out(v.size(), kNoData);
    QVector<float> buf;
    buf.reserve(back);
    for (int i = 0; i < v.size(); ++i) {
        buf.clear();
        for (int j = qMax(0, i - back); j < i; ++j) {
            if (hasData(v[j])) buf.append(v[j]);
        }
        if (buf.size() >= qMax(1, minCount)) out[i] = percentile(buf, p);
    }
    return out;
}

QVector<float> decimateMean(const QVector<float> &v, int factor)
{
    if (factor <= 1) return v;
    QVector<float> out;
    out.reserve(v.size() / factor + 1);
    for (int i = 0; i < v.size(); i += factor) {
        const int e = qMin(int(v.size()), i + factor);
        double s = 0;
        int c = 0;
        for (int j = i; j < e; ++j) {
            if (hasData(v[j])) { s += v[j]; ++c; }
        }
        out.append(c ? float(s / c) : kNoData);
    }
    return out;
}

} // namespace analysis
