/* Sleep Analysis Day Scorer
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "day_scorer.h"

#include <algorithm>
#include <cmath>

namespace analysis {

namespace {

constexpr double kMinCoverage = 0.75;        // SpO2 share of [s, e + link] that counts as covered
constexpr qint64 kArousalBeforeMs = 5000;    // pulse rise as arousal: [e - 5 s, e + 15 s]
constexpr qint64 kArousalAfterMs = 15000;
constexpr qint64 kDhrBeforeMs = 10000;       // ΔHR: mean over [s - 10 s, s] ...
constexpr qint64 kDhrAfterMs = 20000;        // ... against the maximum over [s, e + 20 s]
constexpr int kDhrMinSamples = 5;
constexpr int kMinFlBreaths = 2;             // breaths needed to classify a hypopnea
constexpr double kObstructiveShare = 0.5;    // limited breaths share: obstructive at or above ...
constexpr double kCentralShare = 0.2;        // ... central below (during periodic breathing)
constexpr int kHintMinEvents = 10;           // offset hint (spec §3.4.7)
constexpr int kHintMinMatches = 8;
constexpr double kHintMinGain = 1.5;
constexpr qint64 kHintMaxLagMs = 20 * 60000;
constexpr qint64 kHintStepMs = 5000;
constexpr qint64 kHintWindowMs = 40000;
constexpr qint64 kHintMinLagMs = 30000;

int spanSeconds(const QVector<Span> &spans)
{
    qint64 ms = 0;
    for (const Span &s : spans) ms += qMax<qint64>(0, s.end - s.start);
    return int(ms / 1000);
}

bool inside(qint64 t, const QVector<Span> &spans)
{
    for (const Span &s : spans) {
        if (t >= s.start && t <= s.end) return true;
    }
    return false;
}

// Share of the grid's cells in [from, to) that hold data (0 outside the grid).
double coverage(const Grid &g, qint64 from, qint64 to)
{
    if (g.size() == 0 || to <= from) return 0;
    const int a = g.indexOf(from), b = g.indexOf(to - 1);
    int valid = 0;
    for (int i = qMax(0, a); i <= qMin(g.size() - 1, b); ++i) {
        if (hasData(g.v[i])) ++valid;
    }
    return double(valid) / (b - a + 1);
}

// Cells outside every span become NaN.
void keepOnly(Grid &g, const QVector<Span> &spans)
{
    for (int i = 0; i < g.size(); ++i) {
        if (!inside(g.timeAt(i) + g.stepMs / 2, spans)) g.v[i] = kNoData;
    }
}

QVector<Span> windows(const QVector<DayEvent> &events, qint64 afterMs)
{
    QVector<Span> out;
    out.reserve(events.size());
    for (const DayEvent &e : events) out.append(Span { e.start, e.end + afterMs, 0 });
    return out;
}

// Desaturations by nadir time, for the "nadir in [s, e + link]" rules.
class Nadirs
{
  public:
    explicit Nadirs(const QVector<Desaturation> &desats) : m_desats(desats) {
        for (int i = 0; i < desats.size(); ++i) m_order.append(i);
        std::sort(m_order.begin(), m_order.end(), [&desats](int a, int b) {
            return desats[a].nadirTime < desats[b].nadirTime;
        });
    }
    bool any(qint64 from, qint64 to, double minDepth) const {
        auto it = std::lower_bound(m_order.begin(), m_order.end(), from,
                                   [this](int i, qint64 t) { return m_desats[i].nadirTime < t; });
        for (; it != m_order.end() && m_desats[*it].nadirTime <= to; ++it) {
            if (m_desats[*it].depth() >= minDepth - 1e-6) return true;
        }
        return false;
    }
  private:
    const QVector<Desaturation> &m_desats;
    QVector<int> m_order;
};

bool pulseRiseStartsIn(const QVector<PulseRise> &rises, qint64 from, qint64 to)
{
    for (const PulseRise &r : rises) {
        if (r.start >= from && r.start <= to) return true;
    }
    return false;
}

// Obstructive when most breaths in the event are flow-limited, central when almost none
// are during periodic breathing (spec §3.4.2).
RespEvent classifyHypopnea(const DayEvent &e, const QVector<TimedValue> &flScores,
                           const QVector<Span> &periodic, double flThreshold)
{
    auto it = std::lower_bound(flScores.begin(), flScores.end(), e.start,
                               [](const TimedValue &v, qint64 t) { return v.t < t; });
    int n = 0, limited = 0;
    for (; it != flScores.end() && it->t <= e.end; ++it) {
        ++n;
        if (it->v >= flThreshold) ++limited;
    }
    if (n < kMinFlBreaths) return RespEvent::Hypopnea;
    const double share = double(limited) / n;
    if (share >= kObstructiveShare) return RespEvent::ObstructiveHypopnea;
    if (share < kCentralShare && inside((e.start + e.end) / 2, periodic)) return RespEvent::CentralHypopnea;
    return RespEvent::Hypopnea;
}

qint64 median(QVector<qint64> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Desaturations whose nadir, shifted by the lag, falls in [end, end + 40 s] of an event.
int hintMatches(const QVector<qint64> &nadirs, const QVector<qint64> &ends, qint64 lag)
{
    int n = 0;
    for (qint64 nadir : nadirs) {
        const qint64 t = nadir + lag;
        auto it = std::lower_bound(ends.begin(), ends.end(), t - kHintWindowMs);
        if (it != ends.end() && *it <= t) ++n;
    }
    return n;
}

void scoreHypopneas(const DayInput &in, const AnalysisParams &p, const Grid &spo2, DayResult &r)
{
    const Nadirs nadirs(r.oxi.desaturations);
    const qint64 link = qint64(p.day.linkWindowSec * 1000);
    QVector<TimedValue> flScores = in.flScores;
    std::sort(flScores.begin(), flScores.end(), [](const TimedValue &a, const TimedValue &b) { return a.t < b.t; });

    for (const DayEvent &c : in.candidates) {
        const bool flowOk = c.value >= p.day.flowOnlyReduction - 1e-6;
        const bool covered = coverage(spo2, c.start, c.end + link) >= kMinCoverage;
        const bool arousal = p.day.pulseRiseAsArousal
                          && pulseRiseStartsIn(r.oxi.pulseRises, c.end - kArousalBeforeMs, c.end + kArousalAfterMs);
        const bool aasm3 = covered ? (arousal || nadirs.any(c.start, c.end + link, 3)) : flowOk;
        const bool cms4 = covered ? (arousal || nadirs.any(c.start, c.end + link, 4)) : flowOk;
        r.hypopneasAasm3 += aasm3;
        r.hypopneasCms4 += cms4;
        r.hypopneasFlowOnly += flowOk;
        if (!covered && p.day.rule != HypopneaRule::FlowOnly) ++r.unconfirmable;

        const bool confirmed = p.day.rule == HypopneaRule::Cms4 ? cms4
                             : p.day.rule == HypopneaRule::FlowOnly ? flowOk : aasm3;
        if (!confirmed) continue;
        DayEvent h = c;
        h.type = classifyHypopnea(c, flScores, in.periodic, p.flow.flThreshold);
        r.hypopneas.append(h);
    }
}

void linkDesaturations(const DayInput &in, const AnalysisParams &p, DayResult &r)
{
    if (!r.hasCpap) return;
    const qint64 link = qint64(p.day.linkWindowSec * 1000);
    const QVector<Span> analysisWindows = windows(r.apneas, link) + windows(r.hypopneas, link);
    QVector<DayEvent> deviceAhi;
    for (const DayEvent &e : in.deviceEvents) {
        if (groupOf(e.type) != EventGroup::Rera) deviceAhi.append(e);
    }
    const QVector<Span> deviceWindows = windows(deviceAhi, link);
    QVector<Span> scoreable;
    for (const CpapSession &s : in.cpap) {
        if (s.analyzed) scoreable.append(s.span);
    }

    for (int i = 0; i < r.oxi.desaturations.size(); ++i) {
        const Desaturation &d = r.oxi.desaturations[i];
        if (inside(d.nadirTime, deviceWindows)) ++r.deviceLinkedDesaturations;
        if (inside(d.nadirTime, analysisWindows)) {
            ++r.linkedDesaturations;
            r.linkedDesatArea += d.area;
        } else if (inside(d.nadirTime, scoreable) && !inside(d.nadirTime, in.unscoreable)) {
            r.unexplained.append(i);
        }
    }
}

void pulseResponse(const Grid &pulse, DayResult &r)
{
    if (!r.hasPulse) return;
    for (const QVector<DayEvent> *list : { &r.apneas, &r.hypopneas }) {
        for (const DayEvent &e : *list) {
            if (pulseRiseStartsIn(r.oxi.pulseRises, e.end - kArousalBeforeMs, e.end + kArousalAfterMs)) {
                ++r.eventsWithPulseRise;
            }
            double sum = 0;
            int before = 0, after = 0;
            float peak = kNoData;
            for (int i = qMax(0, pulse.indexOf(e.start - kDhrBeforeMs)); i < pulse.size() && pulse.timeAt(i) < e.start; ++i) {
                if (hasData(pulse.v[i])) { sum += pulse.v[i]; ++before; }
            }
            for (int i = qMax(0, pulse.indexOf(e.start)); i < pulse.size() && pulse.timeAt(i) <= e.end + kDhrAfterMs; ++i) {
                if (!hasData(pulse.v[i])) continue;
                ++after;
                if (!hasData(peak) || pulse.v[i] > peak) peak = pulse.v[i];
            }
            if (before < kDhrMinSamples || after < kDhrMinSamples) continue;
            r.dhrSum += peak - sum / before;
            ++r.dhrEvents;
        }
    }
}

void compare(const DayInput &in, DayResult &r)
{
    if (!r.hasFlow) return;
    r.hasComparison = true;
    QVector<Span> analysed;
    for (const CpapSession &s : in.cpap) {
        if (s.analyzed) analysed.append(s.span);
    }
    for (const DayEvent &e : in.deviceEvents) {
        const qint64 mid = (e.start + e.end) / 2;
        if (inside(mid, analysed) && !inside(mid, in.unscoreable)) r.deviceEvents.append(e);
    }
    r.analysisEvents = r.apneas + r.hypopneas + r.reras;
    std::sort(r.analysisEvents.begin(), r.analysisEvents.end(),
              [](const DayEvent &a, const DayEvent &b) { return a.start < b.start; });

    QVector<MatchEvent> device, analysis;
    for (const DayEvent &e : r.deviceEvents) device.append({ e.start, e.end, e.type });
    for (const DayEvent &e : r.analysisEvents) analysis.append({ e.start, e.end, e.type });
    r.match = matchEvents(device, analysis);

    QVector<qint64> toEnd, toStart;
    for (const auto &m : r.match.matched) {
        const DayEvent &d = r.deviceEvents[m.first], &a = r.analysisEvents[m.second];
        if (groupOf(d.type) != EventGroup::Apnea || groupOf(a.type) != EventGroup::Apnea) continue;
        toEnd.append(d.stamp - a.end);
        toStart.append(d.stamp - a.start);
    }
    if (!toEnd.isEmpty()) {
        r.hasApneaOffset = true;
        r.apneaOffsetToEndMs = median(toEnd);
        r.apneaOffsetToStartMs = median(toStart);
    }
}

void offsetHint(const DayInput &in, DayResult &r)
{
    if (!r.hasCpap || !in.oxiFromSeparateDevice || r.oxi.desaturations.size() < kHintMinEvents) return;
    // Events that do not depend on the oximeter: the flow's apneas and candidates, or
    // without analysed flow the device's apneas and hypopneas.
    QVector<qint64> ends;
    if (r.hasFlow) {
        for (const DayEvent &e : in.apneas) ends.append(e.end);
        for (const DayEvent &e : in.candidates) ends.append(e.end);
    } else {
        for (const DayEvent &e : in.deviceEvents) {
            if (groupOf(e.type) != EventGroup::Rera) ends.append(e.end);
        }
    }
    if (ends.size() < kHintMinEvents) return;
    std::sort(ends.begin(), ends.end());
    QVector<qint64> nadirs;
    for (const Desaturation &d : r.oxi.desaturations) nadirs.append(d.nadirTime);

    QVector<int> matches;
    for (qint64 lag = -kHintMaxLagMs; lag <= kHintMaxLagMs; lag += kHintStepMs) {
        matches.append(hintMatches(nadirs, ends, lag));
    }
    const int atZero = matches[int(kHintMaxLagMs / kHintStepMs)];
    const int best = *std::max_element(matches.begin(), matches.end());
    // Lags a little apart all fit when the window [end, end + 40 s] slides over the
    // nadirs: take the middle of the longest run of best lags (the nearest to 0 on a tie).
    qint64 bestLag = 0;
    int bestRun = 0;
    for (int i = 0; i < matches.size();) {
        if (matches[i] != best) { ++i; continue; }
        int j = i;
        while (j + 1 < matches.size() && matches[j + 1] == best) ++j;
        const qint64 mid = -kHintMaxLagMs + qint64((i + j) / 2) * kHintStepMs;
        if (j - i + 1 > bestRun || (j - i + 1 == bestRun && std::llabs(mid) < std::llabs(bestLag))) {
            bestRun = j - i + 1;
            bestLag = mid;
        }
        i = j + 1;
    }
    if (best >= kHintMinMatches && best >= kHintMinGain * atZero && std::llabs(bestLag) >= kHintMinLagMs) {
        r.hasOffsetHint = true;
        r.offsetHintMs = bestLag;
    }
}

} // namespace

int DayResult::count(RespEvent type) const
{
    int n = 0;
    for (const QVector<DayEvent> *list : { &apneas, &hypopneas, &reras }) {
        for (const DayEvent &e : *list) n += e.type == type;
    }
    return n;
}

int DayResult::deviceCount(EventGroup group) const
{
    int n = 0;
    for (const DayEvent &e : deviceEvents) n += groupOf(e.type) == group;
    return n;
}

DayResult scoreDay(const DayInput &in, const AnalysisParams &p)
{
    DayResult r;
    r.rule = p.day.rule;

    QVector<Span> cpapSpans;
    for (const CpapSession &s : in.cpap) {
        cpapSpans.append(s.span);
        if (!s.analyzed) continue;
        r.flowRateHz = r.hasFlow ? qMin(r.flowRateHz, s.sampleRateHz) : s.sampleRateHz;
        r.flScored = r.hasFlow ? (r.flScored && s.flScored) : s.flScored;
        r.hasFlow = true;
        r.flowSeconds += s.flowSeconds;
        r.unscoreableSeconds += s.unscoreableSeconds;
        r.flSum += s.flSum;
        r.flBreaths += s.flBreaths;
    }
    r.hasCpap = !in.cpap.isEmpty();
    r.flSeconds = spanSeconds(in.flowLimitation);
    r.pbSeconds = spanSeconds(in.periodic);

    // Oximetry of the whole night, or of CPAP time only when so configured.
    Grid spo2 = in.spo2, pulse = in.pulse;
    r.oxiScope = QStringLiteral("night");
    if (p.day.limitOxiToCpap && r.hasCpap) {
        keepOnly(spo2, cpapSpans);
        keepOnly(pulse, cpapSpans);
        r.oxiScope = QStringLiteral("cpap");
    }
    r.oxi = analyzeOximetry(spo2, pulse, p.oxi);
    r.hasOximetry = r.oxi.hasSpo2;
    r.hasPulse = r.oxi.hasPulse;
    const Grid cleanedSpo2 = cleanSpo2(spo2);
    const Grid cleanedPulse = cleanPulse(pulse);

    r.apneas = in.apneas;
    for (const Span &s : in.reras) r.reras.append(DayEvent { s.start, s.end, RespEvent::Rera, -1, 0, 0 });
    scoreHypopneas(in, p, cleanedSpo2, r);
    linkDesaturations(in, p, r);
    pulseResponse(cleanedPulse, r);
    compare(in, r);
    offsetHint(in, r);
    return r;
}

} // namespace analysis
