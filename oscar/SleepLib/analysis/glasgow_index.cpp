/* Glasgow Index
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * glasgowOriginal() is a port of FlowLimits.js of the Glasgow Index,
 * Copyright 2025 DaveSkvn, https://github.com/DaveSkvn/GlasgowIndex, distributed under the
 * GNU General Public License version 3 or (at your option) any later version.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "glasgow_index.h"

#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>

namespace analysis {

double GlasgowCounts::fraction(GlasgowComponent c) const
{
    if (breaths <= 0) return std::numeric_limits<double>::quiet_NaN();
    return double(flagged[c]) / breaths;
}

double GlasgowCounts::index() const
{
    if (breaths <= 0) return std::numeric_limits<double>::quiet_NaN();
    double sum = 0;
    for (int c = 0; c < GiComponentCount; ++c) {
        if (c != GiTopHeavy) sum += fraction(GlasgowComponent(c));
    }
    return sum;
}

GlasgowCounts &GlasgowCounts::operator+=(const GlasgowCounts &o)
{
    breaths += o.breaths;
    for (int c = 0; c < GiComponentCount; ++c) flagged[c] += o.flagged[c];
    return *this;
}

QString GlasgowCounts::toText() const
{
    if (breaths <= 0) return QString();
    QStringList parts;
    for (int n : flagged) parts << QString::number(n);
    return parts.join(QLatin1Char(','));
}

GlasgowCounts GlasgowCounts::fromText(int breaths, const QString &text)
{
    GlasgowCounts c;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (breaths <= 0 || parts.size() != GiComponentCount) return c;
    c.breaths = breaths;
    for (int i = 0; i < GiComponentCount; ++i) c.flagged[i] = parts[i].toInt();
    return c;
}

namespace {

// JavaScript's Math.round: halves go up.
double jsRound(double v) { return std::floor(v + 0.5); }

// A flow recorded below 20 Hz (Prisma: 10 Hz) on the author's 25 Hz grid, by cubic
// (Catmull-Rom) interpolation: a breath's peak usually falls between such samples, and where
// it does decides the spike, double peak, skew and amplitude signs. Faster recordings are left
// as they are.
// The recording's step when it is in whole L/min (Prisma), else 0.
double coarseStep(const QVector<FlowChunk> &chunks)
{
    qint64 n = 0, whole = 0;
    for (const FlowChunk &c : chunks) {
        for (float v : c.samples) {
            ++n;
            if (std::fabs(v - std::round(v)) < 1e-3f) ++whole;
        }
    }
    return n > 0 && whole >= 0.99 * n ? 1.0 : 0.0;
}

// A recording in whole steps wobbles by a step from sample to sample: a centred moving average
// over about 0.3 s takes the wobble out and keeps the shape of the breath.
FlowChunk smoothed(const FlowChunk &c)
{
    const int half = std::max(1, int(std::lround(150 / c.rateMs)));
    FlowChunk r = c;
    const int n = c.samples.size();
    for (int i = 0; i < n; ++i) {
        double sum = 0;
        int k = 0;
        for (int j = std::max(0, i - half); j <= std::min(n - 1, i + half); ++j, ++k) sum += c.samples[j];
        r.samples[i] = float(sum / k);
    }
    return r;
}

QVector<FlowChunk> onAuthorGrid(const QVector<FlowChunk> &recorded)
{
    const bool coarse = coarseStep(recorded) > 0;
    QVector<FlowChunk> out;
    for (const FlowChunk &rc : recorded) {
        const FlowChunk c = coarse && rc.samples.size() >= 3 ? smoothed(rc) : rc;
        if (c.rateMs <= 50 || c.samples.size() < 2) {
            out << c;
            continue;
        }
        FlowChunk r;
        r.start = c.start;
        r.rateMs = 40;
        const int n = c.samples.size();
        const int m = int(std::floor((n - 1) * c.rateMs / r.rateMs)) + 1;
        r.samples.reserve(m);
        auto at = [&c, n](int i) { return double(c.samples[std::clamp(i, 0, n - 1)]); };
        for (int j = 0; j < m; ++j) {
            const double x = j * r.rateMs / c.rateMs;
            const int i = int(std::floor(x));
            const double t = x - i;
            const double p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
            r.samples << float(0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                                      + (3 * p1 - p0 - 3 * p2 + p3) * t * t * t));
        }
        out << r;
    }
    return out;
}

// One inspiration as FlowLimits.js describes it (indices into the joined samples).
struct Inspiration {
    int start = 0, end = 0, midPoint = 0;
    double maxValue = 0;
    double leftPercent = 50, top90Percent = 32, midVar = 0;
    bool multiPeak = false;
    bool noExhale = false;
    bool hasPreRest = false;   // JS leaves preRest undefined for an inspiration without a minimum
    double preRest = 0;
    bool hasAmpVar = false;    // ... and ampVar / inspirPerMin for the first 5 and the last
    double ampVar = 0;
    double inspirPerMin = 0;
};

} // namespace

// FlowLimits.js: findMins, findInspirations, calcCycleBasedIndicators, inspirationAmplitude,
// prepIndices. Constants in samples are the author's at 25 Hz, scaled by n().
GlasgowResult glasgowOriginal(const QVector<FlowChunk> &recorded)
{
    GlasgowResult result;
    const QVector<FlowChunk> chunks = onAuthorGrid(recorded);
    QVector<double> y;
    QVector<qint64> t;
    double rateMs = 0;
    for (const FlowChunk &c : chunks) {   // formDataArray: the flow signals one after another
        if (c.samples.isEmpty() || c.rateMs <= 0) continue;
        if (rateMs <= 0) rateMs = c.rateMs;
        for (int i = 0; i < c.samples.size(); ++i) {
            y << c.samples[i];
            t << c.start + qint64(std::llround(i * c.rateMs));
        }
    }
    if (rateMs <= 0) return result;
    const double fs = 1000.0 / rateMs;
    auto n = [fs](int n25) { return std::max(1, int(std::lround(n25 * fs / 25.0))); };
    const int minWindow = n(25), minInspiration = n(8), shortInspiration = n(12);
    const int extrapolation = n(25), pause = n(10);
    const double greyLower = -10, greyUpper = 5, minPeakBump = 1;
    const int len = y.size();

    // findMins: the lowest sample within a second either side, below the grey zone
    QVector<char> isMin(len, 0);
    for (int ptr = minWindow; ptr < len - minWindow; ++ptr) {
        bool minDetected = true;
        for (int w = ptr - minWindow; w < ptr + minWindow - 1; ++w) {
            if (y[w] < y[ptr]) { minDetected = false; break; }
        }
        isMin[ptr] = minDetected && y[ptr] < greyLower;
    }
    for (int ptr = std::max(0, len - minWindow - 1); ptr < len - 1; ++ptr) isMin[ptr] = 0;

    // findInspirations
    QVector<Inspiration> insp;
    int ignoreUntil = 0;
    for (int i = 0; i < len - 1; ++i) {
        if (i < ignoreUntil || y[i] <= greyUpper || i == 0) continue;
        if (y[i - 1] > y[i] || y[i] < y[i + 1]) continue;
        int start = -1, end = -1;
        for (int d = i; d > 0; --d) {
            if (y[d] > y[i]) break;
            if (y[d] <= greyUpper) { start = d; break; }
        }
        if (start < 0) continue;
        for (int u = i; u < len - 1; ++u) {
            if (y[u] > y[i]) break;
            if (y[u] <= greyUpper) { end = u; break; }
        }
        if (end < 0 || end - start < minInspiration) continue;

        Inspiration in;
        in.start = start;
        in.end = end;
        in.maxValue = y[i];
        in.midPoint = start + int(jsRound((end - start) / 2.0));
        double leftVol = 0, rightVol = 0;
        int top90 = 0;
        const double threshold90 = in.maxValue * 0.9;
        bool firstPeakFound = false, lookingForNextPeak = false;
        double lastMax = 0, lowestPostFirstPeak = 0;
        for (int ptr = start; ptr < end; ++ptr) {
            if (ptr < in.midPoint) leftVol += y[ptr];
            else if (ptr > in.midPoint) rightVol += y[ptr];
            if (y[ptr] > threshold90) ++top90;
            if (!firstPeakFound) {
                if (y[ptr] > lastMax) lastMax = y[ptr];
                else if (y[ptr] < lastMax) firstPeakFound = true;
            } else if (!lookingForNextPeak && (lastMax - y[ptr]) > minPeakBump) {
                lookingForNextPeak = true;
                lowestPostFirstPeak = y[ptr];
            }
            if (lookingForNextPeak && !in.multiPeak) {   // a separate "if" in the original
                if (y[ptr] < lowestPostFirstPeak) lowestPostFirstPeak = y[ptr];
                else if (y[ptr] > lowestPostFirstPeak + minPeakBump) in.multiPeak = true;
            }
        }
        if (end - start > shortInspiration) {
            in.leftPercent = jsRound(10000 * leftVol / (leftVol + rightVol)) / 100;
            in.top90Percent = jsRound(10000.0 * top90 / (end - start)) / 100;
        }
        const int varStart = int(jsRound(in.midPoint - 0.25 * (end - start)));
        const int varEnd = int(jsRound(in.midPoint + 0.25 * (end - start)));
        const double half = 0.5 * (end - start);
        double midSum = 0;
        for (int ptr = varStart; ptr < varEnd; ++ptr) midSum += y[ptr];
        const double midMean = midSum / half;
        double midVar = 0;
        for (int ptr = varStart; ptr < varEnd; ++ptr) midVar += (midMean - y[ptr]) * (midMean - y[ptr]);
        in.midVar = jsRound(100 * midVar / half) / 100;
        insp << in;
        ignoreUntil = end;
    }

    // calcCycleBasedIndicators: link each inspiration to the expiration peak before it
    QVector<int> mins;
    for (int i = 0; i < len - 1; ++i) {
        if (isMin[i]) mins << i;
    }
    int next = 0;
    for (int i = 0; i < mins.size() - 1; ++i) {
        const int indexOfMin = mins[i];
        if (next >= insp.size()) break;
        int emergencyBreak = 10;
        do {
            if (emergencyBreak-- <= 0) break;
            Inspiration &in = insp[next];
            if (in.start < indexOfMin) {
                in.noExhale = true;   // two inspirations for one expiration
                ++next;
            } else if (in.start > mins[i + 1]) {
                break;
            } else if (in.start > indexOfMin) {
                in.noExhale = false;
                const double minValue = y[indexOfMin];
                const double oneSecondLater = indexOfMin + extrapolation < len ? y[indexOfMin + extrapolation] : 0;
                in.hasPreRest = true;
                if (oneSecondLater < 0) {
                    // in double, as in JS: a flat expiration gives an infinite rest, never "no pause"
                    const double intersection = indexOfMin + jsRound(extrapolation * minValue / (minValue - oneSecondLater));
                    in.preRest = in.start - intersection;
                } else {
                    in.preRest = -10;
                }
                ++next;
                break;
            }
        } while (next < insp.size() - 1);
    }

    // inspirationAmplitude
    const int ampWindow = 5;
    for (int i = ampWindow; i < insp.size() - 1; ++i) {
        double mean = 0;
        for (int k = 0; k < ampWindow; ++k) mean += insp[i - k].maxValue;
        mean /= ampWindow;
        double var = 0;
        for (int k = 0; k < ampWindow; ++k) var += (insp[i - k].maxValue - mean) * (insp[i - k].maxValue - mean);
        insp[i].hasAmpVar = true;
        insp[i].ampVar = jsRound(100 * var / ampWindow) / 100;
        const int samples = insp[i].start - insp[i - ampWindow].start;
        insp[i].inspirPerMin = jsRound(ampWindow * 60 * 1000 / (samples * rateMs));
    }

    // prepIndices
    for (const Inspiration &in : insp) {
        GlasgowBreath b;
        b.start = t[in.start];
        b.flags[GiSkew] = in.leftPercent < 45 || in.leftPercent > 55;
        b.flags[GiTopHeavy] = in.top90Percent > 40;
        b.flags[GiFlatTop] = in.midVar < 0.75;
        b.flags[GiSpike] = in.top90Percent < 20;
        b.flags[GiMultiPeak] = in.multiPeak;
        b.flags[GiNoPause] = in.hasPreRest && in.preRest < pause;
        b.flags[GiInspirRate] = in.hasAmpVar && in.inspirPerMin > 20;
        b.flags[GiMultiBreath] = in.noExhale;
        b.flags[GiAmpVar] = in.hasAmpVar && in.ampVar > 4;
        for (int k = 0; k < GiComponentCount; ++k) result.counts.flagged[k] += b.flags[k];
        result.breaths << b;
    }
    result.counts.breaths = insp.size();
    return result;
}

namespace {

// The samples of one breath's time range, from the chunk that holds its start.
struct Samples {
    const FlowChunk *chunk = nullptr;
    int from = 0, to = 0;   // [from, to)
    float at(int i) const { return chunk->samples[i]; }
    int size() const { return to - from; }
};

Samples samplesOf(const QVector<FlowChunk> &chunks, qint64 start, qint64 end)
{
    Samples s;
    for (const FlowChunk &c : chunks) {
        if (c.samples.isEmpty() || c.rateMs <= 0) continue;
        const qint64 cEnd = c.start + qint64(c.samples.size() * c.rateMs);
        if (start < c.start || start >= cEnd) continue;
        s.chunk = &c;
        s.from = int(std::lround((start - c.start) / c.rateMs));
        s.to = std::min<int>(c.samples.size(), int(std::lround((end - c.start) / c.rateMs)));
        s.from = std::min(s.from, s.to);
        return s;
    }
    return s;
}

bool overlaps(const QVector<Span> &spans, qint64 start, qint64 end)
{
    for (const Span &sp : spans) {
        if (start < sp.end && end > sp.start) return true;
    }
    return false;
}

double median(QVector<double> v)
{
    if (v.isEmpty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

// The same signs on our breaths. Signs that do not depend on how strong the flow is keep the
// author's rule in seconds; the L/min thresholds are taken relative to the peak P, chosen to
// give the original's answer at P = 30 L/min.
GlasgowResult glasgowAdapted(const QVector<FlowChunk> &recorded, const QVector<Breath> &breaths,
                             const QVector<Span> &blocked)
{
    const QVector<FlowChunk> chunks = onAuthorGrid(recorded);
    const double step = coarseStep(recorded);   // differences below two steps of the recording are rounding
    GlasgowResult result;
    const int n = breaths.size();
    QVector<double> peak(n, 0);
    QVector<Samples> insp(n), exp(n);
    for (int k = 0; k < n; ++k) {
        insp[k] = samplesOf(chunks, breaths[k].start, breaths[k].inspEnd);
        exp[k] = samplesOf(chunks, breaths[k].inspEnd, breaths[k].end);
        for (int i = insp[k].from; insp[k].chunk && i < insp[k].to; ++i) peak[k] = std::max(peak[k], double(insp[k].at(i)));
    }

    for (int k = 0; k < n; ++k) {
        const Breath &br = breaths[k];
        GlasgowBreath b;
        b.start = br.start;
        Samples s = insp[k];
        const double P = peak[k];
        if (s.chunk && P > 0) {
            // the part of the inspiration above the grey zone (the original's 5 L/min, as 5/30 of
            // the peak), as findInspirations takes it: from the last sample at or below it before
            // the peak to the first one after
            int pk = s.from;
            for (int i = s.from; i < s.to; ++i) pk = s.at(i) > s.at(pk) ? i : pk;
            const double grey = 5.0 / 30 * P;
            int from = pk, to = pk;
            while (from > s.from && s.at(from) > grey) --from;
            const int last = std::min<int>(s.to, s.chunk->samples.size() - 1);   // may reach the first expiratory sample
            while (to < last && s.at(to) > grey) ++to;
            s.from = from;
            s.to = to;
        }
        b.counted = s.chunk && s.size() >= 2 && P > 0 && !overlaps(blocked, br.start, br.end);
        if (!b.counted) {
            result.breaths << b;
            continue;
        }
        const double rateMs = s.chunk->rateMs;
        const int len = s.size();
        const int mid = s.from + int(jsRound(len / 2.0));

        // Skew, Spike, Top Heavy: shares of the inspiration, benign for one of 0.48 s or less
        double left = 0, right = 0;
        int top90 = 0;
        for (int i = s.from; i < s.to; ++i) {
            if (i < mid) left += s.at(i);
            else if (i > mid) right += s.at(i);
            if (s.at(i) > 0.9 * P) ++top90;
        }
        double leftPercent = 50, top90Percent = 32;
        if (len * rateMs > 480) {
            leftPercent = 100 * left / (left + right);
            top90Percent = 100.0 * top90 / len;
        }
        b.flags[GiSkew] = leftPercent < 45 || leftPercent > 55;
        b.flags[GiSpike] = top90Percent < 20;
        b.flags[GiTopHeavy] = top90Percent > 40;

        // Flat Top: variance of the middle half, relative to P²
        const int varStart = int(jsRound(mid - 0.25 * len)), varEnd = int(jsRound(mid + 0.25 * len));
        const double half = 0.5 * len;
        double midSum = 0, midVar = 0;
        for (int i = varStart; i < varEnd; ++i) midSum += s.at(i);
        const double midMean = midSum / half;
        for (int i = varStart; i < varEnd; ++i) midVar += (midMean - s.at(i)) * (midMean - s.at(i));
        b.flags[GiFlatTop] = midVar / half / (P * P) < 0.75 / 900;

        // Multi-Peak: the original's state machine, the step 3.3 % of P
        const double bump = std::max(0.033 * P, 2 * step);
        bool firstPeakFound = false, lookingForNextPeak = false, multiPeak = false;
        double lastMax = 0, lowest = 0;
        for (int i = s.from; i < s.to; ++i) {
            const double v = s.at(i);
            if (!firstPeakFound) {
                if (v > lastMax) lastMax = v;
                else if (v < lastMax) firstPeakFound = true;
            } else if (!lookingForNextPeak && lastMax - v > bump) {
                lookingForNextPeak = true;
                lowest = v;
            }
            if (lookingForNextPeak && !multiPeak) {
                if (v < lowest) lowest = v;
                else if (v > lowest + bump) multiPeak = true;
            }
        }
        b.flags[GiMultiPeak] = multiPeak;

        if (k > 0) {
            // the previous breath's expiration: its peak, and whether there was a real one
            const Samples &e = exp[k - 1];
            int minAt = -1;
            for (int i = e.from; e.chunk && i < e.to; ++i) {
                if (minAt < 0 || e.at(i) < e.at(minAt)) minAt = i;
            }
            QVector<double> pefs;
            for (int j = std::max(0, k - 5); j < k; ++j) pefs << std::fabs(breaths[j].pef);
            const double typicalPef = median(pefs);
            const double m = minAt >= 0 ? e.at(minAt) : 0;
            b.flags[GiMultiBreath] = m > -0.10 * typicalPef;
            if (!b.flags[GiMultiBreath] && minAt >= 0) {
                // No Pause: where the expiration comes back to zero, by extrapolating it from
                // its peak over one second as the original does, or, when it is already back
                // by then, the first sample at or above zero (a resting flow hovers around it,
                // so its sign alone says nothing); to the start of the inspiration proper
                const qint64 tm = e.chunk->start + qint64(std::llround(minAt * e.chunk->rateMs));
                const int later = minAt + int(std::lround(1000 / e.chunk->rateMs));
                const double y1 = later < e.chunk->samples.size() ? e.chunk->samples[later] : 0;
                const double inspStart = s.chunk->start + s.from * s.chunk->rateMs;
                double intersection = tm;
                if (y1 < 0) {
                    intersection = tm + 1000.0 * m / (m - y1);
                } else {
                    int i = minAt + 1;
                    const int stop = std::min<int>(later, e.chunk->samples.size() - 1);
                    while (i < stop && e.chunk->samples[i] < 0) ++i;
                    intersection = e.chunk->start + i * e.chunk->rateMs;
                }
                b.flags[GiNoPause] = inspStart - intersection < 400;
            }
        }
        if (k >= 5) b.flags[GiInspirRate] = 5 * 60000.0 / (br.start - breaths[k - 5].start) > 20;
        if (k >= 4) {
            double mean = 0, var = 0;
            for (int j = k - 4; j <= k; ++j) mean += peak[j];
            mean /= 5;
            for (int j = k - 4; j <= k; ++j) var += (peak[j] - mean) * (peak[j] - mean);
            b.flags[GiAmpVar] = mean > 0 && var / 5 / (mean * mean) > 4.0 / 900 && std::sqrt(var / 5) > step;
        }

        ++result.counts.breaths;
        for (int c = 0; c < GiComponentCount; ++c) result.counts.flagged[c] += b.flags[c];
        result.breaths << b;
    }
    return result;
}

QVector<TimedValue> glasgowSeries(const QVector<GlasgowBreath> &breaths)
{
    QVector<TimedValue> out;
    QVector<const GlasgowBreath *> counted;
    for (const GlasgowBreath &b : breaths) {
        if (b.counted) counted << &b;
    }
    GlasgowCounts window;
    int first = 0;
    for (int i = 0; i < counted.size(); ++i) {
        const GlasgowBreath &b = *counted[i];
        ++window.breaths;
        for (int c = 0; c < GiComponentCount; ++c) window.flagged[c] += b.flags[c];
        while (counted[first]->start <= b.start - 300000) {   // keep (t - 300 s, t]
            --window.breaths;
            for (int c = 0; c < GiComponentCount; ++c) window.flagged[c] -= counted[first]->flags[c];
            ++first;
        }
        // from a minute of breathing on: over the first few breaths the index jumps about
        if (b.start - counted[first]->start >= 60000) out << TimedValue { b.start, float(window.index()) };
    }
    return out;
}

} // namespace analysis
