/* Sleep Analysis Flow Analyzer
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "flow_analyzer.h"

#include <algorithm>

namespace analysis {

namespace {

constexpr double kMinHz = 4;          // below this the flow is not analysed at all
constexpr double kFlHz = 10;          // flow limitation and oscillations need this
constexpr double kTargetHz = 25;      // faster recordings are decimated to about this
constexpr double kMaxBreathSec = 15;
constexpr double kMinBreathSec = 1;
constexpr double kPauseSec = 3;       // a longer pause after expiration ends the breath there

// One chunk ready for analysis: offset removed (x) and smoothed for segmentation (xs).
struct Proc {
    qint64 start = 0;
    double fs = 0;
    QVector<float> x;
    QVector<float> xs;

    qint64 timeAt(int j) const { return start + qint64(std::llround(j * 1000.0 / fs)); }
    qint64 end() const { return timeAt(x.size()); }
    int indexAt(qint64 t) const { return int(std::floor(double(t - start) * fs / 1000.0)); }
};

int oddWindow(double samples) { return qMax(1, 2 * int(std::lround(samples / 2)) + 1); }

Proc prepare(const FlowChunk &chunk)
{
    Proc p;
    p.start = chunk.start;
    const double fsIn = 1000.0 / chunk.rateMs;
    const int factor = qMax(1, int(std::floor(fsIn / kTargetHz + 1e-9)));
    p.fs = fsIn / factor;
    QVector<float> raw = decimateMean(chunk.samples, factor);
    for (float &v : raw) {
        if (!hasData(v)) v = 0;   // a dropped sample reads as no flow
    }
    // The device's leak compensation drifts; remove it with a 20 s moving average.
    const QVector<float> offset = movingAverage(raw, oddWindow(20 * p.fs));
    p.x.resize(raw.size());
    for (int i = 0; i < raw.size(); ++i) p.x[i] = raw[i] - offset[i];
    p.xs = movingAverage(p.x, oddWindow(0.2 * p.fs));
    return p;
}

// Breaths of one chunk: zero crossings with hysteresis (spec §3.3.2).
void segmentBreaths(const Proc &c, QVector<Breath> &out)
{
    const QVector<float> &xs = c.xs;
    const int n = xs.size();
    if (n == 0) return;
    const int stepN = qMax(1, int(std::lround(5 * c.fs)));
    const int winN = qMax(1, int(std::lround(60 * c.fs)));

    // hysteresis: 10 % of the 90th percentile of |flow| over the last 60 s, every 5 s
    QVector<float> hBlock((n + stepN - 1) / stepN, 0);
    QVector<float> buf;
    for (int b = 0; b < hBlock.size(); ++b) {
        int from = b * stepN - winN, to = b * stepN;
        if (from < 0) { from = 0; to = qMin(n, winN); }
        const int stride = qMax(1, (to - from) / 600);
        buf.clear();
        for (int j = from; j < to; j += stride) buf.append(std::fabs(xs[j]));
        const float p90 = percentile(buf, 90);
        hBlock[b] = hasData(p90) ? 0.1f * p90 : 0;
    }

    enum { Unknown, Insp, Exp } state = Unknown;
    int lastNonPos = -1, lastNonNeg = -1;
    QVector<int> rises, falls, returns;
    bool awaitingReturn = false;
    for (int j = 0; j < n; ++j) {
        const float h = hBlock[j / stepN];
        const float v = xs[j];
        if (v <= 0) lastNonPos = j;
        if (v >= 0) lastNonNeg = j;
        if (h <= 0) continue;
        if (v > h && state != Insp) {
            rises.append(qMax(0, lastNonPos + 1));
            falls.append(-1);
            returns.append(-1);
            state = Insp;
            awaitingReturn = false;
        } else if (v < -h && state == Insp) {
            falls.last() = qMax(rises.last(), lastNonNeg + 1);
            state = Exp;
            awaitingReturn = true;
        } else if (v < -h && state == Unknown) {
            state = Exp;
        } else if (state == Exp && awaitingReturn && v >= -h) {
            returns.last() = j;
            awaitingReturn = false;
        }
    }

    for (int k = 0; k < rises.size(); ++k) {
        const int r = rises[k], f = falls[k];
        if (f < 0) continue;
        const int next = (k + 1 < rises.size()) ? rises[k + 1] : -1;
        int e = next;
        // Before a pause (an apnea, or the end of the data) the breath ends with its
        // expiration, not with the next inspiration.
        if (next < 0 || (returns[k] >= 0 && next - returns[k] > kPauseSec * c.fs)) e = returns[k];
        if (e <= f) continue;
        const double dur = (e - r) / c.fs;
        if (dur < kMinBreathSec || dur > kMaxBreathSec) continue;

        Breath b;
        b.start = c.timeAt(r);
        b.inspEnd = c.timeAt(f);
        b.end = c.timeAt(e);
        b.pif = *std::max_element(xs.begin() + r, xs.begin() + f);
        b.pef = *std::min_element(xs.begin() + f, xs.begin() + e);
        double vi = 0;
        for (int j = r; j < f; ++j) vi += qMax(0.0f, c.x[j]);
        b.vi = float(vi / c.fs);
        b.amplitude = b.pif - b.pef;
        out.append(b);
    }
}

// The chunk containing time t, or -1.
int chunkAt(const QVector<Proc> &chunks, qint64 t)
{
    for (int i = 0; i < chunks.size(); ++i) {
        if (t >= chunks[i].start && t < chunks[i].end()) return i;
    }
    return -1;
}

// Peak-to-peak of the smoothed flow over [from, to), or NaN when less than half of it
// is recorded.
float peakToPeak(const Proc &c, qint64 from, qint64 to)
{
    const int j0 = qMax(0, c.indexAt(from));
    const int j1 = qMin(int(c.xs.size()), c.indexAt(to));
    if (j1 - j0 < 0.5 * (to - from) * c.fs / 1000.0 || j1 <= j0) return kNoData;
    const auto mm = std::minmax_element(c.xs.begin() + j0, c.xs.begin() + j1);
    return *mm.second - *mm.first;
}

float median(QVector<float> v) { return percentile(std::move(v), 50); }

void mergeSpans(QVector<Span> &spans)
{
    std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) { return a.start < b.start; });
    QVector<Span> out;
    for (const Span &s : spans) {
        if (!out.isEmpty() && s.start <= out.last().end) out.last().end = qMax(out.last().end, s.end);
        else out.append(s);
    }
    spans = out;
}

} // namespace

int FlowResult::count(bool apnea) const
{
    return int(std::count_if(events.begin(), events.end(), [apnea](const FlowEvent &e) { return e.apnea == apnea; }));
}

FlowResult analyzeFlow(const QVector<FlowChunk> &chunkIn, const QVector<Span> &excluded,
                       const Grid *pulse, const Grid *obstructLevel, const FlowParams &params)
{
    Q_UNUSED(pulse)
    Q_UNUSED(obstructLevel)
    FlowResult result;

    // ---- 3.3.1 preparation
    QVector<Proc> chunks;
    for (const FlowChunk &c : chunkIn) {
        if (c.rateMs <= 0 || c.samples.size() < 2) continue;
        result.sampleRateHz = qMax(result.sampleRateHz, 1000.0 / c.rateMs);
        if (1000.0 / c.rateMs < kMinHz) continue;
        chunks.append(prepare(c));
    }
    std::sort(chunks.begin(), chunks.end(), [](const Proc &a, const Proc &b) { return a.start < b.start; });
    if (chunks.isEmpty()) return result;
    result.analyzed = true;
    result.flScored = result.sampleRateHz >= kFlHz;

    // ---- 3.3.2 breaths
    for (const Proc &c : chunks) segmentBreaths(c, result.breaths);

    // ---- 3.3.3 envelope and baseline, 1 Hz on a common grid
    Grid &E = result.envelope;
    E.start = (chunks.first().start / 1000) * 1000;
    qint64 lastEnd = 0;
    for (const Proc &c : chunks) lastEnd = qMax(lastEnd, c.end());
    const int n = int((((lastEnd + 999) / 1000) * 1000 - E.start) / 1000);
    E.v.fill(kNoData, n);

    // local breath period: median of the breaths that started in the last 120 s
    QVector<float> windowSec(n, 5);
    {
        int lo = 0, hi = 0;
        const QVector<Breath> &br = result.breaths;
        QVector<float> periods;
        for (int s = 0; s < n; ++s) {
            const qint64 t = E.timeAt(s);
            while (hi < br.size() && br[hi].start < t) ++hi;
            while (lo < hi && br[lo].start < t - 120000) ++lo;
            if (hi - lo >= 3) {
                periods.clear();
                for (int k = lo; k < hi; ++k) periods.append(float(br[k].end - br[k].start) / 1000.0f);
                windowSec[s] = qBound(3.0f, 1.2f * median(periods), 10.0f);
            }
        }
    }
    for (int s = 0; s < n; ++s) {
        const qint64 centre = E.timeAt(s) + 500;
        const int ci = chunkAt(chunks, centre);
        if (ci < 0) continue;
        const qint64 half = qint64(windowSec[s] * 500);
        E.v[s] = peakToPeak(chunks[ci], centre - half, centre + half);
    }

    QVector<char> isExcluded(n, 0);
    for (const Span &sp : excluded) {
        for (int s = qMax(0, E.indexOf(sp.start)); s <= qMin(n - 1, E.indexOf(sp.end - 1)); ++s) isExcluded[s] = 1;
    }
    QVector<float> masked = E.v;
    for (int s = 0; s < n; ++s) {
        if (isExcluded[s]) masked[s] = kNoData;
    }
    Grid &B = result.baseline;
    B.start = E.start;
    B.v = trailingPercentile(masked, int(params.baselineWindowSec), params.baselinePercentile, 30);
    const float eNight = percentile(masked, 50);
    QVector<char> weak(n, 0);
    for (int s = 0; s < n; ++s) {
        weak[s] = hasData(B.v[s]) && hasData(eNight) && B.v[s] < 0.2f * eNight;
    }

    // ---- 3.3.4 candidate events
    QVector<Span> longReductions;
    const float keep = float(1.0 - params.hypopneaReduction);   // envelope fraction still "normal"
    int s = 0;
    while (s < n) {
        const bool usable = hasData(E.v[s]) && hasData(B.v[s]) && !isExcluded[s] && !weak[s];
        if (!usable || E.v[s] > keep * B.v[s]) { ++s; continue; }

        const float bref = B.v[s];
        const float thr = keep * bref;
        int u = s + 1, above = 0, endSec = -1;
        bool broken = false;
        for (; u < n; ++u) {
            if (!hasData(E.v[u]) || isExcluded[u]) { broken = true; endSec = u; break; }
            if (E.v[u] > thr) {
                if (++above >= 3) { endSec = u - 2; break; }   // brief rises of up to 2 s are allowed
            } else {
                above = 0;
            }
        }
        if (endSec < 0) { endSec = n; broken = true; }
        const int resume = qMax(endSec, s + 1);
        if (broken) { s = resume; continue; }

        // Refine the edges on breaths: the envelope window blurs them by half its length.
        const qint64 detStart = E.timeAt(s), detEnd = E.timeAt(endSec);
        const qint64 L = qint64(windowSec[s] * 1000);
        qint64 start = detStart, end = detEnd;
        const QVector<Breath> &br = result.breaths;
        for (int k = br.size() - 1; k >= 0; --k) {
            if (br[k].start >= detStart || br[k].amplitude <= thr) continue;
            if (br[k].end >= detStart - 2 * L && br[k].end <= detStart + L) start = br[k].end;
            break;
        }
        for (int k = 0; k < br.size(); ++k) {
            if (br[k].start <= detEnd - L || br[k].start < start || br[k].amplitude <= thr) continue;
            if (br[k].start <= detEnd + 2 * L) end = br[k].start;
            break;
        }
        s = resume;
        if (end - start < params.minEventSec * 1000) continue;
        if (end - start > params.maxEventSec * 1000) {
            longReductions.append(Span { start, end, 0 });   // mask off, or not an event
            continue;
        }

        const int ci = chunkAt(chunks, start);
        if (ci < 0) continue;
        const Proc &c = chunks[ci];

        FlowEvent ev;
        ev.start = start;
        ev.end = end;
        ev.baseline = bref;

        // Apnea: >= minEventSec with flow within (1 - apneaReduction) of the baseline,
        // judged on 2 s windows so that it does not depend on the breath segmentation.
        const float flat = float(1.0 - params.apneaReduction) * bref;
        int run = 0, bestRun = 0;
        for (qint64 w = start; w + 2000 <= end; w += 1000) {
            const float p2p = peakToPeak(c, w, w + 2000);
            run = (hasData(p2p) && p2p <= flat) ? run + 1 : 0;
            bestRun = qMax(bestRun, run);
        }
        ev.apnea = bestRun > 0 && bestRun + 1 >= params.minEventSec;

        QVector<float> amps;
        for (const Breath &b : br) {
            if (b.start >= start && b.end <= end) amps.append(b.amplitude);
        }
        if (!amps.isEmpty()) {
            ev.reduction = qBound(0.0f, 1.0f - median(amps) / bref, 1.0f);
        } else {
            QVector<float> red;
            for (int k = qMax(0, E.indexOf(start)); k < qMin(n, E.indexOf(end)); ++k) {
                if (hasData(E.v[k])) red.append(1.0f - E.v[k] / bref);
            }
            ev.reduction = red.isEmpty() ? 1.0f : qBound(0.0f, median(red), 1.0f);
        }
        if (ev.apnea) ev.reduction = qMax(ev.reduction, float(params.apneaReduction));
        result.events.append(ev);
    }

    // ---- 3.3.9 unscoreable time
    QVector<Span> &uns = result.unscoreable;
    const qint64 dataStart = chunks.first().start;
    for (int k = 0; k < n;) {
        const qint64 t = E.timeAt(k);
        const bool inRange = t + 1000 > dataStart && t < lastEnd;
        const bool bad = inRange && (!hasData(E.v[k]) || isExcluded[k] || weak[k]);
        if (!bad) { ++k; continue; }
        int j = k;
        while (j < n && (!hasData(E.v[j]) || isExcluded[j] || weak[j])) ++j;
        uns.append(Span { E.timeAt(k), qMin(E.timeAt(j), lastEnd), 0 });
        k = j;
    }
    uns += longReductions;
    mergeSpans(uns);

    // analysable time: seconds with flow outside every unscoreable span
    QVector<char> unscoreableSec(n, 0);
    for (const Span &sp : uns) {
        for (int k = qMax(0, E.indexOf(sp.start)); k <= qMin(n - 1, E.indexOf(sp.end - 1)); ++k) unscoreableSec[k] = 1;
    }
    for (int k = 0; k < n; ++k) {
        if (!hasData(E.v[k])) continue;
        if (unscoreableSec[k]) ++result.unscoreableSeconds;
        else ++result.flowSeconds;
    }
    // events never overlap an unscoreable span
    result.events.erase(std::remove_if(result.events.begin(), result.events.end(), [&](const FlowEvent &ev) {
        for (const Span &sp : uns) {
            if (ev.start < sp.end && ev.end > sp.start) return true;
        }
        return false;
    }), result.events.end());

    return result;
}

} // namespace analysis
