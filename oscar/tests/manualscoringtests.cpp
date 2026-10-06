/* Manual Scoring Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "manualscoringtests.h"

#include "SleepLib/manual_scoring.h"
#include "SleepLib/schema.h"

using namespace ManualScoring;

namespace {

const qint64 kSec = 1000;

// OA at 100 s and 200 s, CA at 300 s, H at 400 s; one session 0–3600 s
QList<DeviceEvent> deviceEvents()
{
    return { { CPAP_Obstructive, 100 * kSec, 12 }, { CPAP_Obstructive, 200 * kSec, 15 },
             { CPAP_ClearAirway, 300 * kSec, 11 }, { CPAP_Hypopnea, 400 * kSec, 20 } };
}

QList<QPair<qint64, qint64>> oneSession() { return { { 0, 3600 * kSec } }; }

Edit edit(qint64 id, Kind kind, ChannelID channel, qint64 startMs, qint64 endMs, ChannelID newChannel = 0)
{
    Edit e;
    e.id = id;
    e.kind = kind;
    e.channel = channel;
    e.newChannel = newChannel;
    e.startMs = startMs;
    e.endMs = endMs;
    return e;
}

int total(const QHash<ChannelID, int> &delta)
{
    int sum = 0;
    for (int d : delta) sum += d;
    return sum;
}

} // namespace

void ManualScoringTests::initTestCase()
{
    if (CPAP_Obstructive == 0) schema::init();
}

void ManualScoringTests::testNoEditsNoChange()
{
    const Result r = apply(deviceEvents(), {}, oneSession());
    QCOMPARE(total(r.delta), 0);
    QCOMPARE(r.excludedMs, qint64(0));
    QVERIFY(r.notFound.isEmpty());
    QCOMPARE(r.events.size(), 4);
}

void ManualScoringTests::testAdd()
{
    const Result r = apply(deviceEvents(), { edit(1, Kind::Add, CPAP_Hypopnea, 480 * kSec, 500 * kSec) }, oneSession());
    QCOMPARE(r.delta.value(CPAP_Hypopnea), 1);
    QCOMPARE(total(r.delta), 1);
    QCOMPARE(r.events.size(), 5);
}

void ManualScoringTests::testRemove()
{
    // matched within the 1 s tolerance
    Result r = apply(deviceEvents(), { edit(1, Kind::Remove, CPAP_Obstructive, 185 * kSec, 200 * kSec + 400) }, oneSession());
    QCOMPARE(r.delta.value(CPAP_Obstructive), -1);
    QVERIFY(r.notFound.isEmpty());

    // no event there
    r = apply(deviceEvents(), { edit(2, Kind::Remove, CPAP_Obstructive, 240 * kSec, 250 * kSec) }, oneSession());
    QCOMPARE(total(r.delta), 0);
    QCOMPARE(r.notFound, QList<qint64>({ 2 }));
}

void ManualScoringTests::testRetype()
{
    const Result r = apply(deviceEvents(), { edit(1, Kind::Retype, CPAP_ClearAirway, 289 * kSec, 300 * kSec, CPAP_Obstructive) },
                           oneSession());
    QCOMPARE(r.delta.value(CPAP_ClearAirway), -1);
    QCOMPARE(r.delta.value(CPAP_Obstructive), 1);
    QCOMPARE(total(r.delta), 0);
    bool found = false;
    for (const EffectiveEvent &e : r.events) {
        if (e.endMs == 300 * kSec) {
            found = true;
            QCOMPARE(e.origin, Origin::Retyped);
            QCOMPARE(e.channel, CPAP_Obstructive);
            QCOMPARE(e.originalChannel, CPAP_ClearAirway);
            QCOMPARE(e.editId, qint64(1));
        }
    }
    QVERIFY(found);
}

void ManualScoringTests::testLatestEditWins()
{
    const Result r = apply(deviceEvents(),
                           { edit(1, Kind::Remove, CPAP_Obstructive, 88 * kSec, 100 * kSec),
                             edit(2, Kind::Retype, CPAP_Obstructive, 88 * kSec, 100 * kSec, CPAP_Hypopnea) },
                           oneSession());
    QCOMPARE(r.delta.value(CPAP_Obstructive), -1);
    QCOMPARE(r.delta.value(CPAP_Hypopnea), 1);
    QCOMPARE(total(r.delta), 0);
    QVERIFY(r.notFound.isEmpty());
}

void ManualScoringTests::testExclude()
{
    const Result r = apply(deviceEvents(), { edit(1, Kind::Exclude, 0, 150 * kSec, 350 * kSec) }, oneSession());
    QCOMPARE(r.delta.value(CPAP_Obstructive), -1);
    QCOMPARE(r.delta.value(CPAP_ClearAirway), -1);
    QCOMPARE(r.delta.value(CPAP_Hypopnea), 0);
    QCOMPARE(r.excludedMs, qint64(200 * kSec));
    int excluded = 0;
    for (const EffectiveEvent &e : r.events) excluded += e.excluded ? 1 : 0;
    QCOMPARE(excluded, 2);
}

void ManualScoringTests::testExcludeOverlapAndClip()
{
    const QList<QPair<qint64, qint64>> sessions = { { 0, 1000 * kSec }, { 2000 * kSec, 3000 * kSec } };
    const Result r = apply({}, { edit(1, Kind::Exclude, 0, 900 * kSec, 2100 * kSec), edit(2, Kind::Exclude, 0, 950 * kSec, 1200 * kSec) },
                           sessions);
    // 900–1000 in the first session and 2000–2100 in the second; the gap and the overlap count once
    QCOMPARE(r.excludedMs, qint64(200 * kSec));
}

void ManualScoringTests::testExcludeWholeNight()
{
    const Result r = apply(deviceEvents(), { edit(1, Kind::Exclude, 0, -100 * kSec, 4000 * kSec) }, oneSession());
    QCOMPARE(r.excludedMs, qint64(3600 * kSec));
    QCOMPARE(total(r.delta), -4);
}

void ManualScoringTests::testAddedInsideExcludeNotCounted()
{
    const Result r = apply(deviceEvents(),
                           { edit(1, Kind::Add, CPAP_Hypopnea, 180 * kSec, 200 * kSec), edit(2, Kind::Exclude, 0, 150 * kSec, 350 * kSec) },
                           oneSession());
    QCOMPARE(r.delta.value(CPAP_Hypopnea), 0);
}
