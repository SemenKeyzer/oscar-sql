/* Overview Graph Presets
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "overviewpresets.h"

#include <QCoreApplication>

#include "common_gui.h"
#include "Graphs/gAnalysisCharts.h"

namespace OverviewPresets {

namespace {

// Names given to the Overview's own graphs in Overview::CreateAllGraphs(); the channel
// graphs are named after their channel codes.
const QString kDeviceAhi = QStringLiteral("AHIBreakdown");
const QString kSessionTimes = QStringLiteral("New Session");
const QString kPressure = QStringLiteral("Pressure Settings");
const QString kTimeInApnea = QStringLiteral("TTIA");

QString analysis(gAnalysisChart::Kind kind) { return gAnalysisChart::code(kind); }

} // namespace

QList<Preset> presets()
{
    return { All, Therapy, MaskLeaks, Oxygen, Analysis };
}

QString title(Preset preset)
{
    switch (preset) {
    case All: return QCoreApplication::translate("OverviewPresets", "All");
    case Therapy: return QCoreApplication::translate("OverviewPresets", "Therapy");
    case MaskLeaks: return QCoreApplication::translate("OverviewPresets", "Mask & Leaks");
    case Oxygen: return QCoreApplication::translate("OverviewPresets", "Oxygen");
    case Analysis: break;
    }
    return QCoreApplication::translate("OverviewPresets", "Analysis");
}

QString key(Preset preset)
{
    switch (preset) {
    case All: return QStringLiteral("all");
    case Therapy: return QStringLiteral("therapy");
    case MaskLeaks: return QStringLiteral("mask");
    case Oxygen: return QStringLiteral("oxygen");
    case Analysis: break;
    }
    return QStringLiteral("analysis");
}

Preset fromKey(const QString &key)
{
    for (Preset p : presets())
        if (OverviewPresets::key(p) == key) return p;
    return All;
}

QStringList graphNames(Preset preset)
{
    switch (preset) {
    case All:
        return {};
    case Therapy:
        return { kDeviceAhi, STR_GRAPH_Usage, kSessionTimes, kPressure, kTimeInApnea,
                 analysis(gAnalysisChart::Ahi) };
    case MaskLeaks:
        return { STR_GRAPH_LeakRate, QStringLiteral("LeakTotal"), QStringLiteral("LeakSpan"), kPressure,
                 STR_GRAPH_Usage };
    case Oxygen:
        return { analysis(gAnalysisChart::Odi), analysis(gAnalysisChart::Spo2Ranges),
                 analysis(gAnalysisChart::ProblemZones), analysis(gAnalysisChart::HypoxicBurden),
                 analysis(gAnalysisChart::PulseRises), STR_GRAPH_Oxi_SPO2, STR_GRAPH_Oxi_Pulse,
                 STR_GRAPH_Oxi_SPO2Drop, STR_GRAPH_Oxi_PulseChange };
    case Analysis:
        break;
    }
    QStringList names { kDeviceAhi };
    for (gAnalysisChart::Kind kind : gAnalysisChart::kinds())
        names << analysis(kind);
    return names;
}

Visibility visibility(Preset preset, const Visibility &own)
{
    if (preset == All) return own;
    const QStringList shown = graphNames(preset);
    Visibility result;
    for (auto it = own.cbegin(); it != own.cend(); ++it)
        result.insert(it.key(), shown.contains(it.key()));
    return result;
}

Visibility State::switchTo(Preset preset, const Visibility &current)
{
    // only the user's own choice is remembered, never what a preset showed
    if (m_preset == All) m_own = current;
    m_preset = preset;
    return visibility(preset, m_own);
}

Visibility State::reload(const Visibility &own)
{
    m_own = own;
    return visibility(m_preset, m_own);
}

Visibility State::own(const Visibility &current) const
{
    return m_preset == All ? current : m_own;
}

} // namespace OverviewPresets
