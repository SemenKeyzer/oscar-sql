/* Sleep Analysis Overview Charts
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "Graphs/gAnalysisCharts.h"

#include <cmath>

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

// ColorBrewer YlOrRd, without its palest step (too faint on white): a sequence that
// reads by lightness, so it holds for colour-blind eyes and in greyscale prints.
const QColor kLowSpo2[] = { QColor(0xfe, 0xd9, 0x76), QColor(0xfe, 0xb2, 0x4c), QColor(0xfd, 0x8d, 0x3c),
                            QColor(0xfc, 0x4e, 0x2a), QColor(0xe3, 0x1a, 0x1c), QColor(0xb1, 0x00, 0x26) };
constexpr int kLowSpo2Count = int(sizeof(kLowSpo2) / sizeof(kLowSpo2[0]));

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
    case Spo2Ranges: {
        const QList<double> t = p_profile && p_profile->analysis ? p_profile->analysis->spo2Thresholds() : QList<double>();
        return t.isEmpty() ? QObject::tr("% of time") : QObject::tr("% of time\nbelow %1%").arg(t.first());
    }
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
        // The time below the highest threshold as a share of the night, so short and long
        // nights compare, stacked by range with the lowest at the bottom. The time above it,
        // most of a good night, would only flatten the ranges that matter: the tooltip
        // gives it.
        const QList<double> t = p_profile->analysis->spo2Thresholds();
        const QVector<RangeShare> shares = spo2RangeShares(r.spo2Hist, r.oxiSeconds, t);
        QStringList detail;
        int below = 0;
        for (const RangeShare &s : shares) {
            if (s.colorIndex > 0) {
                add(s.percent, s.percent, s.name, rangeColor(s.colorIndex, t.size() + 1));
                below += s.seconds;
            }
            if (s.seconds > 0) {
                detail.prepend(QObject::tr("%1: %2% (%3 min)").arg(s.name, num(s.percent), num(s.seconds / 60.0, 0)));
            }
        }
        m_ranges[idx] = shares;
        tip = QStringLiteral("\n") + QObject::tr("Recorded: %1 min").arg(num(r.oxiSeconds / 60.0, 0))
            + QStringLiteral("\n") + QObject::tr("Below %1%: %2% of the time (%3 min)")
                  .arg(t.first()).arg(num(100.0 * below / r.oxiSeconds), num(below / 60.0, 0))
            + QStringLiteral("\n") + detail.join(QStringLiteral("\n"));
        break;
    }
    case ProblemZones: {
        if (!r.hasOximetry) return;
        weight = r.oxiSeconds / 3600.0f;
        const QPair<double, double> share = problemZoneShares(r.zoneSeconds, r.zoneSevereSeconds, r.oxiSeconds);
        add(share.second, share.second, QObject::tr("Marked"), kLowSpo2[4]);
        add(share.first, share.first - share.second, QObject::tr("Moderate"), kLowSpo2[1]);
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

float gAnalysisChart::overlayPeak(int idx)
{
    return m_kind == Ahi ? m_device.value(idx) : 0;
}

void gAnalysisChart::drawBarOverlay(QPainter &painter, int idx, const QRectF &column, float miny, float ymult)
{
    // The device's AHI as a dark mark across the analysis' bar, so the two read together.
    if (m_kind != Ahi || !m_device.contains(idx) || column.width() < 3) return;
    const double y = column.bottom() - (m_device.value(idx) - miny) * ymult;
    if (y < column.top()) return;
    painter.save();
    painter.setPen(QPen(QColor(0x20, 0x20, 0x20), 2));
    painter.drawLine(QPointF(column.left(), y), QPointF(column.right(), y));
    painter.restore();
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
        // The share of all the shown nights below the highest threshold, then in each range.
        graph.setUnits(units(m_kind));   // follows the thresholds when they change
        if (m_rangeSeconds <= 0) return;
        QStringList parts;
        qint64 below = 0;
        for (auto it = m_rangeTotals.cbegin(); it != m_rangeTotals.cend(); ++it) {   // highest range first
            if (it.key() == 0) continue;
            below += it->seconds;
            if (it->seconds > 0) parts << QStringLiteral("%1: %2%").arg(it->name, num(100.0 * it->seconds / m_rangeSeconds));
        }
        const QList<double> t = p_profile->analysis->spo2Thresholds();
        parts.prepend(QObject::tr("Below %1%: %2%").arg(t.first()).arg(num(100.0 * below / m_rangeSeconds)));
        graph.renderText(parts.join(QStringLiteral("   ")), rect.left(), rect.top() - 5 * graph.printScaleY(), 0);
        return;
    }
    const QString midName = midcalc == 0 ? QObject::tr("Med.") : midcalc == 1 ? QObject::tr("W-Avg") : QObject::tr("Avg");
    QString txt = QObject::tr("Min: %1  %2: %3  Max: %4").arg(num(calc.min, 2), midName, num(calc.mid(), 2), num(calc.max, 2));
    if (m_kind == Ahi && m_deviceCalc.cnt > 0) {
        txt += QStringLiteral("   ") + QObject::tr("Device (dark mark) %1: %2").arg(midName, num(m_deviceCalc.mid(), 2));
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
        s.name = spo2RangeLabel(i < t.size() ? t[i] : -1, i > 0 ? t[i - 1] : 101);
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

QString gAnalysisChart::spo2RangeLabel(double lower, double upper)
{
    if (lower < 0) return QStringLiteral("< %1 %").arg(upper);
    if (upper > 100) return QStringLiteral("%1 %2 %").arg(QChar(0x2265)).arg(lower);   // ≥
    // Readings are whole %: 90 up to (not including) 94 is 90, 91, 92 and 93.
    const int lo = int(std::ceil(lower)), hi = int(std::ceil(upper)) - 1;
    if (lo == hi) return QStringLiteral("%1 %").arg(lo);
    if (lo < hi) return QStringLiteral("%1%2%3 %").arg(lo).arg(QChar(0x2013)).arg(hi);   // en dash
    return QStringLiteral("%1%2<%3 %").arg(lower).arg(QChar(0x2013)).arg(upper);       // no whole reading inside
}

QColor gAnalysisChart::rangeColor(int index, int count)
{
    if (index <= 0) return QColor(0x9e, 0xca, 0xe1);   // the highest range: a calm blue
    const int below = count - 1;                         // ranges below the highest threshold
    const int step = below > 1 ? qRound(double(index - 1) * (kLowSpo2Count - 1) / (below - 1)) : kLowSpo2Count - 1;
    return kLowSpo2[qBound(0, step, kLowSpo2Count - 1)];
}
