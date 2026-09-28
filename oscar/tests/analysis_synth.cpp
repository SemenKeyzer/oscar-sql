/* Synthetic Signals for Sleep Analysis Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysis_synth.h"

#include <cmath>

namespace synth {

using analysis::Grid;

constexpr double kPi = 3.14159265358979323846;

Grid flat(int seconds, float value, qint64 start)
{
    Grid g;
    g.start = start;
    g.stepMs = 1000;
    g.v.fill(value, seconds);
    return g;
}

void fill(Grid &g, int from, int to, float value)
{
    for (int i = qMax(0, from); i < qMin(g.size(), to); ++i) g.v[i] = value;
}

void ramp(Grid &g, int from, int to, float a, float b)
{
    const int n = to - from;
    for (int i = 0; i < n; ++i) {
        const int k = from + i;
        if (k < 0 || k >= g.size()) continue;
        g.v[k] = std::round(a + (b - a) * float(i) / float(qMax(1, n - 1)));
    }
}

void dip(Grid &g, int at, float drop, int fall, int hold, int rise)
{
    const float level = g.v[at];
    ramp(g, at, at + fall + 1, level, level - drop);
    fill(g, at + fall + 1, at + fall + 1 + hold, level - drop);
    ramp(g, at + fall + 1 + hold, at + fall + 1 + hold + rise + 1, level - drop, level);
}

analysis::FlowChunk breathing(double fs, double period, const QVector<QPair<double, double>> &segments,
                              double drift, qint64 start)
{
    analysis::FlowChunk c;
    c.start = start;
    c.rateMs = 1000.0 / fs;
    double t0 = 0;
    for (const auto &seg : segments) {
        const int n = int(std::lround(seg.first * fs));
        for (int i = 0; i < n; ++i) {
            const double t = t0 + i / fs;
            const double flow = seg.second * std::sin(2 * kPi * t / period) + drift * std::sin(2 * kPi * t / 300.0);
            c.samples.append(float(flow));
        }
        t0 += n / fs;
    }
    return c;
}

} // namespace synth
