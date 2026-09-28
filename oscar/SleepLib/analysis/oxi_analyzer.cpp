/* Sleep Analysis Oximetry Analyzer
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "oxi_analyzer.h"

#include <algorithm>
#include <deque>

namespace analysis {

namespace {

constexpr float kSpo2Low = 50, kSpo2High = 100;
constexpr float kPulseLow = 30, kPulseHigh = 220;
constexpr int kGapSec = 10;          // a gap this long splits the data into segments
constexpr int kMinSegmentSec = 60;   // shorter segments are dropped
constexpr int kMedianWindow = 5;

struct Range { int first; int last; };   // inclusive indices of valid samples

// Maximal runs of data in which no gap reaches kGapSec.
QVector<Range> segments(const QVector<float> &v)
{
    QVector<Range> out;
    const int n = v.size();
    int i = 0;
    while (i < n) {
        while (i < n && !hasData(v[i])) ++i;
        if (i >= n) break;
        Range r { i, i };
        int j = i;
        while (j < n) {
            if (hasData(v[j])) { r.last = j; ++j; continue; }
            int k = j;
            while (k < n && !hasData(v[k])) ++k;
            if (k >= n || k - j >= kGapSec) break;
            j = k;
        }
        out.append(r);
        i = r.last + 1;
    }
    return out;
}

void keepRange(QVector<float> &v, float low, float high)
{
    for (float &x : v) {
        if (hasData(x) && (x < low || x > high)) x = kNoData;
    }
}

// A jump of >= 10 points within 2 s that comes back (to within 3 points) within 5 s
// is a probe artifact, not a desaturation (spec §3.1.4).
void removeSpikes(QVector<float> &v)
{
    const int n = v.size();
    int i = 1;
    while (i < n) {
        if (!hasData(v[i])) { ++i; continue; }
        int prev = -1;
        for (int k = 1; k <= 2 && i - k >= 0; ++k) {
            if (hasData(v[i - k])) { prev = i - k; break; }
        }
        if (prev < 0 || std::fabs(v[i] - v[prev]) < 10) { ++i; continue; }
        const float before = v[prev];
        int back = -1;
        for (int j = i + 1; j <= i + 5 && j < n; ++j) {
            if (hasData(v[j]) && std::fabs(v[j] - before) <= 3) { back = j; break; }
        }
        if (back < 0) { ++i; continue; }
        for (int j = i; j < back; ++j) v[j] = kNoData;
        i = back;
    }
}

void dropShortSegments(QVector<float> &v)
{
    for (const Range &r : segments(v)) {
        if (r.last - r.first + 1 < kMinSegmentSec) {
            for (int i = r.first; i <= r.last; ++i) v[i] = kNoData;
        }
    }
}

QVector<Desaturation> findDesaturations(const Grid &s, const OxiParams &p)
{
    QVector<Desaturation> out;
    const QVector<float> &v = s.v;
    const int window = int(std::lround(p.desatMaxFallSec));   // reference window, s
    const int maxLen = int(std::lround(p.desatMaxSec));

    for (const Range &seg : segments(v)) {
        int windowStart = seg.first;   // the reference only looks back to the last event's end
        std::deque<int> maxima;        // indices, values strictly decreasing: front = latest peak
        int i = seg.first;
        while (i <= seg.last) {
            if (!hasData(v[i])) { ++i; continue; }
            while (!maxima.empty() && v[maxima.back()] <= v[i]) maxima.pop_back();
            maxima.push_back(i);
            const int lo = qMax(i - window, windowStart);
            while (maxima.front() < lo) maxima.pop_front();
            const float peak = v[maxima.front()];

            if (v[i] > peak - p.desatMinDrop) { ++i; continue; }

            // Entered a desaturation: find where the fall began and follow it to the end.
            const int pt = maxima.front();
            int st = pt + 1;
            while (st < i && !(hasData(v[st]) && v[st] <= peak - 1)) ++st;
            int nt = st;
            float nadir = v[st];
            for (int k = st; k <= i; ++k) {
                if (hasData(v[k]) && v[k] < nadir) { nadir = v[k]; nt = k; }
            }
            int et = -1;
            for (int j = i + 1; j <= seg.last; ++j) {
                if (j - st >= maxLen) { et = j; break; }   // cut long events
                if (!hasData(v[j])) continue;
                if (v[j] < nadir) {
                    nadir = v[j];
                    nt = j;
                } else if (v[j] >= peak - 1 || v[j] >= nadir + (2.0f / 3.0f) * (peak - nadir)) {
                    et = j;
                    break;
                }
            }
            if (et < 0) et = seg.last;   // data ends inside the event

            if (et - st >= p.desatMinSec && nt - pt <= p.desatMaxFallSec) {
                Desaturation d;
                d.start = s.timeAt(st);
                d.nadirTime = s.timeAt(nt);
                d.end = s.timeAt(et);
                d.peak = peak;
                d.nadir = nadir;
                for (int k = st; k < et; ++k) {
                    if (hasData(v[k]) && v[k] < peak) d.area += peak - v[k];
                }
                out.append(d);
            }
            // After an event the reference is taken from new data only, so each step of
            // a series is measured from its own peak.
            windowStart = et;
            maxima.clear();
            i = qMax(et, i + 1);
        }
    }
    return out;
}

// Three or more desaturations in a row with nadirs 20-120 s apart (spec §3.2.3).
QVector<Span> findCyclic(const QVector<Desaturation> &desats, double minDepth)
{
    QVector<Span> out;
    QVector<const Desaturation *> run;
    auto flush = [&]() {
        if (run.size() >= 3) {
            Span sp;
            sp.start = run.first()->start;
            sp.end = run.last()->end;
            sp.value = run.size();
            out.append(sp);
        }
        run.clear();
    };
    for (const Desaturation &d : desats) {
        if (d.depth() < minDepth) continue;
        if (!run.isEmpty()) {
            const qint64 gap = d.nadirTime - run.last()->nadirTime;
            if (gap < 20000 || gap > 120000) flush();
        }
        run.append(&d);
    }
    flush();
    return out;
}

QVector<PulseRise> findPulseRises(const Grid &pulse, const OxiParams &par)
{
    QVector<PulseRise> out;
    const QVector<float> &p = pulse.v;
    const int n = p.size();
    const float rise = float(par.pulseRise);
    const float half = rise / 2;

    // baseline: median over [t - 30 s, t - 5 s], at least 15 values
    QVector<float> base(n, kNoData);
    QVector<float> buf;
    for (int i = 0; i < n; ++i) {
        buf.clear();
        for (int j = qMax(0, i - 30); j <= i - 5; ++j) {
            if (hasData(p[j])) buf.append(p[j]);
        }
        if (buf.size() >= 15) base[i] = percentile(buf, 50);
    }

    int nextAllowed = 0;
    int i = 0;
    while (i < n) {
        if (i < nextAllowed || !hasData(p[i]) || !hasData(base[i]) || p[i] - base[i] < half) { ++i; continue; }
        const float b = base[i];
        bool reaches = false;
        for (int j = i; j <= i + 10 && j < n; ++j) {
            if (hasData(p[j]) && p[j] >= b + rise) { reaches = true; break; }
        }
        if (!reaches) { ++i; continue; }

        int above = 0;
        float highest = p[i];
        int j = i;
        for (; j < n && j - i < 60; ++j) {
            if (!hasData(p[j])) continue;
            if (j > i && p[j] <= b + half) break;
            if (p[j] >= b + rise) ++above;
            highest = qMax(highest, p[j]);
        }
        if (above < 3) { ++i; continue; }

        PulseRise r;
        r.start = pulse.timeAt(i);
        r.end = pulse.timeAt(j);
        r.amplitude = highest - b;
        out.append(r);
        nextAllowed = j + 5;
        i = j;
    }
    return out;
}

// Continuous runs of at least minSec seconds where \a test holds.
template <typename Test>
QVector<Span> runsWhere(const Grid &g, double minSec, Test test)
{
    QVector<Span> out;
    const int n = g.size();
    int i = 0;
    while (i < n) {
        if (!(hasData(g.v[i]) && test(g.v[i]))) { ++i; continue; }
        int j = i;
        while (j < n && hasData(g.v[j]) && test(g.v[j])) ++j;
        if (j - i >= minSec) out.append(Span { g.timeAt(i), g.timeAt(j), 0 });
        i = j;
    }
    return out;
}

QVector<ProblemZone> findZones(const Grid &s, const QVector<Desaturation> &desats, const Grid &pulse,
                               const QVector<PulseRise> &rises, const OxiParams &p)
{
    QVector<ProblemZone> out;
    const int n = s.size();
    if (n == 0) return out;

    // prefix counts of seconds below the thresholds and of valid seconds
    QVector<int> low(n + 1, 0), crit(n + 1, 0), valid(n + 1, 0);
    for (int i = 0; i < n; ++i) {
        const bool ok = hasData(s.v[i]);
        valid[i + 1] = valid[i] + (ok ? 1 : 0);
        low[i + 1] = low[i] + (ok && s.v[i] < p.zoneLowPct ? 1 : 0);
        crit[i + 1] = crit[i] + (ok && s.v[i] < p.zoneCriticalPct ? 1 : 0);
    }
    QVector<int> nadirs;
    for (const Desaturation &d : desats) {
        if (d.depth() >= p.desatMinDrop) nadirs.append(s.indexOf(d.nadirTime));
    }

    // Flagged windows, merged into ranges [a, b).
    const int win = qMax(1, int(p.zoneWindowSec));
    const int step = qMax(1, int(p.zoneStepSec));
    QVector<Range> ranges;   // here: first = a, last = b (exclusive)
    for (int w = 0; w < n; w += step) {
        const int e = qMin(n, w + win);
        if (valid[e] == valid[w]) continue;
        const int inWindow = int(std::count_if(nadirs.begin(), nadirs.end(), [&](int k) { return k >= w && k < e; }));
        const bool flagged = inWindow >= p.zoneMinDesats
                             || low[e] - low[w] >= p.zoneLowSec
                             || crit[e] - crit[w] >= p.zoneCriticalSec;
        if (!flagged) continue;
        if (!ranges.isEmpty() && w <= ranges.last().last) ranges.last().last = qMax(ranges.last().last, e);
        else ranges.append(Range { w, e });
        if (e == n) break;
    }

    // Trim each range to what made it a problem: the desaturations whose nadir is inside
    // and the seconds below the low threshold. (Whole windows would stretch a zone by up
    // to a window length on each side of the actual problem.) A zone shorter than
    // zoneMinSec is widened to it around its content rather than dropped.
    QVector<Range> zones;
    const int minLen = int(p.zoneMinSec);
    for (const Range &r : ranges) {
        int a = r.last, b = r.first;
        for (const Desaturation &d : desats) {
            const int k = s.indexOf(d.nadirTime);
            if (d.depth() < p.desatMinDrop || k < r.first || k >= r.last) continue;
            a = qMin(a, s.indexOf(d.start));
            b = qMax(b, s.indexOf(d.end));
        }
        for (int i = r.first; i < r.last; ++i) {
            if (hasData(s.v[i]) && s.v[i] < p.zoneLowPct) { a = qMin(a, i); b = qMax(b, i + 1); }
        }
        if (a >= b) { a = r.first; b = r.last; }
        a = qMax(a, r.first);
        b = qMin(b, r.last);
        if (b - a < minLen) {
            const int extra = minLen - (b - a);
            a = qMax(0, a - extra / 2);
            b = qMin(n, a + minLen);
            a = qMax(0, b - minLen);
        }
        if (!zones.isEmpty() && a - zones.last().last < p.zoneMergeGapSec) {
            zones.last().last = qMax(zones.last().last, b);
        } else {
            zones.append(Range { a, b });
        }
    }

    for (const Range &z : zones) {
        ProblemZone pz;
        pz.start = s.timeAt(z.first);
        pz.end = s.timeAt(z.last);
        pz.lowSeconds = low[z.last] - low[z.first];
        pz.criticalSeconds = crit[z.last] - crit[z.first];
        float minimum = kNoData;
        for (int i = z.first; i < z.last; ++i) {
            if (hasData(s.v[i]) && (!hasData(minimum) || s.v[i] < minimum)) minimum = s.v[i];
        }
        pz.minSpo2 = minimum;
        for (int k : nadirs) {
            if (k >= z.first && k < z.last) ++pz.desaturations;
        }
        pz.severity = ((hasData(minimum) && minimum < p.zoneCriticalPct)
                       || pz.criticalSeconds >= p.zoneCriticalSec) ? 2 : 1;
        if (pulse.size() > 0) {
            double sum = 0;
            int cnt = 0;
            for (int i = qMax(0, pulse.indexOf(pz.start)); i < qMin(pulse.size(), pulse.indexOf(pz.end)); ++i) {
                if (hasData(pulse.v[i])) { sum += pulse.v[i]; ++cnt; }
            }
            if (cnt) pz.meanPulse = float(sum / cnt);
        }
        for (const PulseRise &r : rises) {
            if (r.end >= pz.start && r.end < pz.end) ++pz.pulseRises;
        }
        out.append(pz);
    }
    return out;
}

int spanSeconds(const QVector<Span> &spans)
{
    qint64 ms = 0;
    for (const Span &s : spans) ms += s.end - s.start;
    return int(ms / 1000);
}

} // namespace

int OxiResult::countDesaturations(double minDepth) const
{
    return int(std::count_if(desaturations.begin(), desaturations.end(),
                             [minDepth](const Desaturation &d) { return d.depth() >= minDepth; }));
}

double OxiResult::desaturationArea() const
{
    double a = 0;
    for (const Desaturation &d : desaturations) a += d.area;
    return a;
}

int OxiResult::cyclicSeconds() const { return spanSeconds(cyclic); }
int OxiResult::bradySeconds() const { return spanSeconds(bradycardia); }
int OxiResult::tachySeconds() const { return spanSeconds(tachycardia); }

int OxiResult::zoneSeconds(int minSeverity) const
{
    qint64 ms = 0;
    for (const ProblemZone &z : zones) {
        if (z.severity >= minSeverity) ms += z.end - z.start;
    }
    return int(ms / 1000);
}

int OxiResult::spo2SecondsBelow(double threshold) const
{
    int s = 0;
    for (int i = 0; i < spo2Hist.size(); ++i) {
        if (kSpo2HistMin + i < threshold) s += spo2Hist[i];
    }
    return s;
}

Grid cleanSpo2(const Grid &raw)
{
    Grid g = raw;
    keepRange(g.v, kSpo2Low, kSpo2High);
    removeSpikes(g.v);
    g.v = medianFilter(g.v, kMedianWindow);
    dropShortSegments(g.v);
    return g;
}

Grid cleanPulse(const Grid &raw)
{
    Grid g = raw;
    keepRange(g.v, kPulseLow, kPulseHigh);
    g.v = medianFilter(g.v, kMedianWindow);
    dropShortSegments(g.v);
    return g;
}

OxiResult analyzeOximetry(const Grid &spo2Raw, const Grid &pulseRaw, const OxiParams &params)
{
    OxiResult r;
    r.spo2Hist.fill(0, kSpo2HistMax - kSpo2HistMin + 1);
    r.pulseHist.fill(0, kPulseHistMax - kPulseHistMin + 1);

    const Grid spo2 = cleanSpo2(spo2Raw);
    const Grid pulse = cleanPulse(pulseRaw);

    QVector<float> values;
    values.reserve(spo2.size());
    for (float v : spo2.v) {
        if (!hasData(v)) continue;
        values.append(v);
        ++r.spo2Seconds;
        r.spo2Sum += v;
        if (!hasData(r.spo2Nadir) || v < r.spo2Nadir) r.spo2Nadir = v;
        const int bin = qBound(kSpo2HistMin, int(std::lround(v)), kSpo2HistMax) - kSpo2HistMin;
        ++r.spo2Hist[bin];
    }
    r.hasSpo2 = r.spo2Seconds > 0;
    r.spo2Median = percentile(values, 50);

    for (float v : pulse.v) {
        if (!hasData(v)) continue;
        ++r.pulseSeconds;
        r.pulseSum += v;
        r.pulseSqSum += double(v) * v;
        if (!hasData(r.pulseMin) || v < r.pulseMin) r.pulseMin = v;
        if (!hasData(r.pulseMax) || v > r.pulseMax) r.pulseMax = v;
        const int bin = qBound(kPulseHistMin, int(std::lround(v)), kPulseHistMax) - kPulseHistMin;
        ++r.pulseHist[bin];
    }
    r.hasPulse = r.pulseSeconds > 0;

    if (r.hasSpo2) {
        r.desaturations = findDesaturations(spo2, params);
        r.cyclic = findCyclic(r.desaturations, params.desatMinDrop);
    }
    if (r.hasPulse) {
        r.pulseRises = findPulseRises(pulse, params);
        r.bradycardia = runsWhere(pulse, params.bradyTachyMinSec, [&](float v) { return v < params.bradyBpm; });
        r.tachycardia = runsWhere(pulse, params.bradyTachyMinSec, [&](float v) { return v > params.tachyBpm; });
    }
    if (r.hasSpo2) {
        r.zones = findZones(spo2, r.desaturations, pulse, r.pulseRises, params);
    }
    return r;
}

} // namespace analysis
