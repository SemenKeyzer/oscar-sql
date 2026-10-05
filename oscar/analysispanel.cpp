/* Sleep Analysis Panel
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysispanel.h"
#include "SleepLib/profiles.h"

#include <QCheckBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "Graphs/gAnalysisCharts.h"
#include "SleepLib/day.h"
#include "SleepLib/machine_common.h"
#include "SleepLib/schema.h"

using namespace analysis;

namespace {

const QString kTable = QStringLiteral("<table cellspacing=0 cellpadding=1 border=0 width='100%'>");

QString number(double v, int decimals = 1)
{
    return QString::number(v, 'f', decimals);
}

// Events per hour, or a dash without hours.
QString perHour(double count, double hours)
{
    return hours > 0 ? number(count / hours) : QStringLiteral("&mdash;");
}

QString row(const QString &label, const QString &value)
{
    return QStringLiteral("<tr><td>%1</td><td align=right>%2</td></tr>").arg(label, value);
}

QString row3(const QString &label, const QString &device, const QString &analysisValue)
{
    return QStringLiteral("<tr><td>%1</td><td align=right>%2</td><td align=right>%3</td></tr>").arg(label, device, analysisValue);
}

QString wide(const QString &text)
{
    return QStringLiteral("<tr><td colspan=3>%1</td></tr>").arg(text);
}

QString clock(qint64 t)
{
    return QDateTime::fromMSecsSinceEpoch(t).toString(QStringLiteral("HH:mm:ss"));
}

QString eventLabel(RespEvent type)
{
    switch (type) {
    case RespEvent::ObstructiveApnea: return QStringLiteral("OA");
    case RespEvent::CentralApnea: return QStringLiteral("CA");
    case RespEvent::Apnea: return QStringLiteral("A");
    case RespEvent::ObstructiveHypopnea: return QStringLiteral("OH");
    case RespEvent::CentralHypopnea: return QStringLiteral("CH");
    case RespEvent::Hypopnea: return QStringLiteral("H");
    case RespEvent::Rera: break;
    }
    return QStringLiteral("RERA");
}

// The Glasgow Index (DaveSkvn's), original and adapted, with its nine signs.
QString glasgowHtml(const DayResult &r)
{
    const QString dash = QStringLiteral("&mdash;");
    auto value = [&dash](double v) { return std::isnan(v) ? dash : number(v, 2); };
    if (!r.flScored) {
        return QStringLiteral("<p><b>%1</b>: &mdash; <font size=-1>%2</font></p>")
            .arg(AnalysisPanel::tr("Glasgow Index"), AnalysisPanel::tr("Not computed: the flow is recorded below 10 Hz."));
    }
    QString html = kTable;
    html += QStringLiteral("<tr><td><b>%1</b></td><td align=right>%2</td><td align=right>%3</td></tr>")
                .arg(AnalysisPanel::tr("Glasgow Index"), AnalysisPanel::tr("original"), AnalysisPanel::tr("adapted"));
    html += row3(AnalysisPanel::tr("Index"), value(r.glasgow.index()), value(r.glasgowAdapted.index()));
    if (!r.glasgow.isEmpty() || !r.glasgowAdapted.isEmpty()) {
        const QString names[GiComponentCount] = {
            AnalysisPanel::tr("Skew"), AnalysisPanel::tr("Spike"), AnalysisPanel::tr("Flat top"),
            AnalysisPanel::tr("Top heavy (not in the sum)"), AnalysisPanel::tr("Double peak"), AnalysisPanel::tr("No pause"),
            AnalysisPanel::tr("Inspiration rate"), AnalysisPanel::tr("Double inspiration"), AnalysisPanel::tr("Variable amplitude"),
        };
        for (int k = 0; k < GiComponentCount; ++k) {
            html += row3(QStringLiteral("<font size=-1>&nbsp;&nbsp;%1</font>").arg(names[k]),
                         QStringLiteral("<font size=-1>%1</font>").arg(value(r.glasgow.fraction(GlasgowComponent(k)))),
                         QStringLiteral("<font size=-1>%1</font>").arg(value(r.glasgowAdapted.fraction(GlasgowComponent(k)))));
        }
    }
    html += QStringLiteral("</table>");
    html += QStringLiteral("<p><font size=-1>%1</font></p>").arg(
        AnalysisPanel::tr("Author's scale: 0–0.2 clean breathing, about 3 serious problems. Experimental, not reviewed by physicians."));
    return html;
}

} // namespace

QString AnalysisPanel::disclaimer()
{
    return tr("Experimental analysis for self-review. It is not a medical diagnosis; discuss therapy changes with your clinician.");
}

QString AnalysisPanel::duration(qint64 ms)
{
    const qint64 s = (ms + 500) / 1000;
    if (s >= 3600) return QStringLiteral("%1h %2m").arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1m %2s").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString AnalysisPanel::offset(qint64 ms)
{
    const qint64 s = (std::llabs(ms) + 500) / 1000;
    return QStringLiteral("%1%2:%3:%4").arg(ms < 0 ? QStringLiteral("-") : QStringLiteral("+"))
        .arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString AnalysisPanel::sidebarHtml(Day *day, const DayResult &r, const QString &oxiSource,
                                   const QList<double> &spo2Thresholds)
{
    QString html;
    html += QStringLiteral("<p><i><font size=-1>%1</font></i></p>").arg(disclaimer().toHtmlEscaped());

    // --- breathing: the device next to the analysis
    const double analysisHours = r.flowSeconds / 3600.0;
    if (r.hasFlow) {
        const double deviceHours = day ? day->hours(MT_CPAP) : 0;
        auto dev = [day](ChannelID code) { return day ? double(day->count(code)) : 0.0; };
        const double devApneas = dev(CPAP_Obstructive) + dev(CPAP_ClearAirway) + dev(CPAP_Apnea) + dev(CPAP_AllApnea);
        const double devHypopneas = dev(CPAP_Hypopnea) + dev(CPAP_ObstructiveHypopnea) + dev(CPAP_CentralHypopnea);
        const int oa = r.count(RespEvent::ObstructiveApnea), ca = r.count(RespEvent::CentralApnea), a = r.count(RespEvent::Apnea);
        const int oh = r.count(RespEvent::ObstructiveHypopnea), ch = r.count(RespEvent::CentralHypopnea), h = r.count(RespEvent::Hypopnea);

        html += kTable;
        html += QStringLiteral("<tr><td></td><td align=right><b>%1</b></td><td align=right><b>%2</b></td></tr>")
                    .arg(tr("Device"), tr("Analysis"));
        html += row3(tr("AHI"), day ? number(day->calcAHI()) : QStringLiteral("&mdash;"), perHour(r.apneas.size() + r.hypopneas.size(), analysisHours));
        html += row3(tr("Apneas /h"), perHour(devApneas, deviceHours), perHour(r.apneas.size(), analysisHours));
        html += row3(QStringLiteral("&nbsp;&nbsp;OA &middot; CA &middot; A"),
                     QStringLiteral("%1 &middot; %2 &middot; %3").arg(dev(CPAP_Obstructive)).arg(dev(CPAP_ClearAirway)).arg(dev(CPAP_Apnea) + dev(CPAP_AllApnea)),
                     QStringLiteral("%1 &middot; %2 &middot; %3").arg(oa).arg(ca).arg(a));
        html += row3(tr("Hypopneas /h"), perHour(devHypopneas, deviceHours), perHour(r.hypopneas.size(), analysisHours));
        html += row3(QStringLiteral("&nbsp;&nbsp;OH &middot; CH &middot; H"),
                     QStringLiteral("%1 &middot; %2 &middot; %3").arg(dev(CPAP_ObstructiveHypopnea)).arg(dev(CPAP_CentralHypopnea)).arg(dev(CPAP_Hypopnea)),
                     QStringLiteral("%1 &middot; %2 &middot; %3").arg(oh).arg(ch).arg(h));
        const bool deviceRera = day && day->channelExists(CPAP_RERA);
        html += row3(tr("RERA /h"), deviceRera ? perHour(dev(CPAP_RERA), deviceHours) : QStringLiteral("&mdash;"),
                     perHour(r.reras.size(), analysisHours));
        const bool deviceFl = day && day->channelExists(CPAP_FlowLimit);
        html += row3(tr("Flow limitation, % time"), deviceFl ? number(day->calcPON(CPAP_FlowLimit)) : QStringLiteral("&mdash;"),
                     r.flScored && r.flowSeconds > 0 ? number(100.0 * r.flSeconds / r.flowSeconds) : QStringLiteral("&mdash;"));
        const bool devicePb = day && (day->channelExists(CPAP_CSR) || day->channelExists(CPAP_PB));
        html += row3(tr("Periodic breathing, % time"),
                     devicePb ? number(day->calcPON(CPAP_CSR) + day->calcPON(CPAP_PB)) : QStringLiteral("&mdash;"),
                     r.flowSeconds > 0 ? number(100.0 * r.pbSeconds / r.flowSeconds) : QStringLiteral("&mdash;"));
        html += row3(tr("Hours"), number(deviceHours, 2), number(analysisHours, 2));
        html += QStringLiteral("</table>");

        if (r.flScored && r.flBreaths > 0) {
            html += QStringLiteral("<p>%1</p>").arg(r.flLimitedBreaths >= 0
                ? tr("Flow limitation: %1, longest run %2; %3% of breaths")
                      .arg(duration(1000LL * r.flSeconds), duration(1000LL * r.flLongestSeconds),
                           number(100.0 * r.flLimitedBreaths / r.flBreaths, 0))
                : tr("Flow limitation: %1, longest run %2").arg(duration(1000LL * r.flSeconds), duration(1000LL * r.flLongestSeconds)));
        }
        html += glasgowHtml(r);

        html += QStringLiteral("<p>%1: AASM 3 % %2 &middot; CMS 4 % %3 &middot; %4 %5</p>")
                    .arg(tr("Hypopnea index by rule"), perHour(r.hypopneasAasm3, analysisHours),
                         perHour(r.hypopneasCms4, analysisHours), tr("Flow only"), perHour(r.hypopneasFlowOnly, analysisHours));

        if (r.hasComparison) {
            const MatchResult &m = r.match;
            html += QStringLiteral("<p>%1</p>").arg(
                tr("Agreement with the device: %1% (matched %2, device only %3, analysis only %4, different type %5).")
                    .arg(number(100.0 * m.agreement(), 0)).arg(m.matched.size()).arg(m.deviceOnly.size())
                    .arg(m.analysisOnly.size()).arg(m.typeMismatch)
                + QStringLiteral(" <a href='analysis=differences'>%1</a>").arg(tr("Show differences")));
        }
    }

    // --- oximetry
    const OxiResult &o = r.oxi;
    if (r.hasOximetry) {
        const double oxiHours = o.spo2Seconds / 3600.0;
        html += kTable;
        html += wide(QStringLiteral("<b>%1</b>").arg(tr("Oximetry")));
        html += row(tr("SpO2 mean / nadir"), QStringLiteral("%1 % / %2 %").arg(number(o.spo2Sum / qMax(1, o.spo2Seconds)))
                                                   .arg(number(o.spo2Nadir, 0)));
        html += row(tr("ODI 3% / 4%"), QStringLiteral("%1 / %2").arg(perHour(o.countDesaturations(3), oxiHours),
                                                                       perHour(o.countDesaturations(4), oxiHours)));
        if (!o.desaturations.isEmpty()) {
            double depth = 0, ms = 0;
            for (const Desaturation &d : o.desaturations) {
                depth += d.depth();
                ms += d.end - d.start;
            }
            const int n = o.desaturations.size();
            html += row(tr("Mean desaturation"), QStringLiteral("%1 %, %2 s").arg(number(depth / n)).arg(number(ms / n / 1000.0, 0)));
        }
        html += QStringLiteral("</table>");

        // time below each threshold, then in each range between them
        html += kTable;
        html += QStringLiteral("<tr><td><b>%1</b></td><td align=right><b>%2</b></td><td align=right><b>%3</b></td></tr>")
                    .arg(tr("SpO2"), tr("min"), tr("% time"));
        for (double t : spo2Thresholds) {
            const int s = o.spo2SecondsBelow(t);
            html += row3(tr("below %1 %").arg(t), number(s / 60.0), number(100.0 * s / qMax(1, o.spo2Seconds)));
        }
        html += QStringLiteral("</table>");
        if (!spo2Thresholds.isEmpty()) {
            // the ranges as the Overview's SpO2 chart names and counts them, highest first
            html += kTable;
            const QVector<gAnalysisChart::RangeShare> shares = gAnalysisChart::spo2RangeShares(o.spo2Hist, o.spo2Seconds, spo2Thresholds);
            for (auto it = shares.crbegin(); it != shares.crend(); ++it) {
                html += row3(it->name.toHtmlEscaped(), number(it->seconds / 60.0), number(it->percent));
            }
            html += QStringLiteral("</table>");
        }

        html += kTable;
        html += row(tr("Problem zones"), o.zones.isEmpty() ? tr("none")
                    : tr("%1, %2 (marked %3)").arg(o.zones.size()).arg(duration(1000LL * o.zoneSeconds(1)), duration(1000LL * o.zoneSeconds(2))));
        if (r.hasCpap && r.hasFlow) {
            html += row(tr("Hypoxic burden (approx.)"), analysisHours > 0
                        ? QStringLiteral("%1 %&middot;min/h").arg(number(r.linkedDesatArea / 60.0 / analysisHours)) : QStringLiteral("&mdash;"));
            html += row(tr("Unexplained desaturations"), QString::number(r.unexplained.size()));
        }
        html += row(tr("SpO2 source"), oxiSource.toHtmlEscaped() + (r.oxiScope == QLatin1String("cpap") ? tr(" (CPAP time only)") : QString()));
        html += QStringLiteral("</table>");
    }

    // --- pulse
    if (r.hasPulse) {
        const double pulseHours = o.pulseSeconds / 3600.0;
        html += kTable;
        html += wide(QStringLiteral("<b>%1</b>").arg(tr("Pulse")));
        html += row(tr("Mean / min / max"), QStringLiteral("%1 / %2 / %3").arg(number(o.pulseSum / qMax(1, o.pulseSeconds), 0))
                                                   .arg(number(o.pulseMin, 0), number(o.pulseMax, 0)));
        html += row(tr("Pulse rises /h"), perHour(o.pulseRises.size(), pulseHours));
        if (r.dhrEvents > 0) {
            html += row(tr("Pulse response to events"), tr("+%1 bpm (%2 events)").arg(number(r.dhrSum / r.dhrEvents)).arg(r.dhrEvents));
        }
        if (o.bradySeconds() > 0 || o.tachySeconds() > 0) {
            html += row(tr("Low / high pulse"), QStringLiteral("%1 / %2").arg(duration(1000LL * o.bradySeconds()), duration(1000LL * o.tachySeconds())));
        }
        html += QStringLiteral("</table>");
    }

    // --- notes
    QStringList notes;
    if (r.hasCpap && !r.hasFlow) {
        notes << (r.flowRateHz > 0 ? tr("Flow analysis unavailable (sample rate %1 Hz).").arg(number(r.flowRateHz))
                                   : tr("Flow analysis unavailable: no flow data."));
    }
    if (r.hasFlow) {
        switch (r.rule) {
        case HypopneaRule::Auto:
            notes << tr("Hypopneas: AASM 3 % where the oximeter covers the event, flow only elsewhere (Auto).");
            break;
        case HypopneaRule::Aasm3:
            notes << tr("Hypopneas: AASM 3 %.");
            break;
        case HypopneaRule::Cms4:
            notes << tr("Hypopneas: CMS 4 %.");
            break;
        case HypopneaRule::FlowOnly:
            notes << tr("Hypopneas: flow only.");
            break;
        }
        QString flow = tr("Flow recorded at %1 Hz.").arg(number(r.flowRateHz, 0));
        if (!r.flScored) flow += QLatin1Char(' ') + tr("Flow limitation and cardiogenic oscillations need 10 Hz: not scored.");
        notes << flow;
        if (r.unscoreableSeconds > 0) notes << tr("Unscoreable time: %1 (leaks, gaps, weak signal).").arg(duration(1000LL * r.unscoreableSeconds));
        if (!r.hasOximetry) notes << tr("No oximetry: hypopneas are scored from the flow only.");
        else if (r.unconfirmable > 0 && r.rule != HypopneaRule::FlowOnly) {
            notes << tr("%n candidate(s) without SpO2 at the time, scored from the flow only.", "", r.unconfirmable);
        }
    }
    if (r.hasOffsetHint) {
        notes << tr("Oximeter data may be offset by about %1.").arg(offset(r.offsetHintMs))
                 + QStringLiteral(" <a href='align=oximeter'>%1</a>").arg(tr("Align oximeter..."));
    }
    for (const QString &n : notes) html += QStringLiteral("<p><font size=-1>%1</font></p>").arg(n);
    return html;
}

AnalysisTab::AnalysisTab(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    // step through the differences without hunting for them in the tree
    auto *stepper = new QHBoxLayout;
    m_prev = new QPushButton(tr("< Previous"), this);
    m_next = new QPushButton(tr("Next >"), this);
    m_prev->setToolTip(tr("The previous difference from the device"));
    m_next->setToolTip(tr("The next difference from the device"));
    m_position = new QLabel(this);
    stepper->addWidget(m_prev);
    stepper->addWidget(m_next);
    stepper->addWidget(m_position, 1);
    layout->addLayout(stepper);
    connect(m_prev, &QPushButton::clicked, this, [this]() { stepDifference(-1); });
    connect(m_next, &QPushButton::clicked, this, [this]() { stepDifference(1); });
    auto *onFlow = new QCheckBox(tr("Show the differences on the flow graph"), this);
    onFlow->setToolTip(tr("Marks where the device and the analysis disagree: amber, the device only; "
                          "purple, the analysis only; yellow, a different type of event."));
    onFlow->setChecked(!p_profile || p_profile->analysis->showFlowDifferences());
    layout->addWidget(onFlow);
    connect(onFlow, &QCheckBox::toggled, this, [this](bool on) {
        if (p_profile) p_profile->analysis->setShowFlowDifferences(on);
        emit showOnFlowChanged(on);
    });
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(1);
    m_tree->header()->hide();
    layout->addWidget(m_tree);
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) { onItemClicked(item); });
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) { onItemClicked(item); });
    updateStepper();
}

void AnalysisTab::clear()
{
    m_tree->clear();
    m_differences.clear();
    m_current = -1;
    updateStepper();
}

void AnalysisTab::stepDifference(int step)
{
    if (m_differences.isEmpty()) return;
    m_current = m_current < 0 ? (step > 0 ? 0 : int(m_differences.size()) - 1)
                              : qBound(0, m_current + step, int(m_differences.size()) - 1);
    QTreeWidgetItem *item = m_differences.at(m_current);
    m_tree->setCurrentItem(item);
    m_tree->scrollToItem(item);
    onItemClicked(item);
    updateStepper();
}

void AnalysisTab::updateStepper()
{
    const int n = int(m_differences.size());
    m_prev->setEnabled(n > 0 && m_current != 0);
    m_next->setEnabled(n > 0 && m_current < n - 1);
    m_position->setText(n == 0 ? tr("No differences from the device")
                        : m_current < 0 ? tr("%n difference(s) from the device", nullptr, n)
                                        : tr("Difference %1 of %2").arg(m_current + 1).arg(n));
}

void AnalysisTab::setResult(const DayResult &r)
{
    m_tree->clear();
    m_differences.clear();
    m_current = -1;
    auto group = [this](const QString &title, int count) {
        auto *g = new QTreeWidgetItem(m_tree, QStringList(QStringLiteral("%1 (%2)").arg(title).arg(count)));
        g->setFlags(g->flags() & ~Qt::ItemIsSelectable);
        return g;
    };
    auto item = [](QTreeWidgetItem *parent, const QString &text, qint64 from, qint64 to) {
        auto *it = new QTreeWidgetItem(parent, QStringList(text));
        it->setData(0, Qt::UserRole, from);
        it->setData(0, Qt::UserRole + 1, to);
        return it;
    };

    const OxiResult &o = r.oxi;
    if (!o.zones.isEmpty()) {
        QTreeWidgetItem *g = group(tr("Problem zones"), o.zones.size());
        for (const ProblemZone &z : o.zones) {
            item(g, tr("%1, %2, min %3 %, %4 desaturations%5")
                        .arg(clock(z.start), AnalysisPanel::duration(z.end - z.start)).arg(z.minSpo2).arg(z.desaturations)
                        .arg(z.severity >= 2 ? tr(" (marked)") : QString()),
                 z.start, z.end);
        }
    }
    if (r.hasComparison) {
        // The same differences the flow graph marks (AnalysisPanel::differences()).
        const QVector<DifferenceSpan> diffs = AnalysisPanel::differences(r);
        auto count = [&diffs](DifferenceSpan::Kind k) {
            return int(std::count_if(diffs.cbegin(), diffs.cend(), [k](const DifferenceSpan &d) { return d.kind == k; }));
        };
        QTreeWidgetItem *deviceOnly = group(tr("Device only"), count(DifferenceSpan::DeviceOnly));
        QTreeWidgetItem *analysisOnly = group(tr("Analysis only"), count(DifferenceSpan::AnalysisOnly));
        QTreeWidgetItem *differentType = group(tr("Different type"), count(DifferenceSpan::DifferentType));
        for (const DifferenceSpan &d : diffs) {
            const qint64 seconds = (d.end - d.start) / 1000;
            switch (d.kind) {
            case DifferenceSpan::DeviceOnly:
                m_differences << item(deviceOnly, QStringLiteral("%1 %2, %3 s").arg(clock(d.start), d.label()).arg(seconds), d.start, d.end);
                break;
            case DifferenceSpan::AnalysisOnly:
                m_differences << item(analysisOnly, QStringLiteral("%1 %2, %3 s").arg(clock(d.start), d.label()).arg(seconds), d.start, d.end);
                break;
            case DifferenceSpan::DifferentType:
                m_differences << item(differentType, tr("%1 device %2, analysis a%3").arg(clock(d.start), d.device, d.analysis), d.start, d.end);
                break;
            }
        }
    }
    if (!r.unexplained.isEmpty()) {
        QTreeWidgetItem *g = group(tr("Unexplained desaturations"), r.unexplained.size());
        for (int i : r.unexplained) {
            const Desaturation &d = o.desaturations[i];
            item(g, QStringLiteral("%1 -%2 %, %3 s").arg(clock(d.start)).arg(d.depth()).arg((d.end - d.start) / 1000),
                 d.start, d.end);
        }
    }
    if (m_tree->topLevelItemCount() == 0) {
        auto *none = new QTreeWidgetItem(m_tree, QStringList(tr("Nothing to show for this day.")));
        none->setFlags(Qt::NoItemFlags);
    }
    m_tree->expandAll();
    std::stable_sort(m_differences.begin(), m_differences.end(), [](QTreeWidgetItem *a, QTreeWidgetItem *b) {
        return a->data(0, Qt::UserRole).toLongLong() < b->data(0, Qt::UserRole).toLongLong();
    });
    updateStepper();
}

void AnalysisTab::onItemClicked(QTreeWidgetItem *item)
{
    if (!item || item->data(0, Qt::UserRole).isNull()) return;
    const qint64 from = item->data(0, Qt::UserRole).toLongLong();
    const qint64 to = item->data(0, Qt::UserRole + 1).toLongLong();
    // the flow graph draws the difference shown stronger; anything else clears it
    if (m_differences.contains(item)) emit differenceShown(from, to);
    else emit differenceShown(0, 0);
    // some context on both sides, a minute at least
    const qint64 pad = qMax<qint64>(60000, (to - from) / 2);
    emit showRange(from - pad, to + pad);
}

QString DifferenceSpan::label() const
{
    switch (kind) {
    case DeviceOnly: return device;
    case AnalysisOnly: return QStringLiteral("a") + analysis;
    case DifferentType: break;
    }
    return device + QStringLiteral(" \u2194 a") + analysis;
}

QVector<DifferenceSpan> AnalysisPanel::differences(const DayResult &r)
{
    QVector<DifferenceSpan> out;
    if (!r.hasComparison) return out;
    const MatchResult &m = r.match;
    for (int i : m.deviceOnly) {
        const DayEvent &e = r.deviceEvents[i];
        DifferenceSpan d;
        d.kind = DifferenceSpan::DeviceOnly;
        d.start = e.start;
        d.end = e.end;
        d.device = eventLabel(e.type);
        out.append(d);
    }
    for (int j : m.analysisOnly) {
        const DayEvent &e = r.analysisEvents[j];
        DifferenceSpan d;
        d.kind = DifferenceSpan::AnalysisOnly;
        d.start = e.start;
        d.end = e.end;
        d.analysis = eventLabel(e.type);
        out.append(d);
    }
    for (const auto &pair : m.matched) {
        const DayEvent &dev = r.deviceEvents[pair.first], &an = r.analysisEvents[pair.second];
        if (groupOf(dev.type) == groupOf(an.type)) continue;
        DifferenceSpan d;
        d.kind = DifferenceSpan::DifferentType;
        d.start = qMin(dev.start, an.start);
        d.end = qMax(dev.end, an.end);
        d.device = eventLabel(dev.type);
        d.analysis = eventLabel(an.type);
        out.append(d);
    }
    std::stable_sort(out.begin(), out.end(), [](const DifferenceSpan &a, const DifferenceSpan &b) { return a.start < b.start; });
    return out;
}

