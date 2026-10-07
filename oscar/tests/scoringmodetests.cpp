/* Scoring Mode Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "scoringmodetests.h"

#include "Graphs/gManualScoringLayer.h"
#include "Graphs/scoringgesture.h"
#include "SleepLib/schema.h"
#include "common_gui.h"

using namespace ScoringGesture;

void ScoringModeTests::testDragSelectsInScoringMode()
{
    QCOMPARE(actionFor(true, STR_GRAPH_FlowRate, Qt::LeftButton, true), Action::SelectRange);
}

void ScoringModeTests::testDragZoomsOutsideScoringMode()
{
    QCOMPARE(actionFor(false, STR_GRAPH_FlowRate, Qt::LeftButton, true), Action::None);
    QCOMPARE(actionFor(false, STR_GRAPH_FlowRate, Qt::RightButton, false), Action::None);
}

void ScoringModeTests::testRightClickRequestsMenu()
{
    QCOMPARE(actionFor(true, STR_GRAPH_FlowRate, Qt::RightButton, false), Action::ContextMenu);
    QCOMPARE(actionFor(true, STR_GRAPH_SleepFlags, Qt::RightButton, false), Action::ContextMenu);
    QCOMPARE(actionFor(true, STR_GRAPH_FlowRate, Qt::LeftButton, false), Action::None);   // a plain click zooms as before
    QCOMPARE(actionFor(true, STR_GRAPH_FlowRate, Qt::RightButton, true), Action::None);   // a right drag pans
}

void ScoringModeTests::testOtherGraphsUnaffected()
{
    QCOMPARE(actionFor(true, STR_GRAPH_Pressure, Qt::LeftButton, true), Action::None);
    QCOMPARE(actionFor(true, STR_GRAPH_Pressure, Qt::RightButton, false), Action::None);
    QCOMPARE(actionFor(true, STR_GRAPH_SleepFlags, Qt::LeftButton, true), Action::None);   // ranges only on the flow
}

void ScoringModeTests::testRangeFromPixels()
{
    // 1000 px for 100 s starting at 10 000 ms; dragged right to left, partly outside the plot
    const QPair<qint64, qint64> r = rangeFor(10000, 110000, 1000, 600, 200);
    QCOMPARE(r.first, qint64(30000));
    QCOMPARE(r.second, qint64(70000));
    const QPair<qint64, qint64> clipped = rangeFor(10000, 110000, 1000, -50, 1200);
    QCOMPARE(clipped.first, qint64(10000));
    QCOMPARE(clipped.second, qint64(110000));
}

void ScoringModeTests::initTestCase()
{
    if (CPAP_Obstructive == 0) schema::init();
}

void ScoringModeTests::testLayerItems()
{
    using ManualScoring::EffectiveEvent;
    using ManualScoring::Origin;
    using Item = gManualScoringLayer::Item;
    ManualScoring::Result r;
    r.events = { { CPAP_Obstructive, CPAP_Obstructive, 100000, 12, Origin::Device, 0, false },     // untouched: nothing drawn
                 { CPAP_Hypopnea, CPAP_Hypopnea, 200000, 20, Origin::Added, 1, false },
                 { CPAP_Obstructive, CPAP_Obstructive, 300000, 15, Origin::Removed, 2, false },
                 { CPAP_Obstructive, CPAP_ClearAirway, 400000, 11, Origin::Retyped, 3, false },
                 { CPAP_Hypopnea, CPAP_Hypopnea, 9000000, 20, Origin::Added, 4, false } };      // outside the view
    r.excludedSpans = { { 500000, 600000 } };

    const QList<Item> markers = gManualScoringLayer::items(r, 0, 1000000, true);
    QCOMPARE(markers.size(), 4);
    QCOMPARE(int(markers[0].kind), int(Item::Added));
    QCOMPARE(markers[0].start, qint64(180000));
    QCOMPARE(markers[0].end, qint64(200000));
    QCOMPARE(markers[0].label, QStringLiteral("manual"));
    QCOMPARE(int(markers[1].kind), int(Item::Removed));
    QCOMPARE(int(markers[2].kind), int(Item::Retyped));
    QCOMPARE(markers[2].channel, CPAP_Obstructive);
    QCOMPARE(markers[2].label, QStringLiteral("was %1").arg(schema::channel[CPAP_ClearAirway].label()));
    QCOMPARE(int(markers[3].kind), int(Item::Excluded));
    QCOMPARE(markers[3].start, qint64(500000));

    // the other graphs get only the excluded stretches
    const QList<Item> hatch = gManualScoringLayer::items(r, 0, 1000000, false);
    QCOMPARE(hatch.size(), 1);
    QCOMPARE(int(hatch[0].kind), int(Item::Excluded));
}

// the event flags and analysis flags graphs always show the whole night (block zoom): the
// layer places the stretch on that range, not on the zoomed one
void ScoringModeTests::testWholeNightGraphsUseWholeRange()
{
    const QPair<qint64, qint64> whole = gManualScoringLayer::drawnRange(true, 1000, 2000, 0, 10000);
    QCOMPARE(whole.first, qint64(0));
    QCOMPARE(whole.second, qint64(10000));
    const QPair<qint64, qint64> zoomed = gManualScoringLayer::drawnRange(false, 1000, 2000, 0, 10000);
    QCOMPARE(zoomed.first, qint64(1000));
    QCOMPARE(zoomed.second, qint64(2000));
}
