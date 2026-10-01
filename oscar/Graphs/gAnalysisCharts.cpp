/* Sleep Analysis Overview Charts
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "Graphs/gAnalysisCharts.h"

#include "mainwindow.h"
#include "SleepLib/analysis/analysis_channels.h"
#include "SleepLib/analysis/analysis_service.h"
#include "SleepLib/analysis/oxi_analyzer.h"
#include "SleepLib/profiles.h"

extern MainWindow *mainwin;

namespace {

AnalysisDailyData rowFor(Day *day)
{
    analysis::AnalysisService *service = mainwin ? mainwin->analysisService() : nullptr;
    return service && day ? service->row(day->date()) : AnalysisDailyData();
}

// Seconds of SpO2 below \a threshold, from a stored histogram (seconds per whole %).
int secondsBelow(const QVector<int> &hist, double threshold)
{
    int s = 0;
    for (int i = 0; i < hist.size(); ++i) {
        if (analysis::kSpo2HistMin + i < threshold) s += hist[i];
    }
    return s;
}

QColor rangeColor(int index, int count)
{
    // green for the highest range through orange to red for the lowest
    const double f = count > 1 ? double(index) / (count - 1) : 0;
    return QColor::fromHsvF(0.33 * (1 - f), 0.75, 0.85);
}

QString num(double v, int decimals = 1)
{
    return QString::number(v, 'f', decimals);
}

} // namespace

gAnalysisChart::gAnalysisChart(Kind kind)
    : gSummaryChart(code(kind), kind == Ahi || kind == HypoxicBurden || kind == FlowLimitation ? MT_CPAP : MT_UNKNOWN),
      m_kind(kind),
      m_deviceCalc(NoChannel, ST_CNT, Qt::black)
{
    addCalc(NoChannel, ST_CNT, QColor(0x30, 0x60, 0xa0));
    m_deviceCalc.reset(0, 0);
}

QList<gAnalysisChart::Kind> gAnalysisChart::kinds()
{
    return { Ahi, Odi, Spo2Ranges, ProblemZones, HypoxicBurden, FlowLimitation, PulseRises };
}

QString gAnalysisChart::code(Kind kind)
{
    switch (kind) {
    case Ahi: return QStringLiteral("AnalysisAHI");
    case Odi: return QStringLiteral("AnalysisODI");
    case Spo2Ranges: return QStringLiteral("AnalysisSpO2Ranges");
    case ProblemZones: return QStringLiteral("AnalysisZones");
    case HypoxicBurden: return QStringLiteral("AnalysisHB");
    case FlowLimitation: return QStringLiteral("AnalysisFL");
    case PulseRises: break;
    }
    return QStringLiteral("AnalysisPulseRises");
}

QString gAnalysisChart::title(Kind kind)
{
    switch (kind) {
    case Ahi: return QObject::tr("AHI (analysis)");
    case Odi: return QObject::tr("ODI 3% / 4% (analysis)");
    case Spo2Ranges: return QObject::tr("SpO2 Time in Ranges");
    case ProblemZones: return QObject::tr("Oximetry Problem Zones");
    case HypoxicBurden: return QObject::tr("Hypoxic Burden (approx.)");
    case FlowLimitation: return QObject::tr("Flow Limitation (analysis)");
    case PulseRises: break;
    }
    return QObject::tr("Pulse Rise Index");
}

QString gAnalysisChart::units(Kind kind)
{
    switch (kind) {
    case Ahi: return QObject::tr("Events/hour\n(analysis)");
    case Odi: return QObject::tr("Desaturations\nper hour");
    case Spo2Ranges: return QObject::tr("% of time");
    case ProblemZones: return QObject::tr("% of time");
    case HypoxicBurden: return QObject::tr("%·min/h");
    case FlowLimitation: return QObject::tr("% of time");
    case PulseRises: break;
    }
    return QObject::tr("Rises/hour");
}

void gAnalysisChart::preCalc()
{
    gSummaryChart::preCalc();
    m_deviceCalc.reset(idx_end - idx_start, midcalc);
    m_rangeTotals.clear();
    m_rangeSeconds = 0;
}

void gAnalysisChart::populate(Day *day, int idx)
{
    const AnalysisDailyData r = rowFor(day);
    if (!r.id) return;

    QVector<SummaryChartSlice> slices;
    QString tip;
    float weight = 0;
    SummaryCalcItem *calc = &calcitems[0];
    auto add = [&slices, calc](float value, float height, const QString &name, const QColor &color) {
        if (height > 0) slices.append(SummaryChartSlice(calc, value, height, name, color));
    };

    switch (m_kind) {
    case Ahi: {
        if (!r.hasFlow || r.flowSeconds <= 0) return;
        weight = r.flowSeconds / 3600.0f;
        const struct { int n; ChannelID code; } parts[] = {
            { r.nObstructiveApnea, AN_ObstructiveApnea }, { r.nCentralApnea, AN_CentralApnea }, { r.nApnea, AN_Apnea },
            { r.nObstructiveHypopnea, AN_ObstructiveHypopnea }, { r.nCentralHypopnea, AN_CentralHypopnea }, { r.nHypopnea, AN_Hypopnea },
        };
        float total = 0;
        QStringList detail;
        for (const auto &p : parts) {
            const float v = p.n / weight;
            total += v;
            add(v, v, schema::channel[p.code].label(), schema::channel[p.code].defaultColor());
            if (p.n) detail << QStringLiteral("%1 %2").arg(schema::channel[p.code].label(), num(v, 2));
        }
        m_device[idx] = day->calcAHI();
        tip = QObject::tr("\nAHI (analysis): %1 (%2)\nAHI (device): %3").arg(num(total, 2), detail.join(QStringLiteral(", ")), num(m_device[idx], 2));
        break;
    }
    case Odi: {
        if (!r.hasOximetry || r.oxiSeconds <= 0) return;
        weight = r.oxiSeconds / 3600.0f;
        const float odi3 = r.nDesat3 / weight, odi4 = r.nDesat4 / weight;
        add(odi4, odi4, QObject::tr("ODI 4%"), QColor(0x20, 0x60, 0xb0));
        add(odi3, odi3 - odi4, QObject::tr("ODI 3%"), QColor(0x80, 0xb0, 0xe0));
        tip = QObject::tr("\nODI 3%: %1\nODI 4%: %2").arg(num(odi3, 2), num(odi4, 2));
        break;
    }
    case Spo2Ranges: {
        if (!r.hasOximetry || r.oxiSeconds <= 0) return;
        weight = r.oxiSeconds / 3600.0f;
        // Shares of the night, so short and long nights compare; the tooltip adds the minutes.
        const QList<double> t = p_profile->analysis->spo2Thresholds();
        const QVector<RangeShare> shares = spo2RangeShares(r.spo2Hist, r.oxiSeconds, t);
        QStringList detail;
        for (const RangeShare &s : shares) {
            add(s.percent, s.percent, s.name, rangeColor(s.colorIndex, t.size() + 1));
            if (s.seconds > 0) {
                detail.prepend(QObject::tr("%1: %2% (%3 min)").arg(s.name, num(s.percent), num(s.seconds / 60.0, 0)));
            }
        }
        m_ranges[idx] = shares;
        tip = QStringLiteral("\n") + QObject::tr("Recorded: %1 min").arg(num(r.oxiSeconds / 60.0, 0))
            + QStringLiteral("\n") + detail.join(QStringLiteral("\n"));
        break;
    }
    case ProblemZones: {
        if (!r.hasOximetry) return;
        weight = r.oxiSeconds / 3600.0f;
        const QPair<double, double> share = problemZoneShares(r.zoneSeconds, r.zoneSevereSeconds, r.oxiSeconds);
        add(share.second, share.second, QObject::tr("Marked"), QColor(0xd0, 0x40, 0x30));
        add(share.first, share.first - share.second, QObject::tr("Moderate"), QColor(0xf0, 0xb0, 0x60));
        tip = QStringLiteral("\n") + QObject::tr("Problem zones: %1, %2% of the time (%3 min), marked %4% (%5 min)")
                  .arg(r.nZones).arg(num(share.first), num(r.zoneSeconds / 60.0, 0),
                                     num(share.second), num(r.zoneSevereSeconds / 60.0, 0));
        break;
    }
    case HypoxicBurden: {
        if (!r.hasFlow || !r.hasOximetry || !r.hasCpap || r.flowSeconds <= 0) return;
        weight = r.flowSeconds / 3600.0f;
        const float hb = r.linkedDesatArea / 60.0 / weight;
        slices.append(SummaryChartSlice(calc, hb, hb, title(HypoxicBurden), QColor(0x90, 0x50, 0xa0)));
        tip = QObject::tr("\nHypoxic burden (approx.): %1 %·min/h").arg(num(hb));
        break;
    }
    case FlowLimitation: {
        if (!r.hasFlow || r.flowSeconds <= 0 || r.flBreaths <= 0) return;   // not scored below 10 Hz
        weight = r.flowSeconds / 3600.0f;
        const float pct = 100.0f * r.flSeconds / r.flowSeconds;
        slices.append(SummaryChartSlice(calc, pct, pct, title(FlowLimitation), QColor(0x70, 0x70, 0x70)));
        tip = QObject::tr("\nFlow limitation: %1% of the time").arg(num(pct));
        break;
    }
    case PulseRises: {
        if (!r.hasPulse || r.pulseSeconds <= 0) return;
        weight = r.pulseSeconds / 3600.0f;
        const float index = r.nPulseRise / weight;
        slices.append(SummaryChartSlice(calc, index, index, title(PulseRises), QColor(0xe0, 0x60, 0x60)));
        tip = QObject::tr("\nPulse rises: %1 per hour").arg(num(index));
        break;
    }
    }
    // A night the chart applies to but with nothing to count: a zero, not a gap.
    if (slices.isEmpty()) slices.append(SummaryChartSlice(calc, 0, 0, title(m_kind), Qt::transparent));
    cache[idx] = slices;
    m_weight[idx] = weight;
    m_tooltip[idx] = tip;
}

void gAnalysisChart::customCalc(Day *day, QVector<SummaryChartSlice> &slices)
{
    if (slices.isEmpty() || !day) return;
    const int idx = dayindex.value(day->date(), -1);
    float total = 0;
    for (const SummaryChartSlice &s : slices) total += s.height;
    if (m_kind == Odi && !slices.isEmpty()) total = slices.last().value;   // ODI 3 %, which ODI 4 % is part of
    calcitems[0].update(total, m_weight.value(idx));
    if (m_kind == Ahi && m_device.contains(idx)) m_deviceCalc.update(m_device.value(idx), day->hours(MT_CPAP));
    if (m_kind == Spo2Ranges && m_ranges.contains(idx)) {
        for (const RangeShare &s : m_ranges.value(idx)) {
            RangeShare &total = m_rangeTotals[s.colorIndex];
            total.name = s.name;
            total.colorIndex = s.colorIndex;
            total.seconds += s.seconds;
            m_rangeSeconds += s.seconds;
        }
    }
}

QString gAnalysisChart::tooltipData(Day *, int idx)
{
    return m_tooltip.value(idx);
}

void gAnalysisChart::afterDraw(QPainter &, gGraph &graph, QRectF rect)
{
    SummaryCalcItem &calc = calcitems[0];
    if (calc.cnt == 0) return;
    if (m_kind == Spo2Ranges) {
        // Every bar is the whole night (100 %), so show each range's share of all the shown nights.
        QStringList parts;
        for (auto it = m_rangeTotals.cbegin(); it != m_rangeTotals.cend(); ++it) {   // highest range first
            if (it->seconds > 0 && m_rangeSeconds > 0) {
                parts << QStringLiteral("%1: %2%").arg(it->name, num(100.0 * it->seconds / m_rangeSeconds));
            }
        }
        graph.renderText(parts.join(QStringLiteral("   ")), rect.left(), rect.top() - 5 * graph.printScaleY(), 0);
        return;
    }
    const QString midName = midcalc == 0 ? QObject::tr("Med.") : midcalc == 1 ? QObject::tr("W-Avg") : QObject::tr("Avg");
    QString txt = QObject::tr("Min: %1  %2: %3  Max: %4").arg(num(calc.min, 2), midName, num(calc.mid(), 2), num(calc.max, 2));
    if (m_kind == Ahi && m_deviceCalc.cnt > 0) {
        txt += QStringLiteral("   ") + QObject::tr("Device %1: %2").arg(midName, num(m_deviceCalc.mid(), 2));
    }
    graph.renderText(txt, rect.left(), rect.top() - 5 * graph.printScaleY(), 0);
}

QVector<gAnalysisChart::RangeShare> gAnalysisChart::spo2RangeShares(const QVector<int> &hist, int oxiSeconds,
                                                                      const QList<double> &t)
{
    QVector<RangeShare> shares;
    // from the lowest range up, so the worst time sits at the bottom of the bar
    for (int i = t.size(); i >= 0; --i) {
        const int below = i < t.size() ? secondsBelow(hist, t[i]) : 0;
        const int upto = i > 0 ? secondsBelow(hist, t[i - 1]) : oxiSeconds;
        RangeShare s;
        s.name = i == t.size() ? QObject::tr("< %1%").arg(t.last())
               : i == 0 ? QObject::tr(">= %1%").arg(t.first())
               : QStringLiteral("%1-%2%").arg(t[i]).arg(t[i - 1]);
        s.seconds = qMax(0, upto - below);
        s.percent = oxiSeconds > 0 ? 100.0 * s.seconds / oxiSeconds : 0;
        s.colorIndex = i;
        shares.append(s);
    }
    return shares;
}

QPair<double, double> gAnalysisChart::problemZoneShares(int zoneSeconds, int markedSeconds, int oxiSeconds)
{
    if (oxiSeconds <= 0) return { 0, 0 };
    return { 100.0 * zoneSeconds / oxiSeconds, 100.0 * markedSeconds / oxiSeconds };
}

