/* Sleep Analysis Flow Analyzer
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "flow_analyzer.h"

#include "apnea_classifier.h"

#include <algorithm>

namespace analysis {

float flowLimitationScore(const QVector<float> &insp)
{
    const int n = insp.size();
    if (n < 4) return kNoData;
    const float pif = *std::max_element(insp.begin(), insp.end());
    if (!(pif > 0)) return kNoData;

    // time normalised to tau in [0, 1], flow to u = x / PIF, 32 points
    constexpr int kPoints = 32;
    float u[kPoints];
    for (int k = 0; k < kPoints; ++k) {
        const double pos = double(k) / (kPoints - 1) * (n - 1);
        const int i = int(pos);
        const double f = pos - i;
        const float a = insp[i], b = insp[qMin(n - 1, i + 1)];
        u[k] = float((a + (b - a) * f) / pif);
    }
    auto tau = [](int k) { return double(k) / (kPoints - 1); };
    auto clamp01 = [](double v) { return float(qBound(0.0, v, 1.0)); };

    // Flat top: share of the middle of inspiration near the peak (a sine: about 0.6).
    int mid = 0, flat = 0;
    for (int k = 0; k < kPoints; ++k) {
        if (tau(k) < 0.2 || tau(k) > 0.8) continue;
        ++mid;
        if (u[k] >= 0.85f) ++flat;
    }
    const float plateau = clamp01((double(flat) / qMax(1, mid) - 0.6) / 0.35);

    // "M": a dip in the middle between two peaks.
    int minK = -1;
    float minV = 2;
    for (int k = 0; k < kPoints; ++k) {
        if (tau(k) >= 0.25 && tau(k) <= 0.75 && u[k] < minV) { minV = u[k]; minK = k; }
    }
    float dip = 0;
    if (minK > 0 && minK < kPoints - 1) {
        const float left = *std::max_element(u, u + minK);
        const float right = *std::max_element(u + minK + 1, u + kPoints);
        if (left >= 0.9f && right >= 0.9f && minV <= 0.8f) dip = clamp01((0.95 - minV) / 0.25);
    }

    // "Chair": an early peak, then a level plateau well below it.
    float chair = 0;
    const int peakK = int(std::max_element(u, u + kPoints) - u);
    if (tau(peakK) <= 0.3) {
        double st = 0, su = 0, stt = 0, stu = 0;
        int m = 0;
        for (int k = 0; k < kPoints; ++k) {
            if (tau(k) < 0.4 || tau(k) > 0.8) continue;
            st += tau(k); su += u[k]; stt += tau(k) * tau(k); stu += tau(k) * u[k]; ++m;
        }
        const double mean = su / m;
        const double slope = (m * stu - st * su) / (m * stt - st * st);
        if (mean >= 0.5 && mean <= 0.85 && std::fabs(slope) <= 0.5) chair = clamp01(1 - std::fabs(slope) / 0.5);
    }
    return qMax(plateau, qMax(dip, chair));
}

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
        if (c.fs >= kFlHz && (f - r) / c.fs >= 0.6) {
            b.fl = flowLimitationScore(QVector<float>(xs.begin() + r, xs.begin() + f));
        }
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

bool overlapsAny(const QVector<Span> &spans, qint64 start, qint64 end)
{
    for (const Span &sp : spans) {
        if (start < sp.end && end > sp.start) return true;
    }
    return false;
}

int spanSeconds(const QVector<Span> &spans)
{
    qint64 ms = 0;
    for (const Span &sp : spans) ms += sp.end - sp.start;
    return int(ms / 1000);
}

// Consecutive breaths: no pause between them.
bool follows(const Breath &prev, const Breath &next) { return next.start - prev.end <= 500; }

// Runs of >= 3 flow-limited breaths, or >= 10 s of them (spec §3.3.5).
QVector<Span> flowLimitationSpans(const QVector<Breath> &breaths, const QVector<Span> &blocked, double threshold)
{
    QVector<Span> out;
    QVector<const Breath *> run;
    auto flush = [&]() {
        if (!run.isEmpty() && (run.size() >= 3 || run.last()->end - run.first()->start >= 10000)) {
            out.append(Span { run.first()->start, run.last()->end, float(run.size()) });
        }
        run.clear();
    };
    for (const Breath &b : breaths) {
        const bool limited = hasData(b.fl) && b.fl >= threshold && !overlapsAny(blocked, b.start, b.end);
        if (!limited) { flush(); continue; }
        if (!run.isEmpty() && !follows(*run.last(), b)) flush();
        run.append(&b);
    }
    flush();
    return out;
}

// RERA-like episodes (spec §3.3.7): >= 2 consecutive breaths over >= 10 s, each flow
// limited or smaller than the one before, ended by a breath >= 1.5x their mean amplitude.
QVector<Span> flowReras(const QVector<Breath> &breaths, const QVector<Span> &blocked, double threshold)
{
    QVector<Span> out;
    QVector<int> run;
    for (int k = 0; k < breaths.size(); ++k) {
        const Breath &b = breaths[k];
        const bool free = !overlapsAny(blocked, b.start, b.end);
        const bool contiguous = !run.isEmpty() && follows(breaths[run.last()], b);
        const bool limited = hasData(b.fl) && b.fl >= threshold;
        const bool shrinking = k > 0 && follows(breaths[k - 1], b) && b.amplitude < 0.95f * breaths[k - 1].amplitude;
        const bool qualifies = free && (limited || shrinking);

        if (qualifies && (run.isEmpty() || contiguous)) { run.append(k); continue; }

        if (run.size() >= 2 && contiguous && free) {
            const qint64 dur = breaths[run.last()].end - breaths[run.first()].start;
            double mean = 0;
            for (int i : run) mean += breaths[i].amplitude;
            mean /= run.size();
            if (dur >= 10000 && b.amplitude >= 1.5 * mean) out.append(Span { breaths[run.first()].start, b.start, 0 });
        }
        run.clear();
        if (qualifies) run.append(k);
    }
    return out;
}

// Periodic breathing from the envelope (spec §3.3.8): 600 s windows every 60 s whose
// autocorrelation peaks >= 0.5 at a lag of 30-100 s and whose envelope swings by >= 50 %.
QVector<Span> periodicBreathing(const Grid &E)
{
    constexpr int kWin = 600, kStep = 60, kLagMin = 30, kLagMax = 100;
    struct Window { int start; int lag; };
    QVector<Window> windows;
    QVector<float> v(kWin);
    QVector<double> d(kWin);
    for (int w = 0; w + kWin <= E.size(); w += kStep) {
        int missing = 0;
        double sum = 0;
        for (int i = 0; i < kWin; ++i) {
            v[i] = E.v[w + i];
            if (hasData(v[i])) sum += v[i]; else ++missing;
        }
        if (missing > kWin / 5) continue;
        const double mean = sum / (kWin - missing);
        for (float &x : v) if (!hasData(x)) x = float(mean);
        const float p90 = percentile(v, 90), p10 = percentile(v, 10);
        if (!(p90 > 0) || (p90 - p10) / p90 < 0.5f) continue;
        double var = 0;
        for (int i = 0; i < kWin; ++i) { d[i] = v[i] - mean; var += d[i] * d[i]; }
        if (var <= 0) continue;
        double best = 0;
        int bestLag = 0;
        for (int lag = kLagMin; lag <= kLagMax; ++lag) {
            double acc = 0;
            for (int i = 0; i + lag < kWin; ++i) acc += d[i] * d[i + lag];
            if (acc / var > best) { best = acc / var; bestLag = lag; }
        }
        if (best >= 0.5) windows.append(Window { w, bestLag });
    }

    QVector<Span> out;
    int count = 0;
    double lagSum = 0;
    auto close = [&]() {
        if (count && out.last().end - out.last().start >= 600000) out.last().value = float(lagSum / count);
        else if (count) out.removeLast();
        count = 0;
        lagSum = 0;
    };
    for (const Window &win : windows) {
        const qint64 a = E.timeAt(win.start), b = E.timeAt(win.start + kWin);
        if (count && a <= out.last().end) {
            out.last().end = b;
        } else {
            close();
            out.append(Span { a, b, 0 });
        }
        ++count;
        lagSum += win.lag;
    }
    close();
    return out;
}

// Mean of a 1 Hz grid over [from, to), or NaN.
float meanOver(const Grid *g, qint64 from, qint64 to)
{
    if (!g || g->size() == 0) return kNoData;
    double sum = 0;
    int cnt = 0;
    for (int i = qMax(0, g->indexOf(from)); i < qMin(g->size(), g->indexOf(to)); ++i) {
        if (hasData(g->v[i])) { sum += g->v[i]; ++cnt; }
    }
    return cnt ? float(sum / cnt) : kNoData;
}

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

int FlowResult::flowLimitationSeconds() const { return spanSeconds(flowLimitation); }
int FlowResult::periodicSeconds() const { return spanSeconds(periodic); }

FlowResult analyzeFlow(const QVector<FlowChunk> &chunkIn, const QVector<Span> &excluded,
                       const Grid *pulse, const Grid *obstructLevel, const FlowParams &params)
{
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

    // ---- 3.3.5, 3.3.7, 3.3.8 flow limitation, RERA-like episodes, periodic breathing
    QVector<Span> blocked = uns;   // breaths inside events or unscoreable time do not count
    for (const FlowEvent &ev : result.events) blocked.append(Span { ev.start, ev.end, 0 });
    for (const Breath &b : result.breaths) {
        if (!hasData(b.fl) || overlapsAny(blocked, b.start, b.end)) continue;
        result.flSum += b.fl;
        ++result.flBreaths;
    }
    result.flowLimitation = flowLimitationSpans(result.breaths, blocked, params.flThreshold);
    result.reras = flowReras(result.breaths, blocked, params.flThreshold);
    result.periodic = periodicBreathing(E);

    // ---- 3.3.6 apnea classification (experimental)
    if (params.classifyApneas) {
        for (FlowEvent &ev : result.events) {
            if (!ev.apnea) continue;
            const int ci = chunkAt(chunks, ev.start);
            if (ci < 0) continue;
            const Proc &c = chunks[ci];
            ApneaEvidence evd;
            const int j0 = qMax(0, c.indexAt(ev.start)), j1 = qMin(int(c.x.size()), c.indexAt(ev.end));
            if (j1 > j0) evd.flow = QVector<float>(c.x.begin() + j0, c.x.begin() + j1);
            evd.fs = c.fs;
            evd.baseline = ev.baseline;
            for (const Breath &b : result.breaths) {
                if (b.end <= ev.start + 500) evd.before.append(b);
                else if (!evd.after && b.start >= ev.end - 500) evd.after = &b;
                if (evd.before.size() > 3) evd.before.removeFirst();
            }
            evd.pulseBpm = meanOver(pulse, ev.start, ev.end);
            evd.inPeriodicBreathing = overlapsAny(result.periodic, ev.start, ev.end);
            evd.obstructLevel = meanOver(obstructLevel, ev.start, ev.end);
            ev.classScore = apneaScore(evd, params.flThreshold);
            ev.cls = classForScore(ev.classScore);
        }
    }

    return result;
}

} // namespace analysis
