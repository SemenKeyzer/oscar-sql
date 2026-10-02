/* Overview Graph Presets Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "overviewpresetstests.h"

#include "overviewpresets.h"
#include "common_gui.h"
#include "Graphs/gAnalysisCharts.h"

using namespace OverviewPresets;

namespace {

// The user's own choice: device AHI and SpO2 hidden, the rest shown.
Visibility ownChoice()
{
    Visibility own;
    own.insert(QStringLiteral("AHIBreakdown"), false);
    own.insert(STR_GRAPH_Usage, true);
    own.insert(STR_GRAPH_LeakRate, true);
    own.insert(STR_GRAPH_RespRate, true);
    own.insert(STR_GRAPH_Oxi_SPO2, false);
    own.insert(gAnalysisChart::code(gAnalysisChart::Odi), true);
    return own;
}

} // namespace

void OverviewPresetsTests::testKeys()
{
    QCOMPARE(presets(), QList<Preset>({ All, Therapy, MaskLeaks, Oxygen, Analysis }));
    for (Preset p : presets()) {
        QVERIFY2(!title(p).isEmpty(), qPrintable(key(p)));
        QCOMPARE(fromKey(key(p)), p);
    }
    QCOMPARE(fromKey(QStringLiteral("no such preset")), All);
    QCOMPARE(fromKey(QString()), All);
}

void OverviewPresetsTests::testPresetGraphs()
{
    QVERIFY(graphNames(All).isEmpty());

    const QStringList therapy = graphNames(Therapy);
    for (const QString &name : { QStringLiteral("AHIBreakdown"), STR_GRAPH_Usage, QStringLiteral("New Session"),
                                 QStringLiteral("Pressure Settings"), QStringLiteral("TTIA"),
                                 gAnalysisChart::code(gAnalysisChart::Ahi) })
        QVERIFY2(therapy.contains(name), qPrintable(name));

    const QStringList mask = graphNames(MaskLeaks);
    for (const QString &name : { STR_GRAPH_LeakRate, QStringLiteral("LeakTotal"), QStringLiteral("LeakSpan"),
                                 QStringLiteral("Pressure Settings"), STR_GRAPH_Usage })
        QVERIFY2(mask.contains(name), qPrintable(name));

    const QStringList oxygen = graphNames(Oxygen);
    for (gAnalysisChart::Kind kind : { gAnalysisChart::Odi, gAnalysisChart::Spo2Ranges, gAnalysisChart::ProblemZones,
                                       gAnalysisChart::HypoxicBurden, gAnalysisChart::PulseRises })
        QVERIFY2(oxygen.contains(gAnalysisChart::code(kind)), qPrintable(gAnalysisChart::code(kind)));
    QVERIFY(oxygen.contains(STR_GRAPH_Oxi_SPO2));
    QVERIFY(oxygen.contains(STR_GRAPH_Oxi_Pulse));
    QVERIFY(!oxygen.contains(QStringLiteral("AHIBreakdown")));

    // every analysis graph, with the device's AHI to compare against
    const QStringList analysis = graphNames(Analysis);
    for (gAnalysisChart::Kind kind : gAnalysisChart::kinds())
        QVERIFY2(analysis.contains(gAnalysisChart::code(kind)), qPrintable(gAnalysisChart::code(kind)));
    QVERIFY(analysis.contains(QStringLiteral("AHIBreakdown")));

    // breathing graphs stay with the user's own choice
    for (Preset p : presets())
        QVERIFY2(!graphNames(p).contains(STR_GRAPH_RespRate), qPrintable(key(p)));
}

void OverviewPresetsTests::testVisibility()
{
    const Visibility own = ownChoice();
    QCOMPARE(visibility(All, own), own);

    // the preset's graphs, shown even where the user hid them, and nothing else
    const Visibility oxygen = visibility(Oxygen, own);
    QCOMPARE(oxygen.keys().size(), own.keys().size());
    QCOMPARE(oxygen.value(STR_GRAPH_Oxi_SPO2), true);
    QCOMPARE(oxygen.value(gAnalysisChart::code(gAnalysisChart::Odi)), true);
    QCOMPARE(oxygen.value(STR_GRAPH_Usage), false);
    QCOMPARE(oxygen.value(STR_GRAPH_LeakRate), false);
    QCOMPARE(oxygen.value(STR_GRAPH_RespRate), false);
    QCOMPARE(oxygen.value(QStringLiteral("AHIBreakdown")), false);
}

void OverviewPresetsTests::testLeavingPresetRestoresOwnChoice()
{
    const Visibility own = ownChoice();
    State state;
    QCOMPARE(state.preset(), All);

    Visibility shown = state.switchTo(Oxygen, own);
    QCOMPARE(shown, visibility(Oxygen, own));
    QCOMPARE(state.preset(), Oxygen);

    // a graph ticked inside the preset, then another preset: still only borrowed
    shown[STR_GRAPH_RespRate] = true;
    shown = state.switchTo(Therapy, shown);
    QCOMPARE(shown, visibility(Therapy, own));

    QCOMPARE(state.switchTo(All, shown), own);
    QCOMPARE(state.preset(), All);
}

void OverviewPresetsTests::testSavingUnderPresetKeepsOwnChoice()
{
    const Visibility own = ownChoice();
    State state;
    QCOMPARE(state.own(own), own);

    const Visibility shown = state.switchTo(MaskLeaks, own);
    QCOMPARE(state.own(shown), own);
}

void OverviewPresetsTests::testReloadKeepsPreset()
{
    State state;
    state.switchTo(Oxygen, ownChoice());

    // the graphs were rebuilt from a saved choice that now shows SpO2
    Visibility rebuilt = ownChoice();
    rebuilt[STR_GRAPH_Oxi_SPO2] = true;
    rebuilt.insert(STR_GRAPH_Oxi_Pulse, false);
    const Visibility shown = state.reload(rebuilt);
    QCOMPARE(shown, visibility(Oxygen, rebuilt));
    QCOMPARE(state.preset(), Oxygen);
    QCOMPARE(state.switchTo(All, shown), rebuilt);

    // under All a reload just shows the user's choice
    State all;
    QCOMPARE(all.reload(rebuilt), rebuilt);
}
