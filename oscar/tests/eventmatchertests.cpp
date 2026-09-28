/* Sleep Analysis Event Matcher Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "eventmatchertests.h"

#include "SleepLib/analysis/device_event_conventions.h"
#include "SleepLib/analysis/event_matcher.h"

using namespace analysis;

namespace {

// An event from \a from to \a to seconds.
MatchEvent ev(double from, double to, RespEvent type)
{
    return MatchEvent { qint64(from * 1000), qint64(to * 1000), type };
}

} // namespace

void EventMatcherTests::testOverlappingEventsMatch()
{
    const MatchResult r = matchEvents({ ev(100, 115, RespEvent::ObstructiveApnea) },
                                      { ev(102, 116, RespEvent::ObstructiveApnea) });
    QCOMPARE(r.matched.size(), 1);
    QCOMPARE(r.matched[0], qMakePair(0, 0));
    QVERIFY(r.deviceOnly.isEmpty());
    QVERIFY(r.analysisOnly.isEmpty());
    QCOMPARE(r.typeMismatch, 0);
    QCOMPARE(r.agreement(), 1.0);
}

void EventMatcherTests::testToleranceWidensIntervals()
{
    // 8 s apart: both widened by 5 s, they overlap
    QCOMPARE(matchEvents({ ev(100, 110, RespEvent::Hypopnea) }, { ev(118, 128, RespEvent::Hypopnea) }).matched.size(), 1);
    // 11 s apart: they do not
    const MatchResult r = matchEvents({ ev(100, 110, RespEvent::Hypopnea) }, { ev(121, 131, RespEvent::Hypopnea) });
    QCOMPARE(r.matched.size(), 0);
    QCOMPARE(r.deviceOnly, QVector<int>({ 0 }));
    QCOMPARE(r.analysisOnly, QVector<int>({ 0 }));
    // a narrower tolerance
    QCOMPARE(matchEvents({ ev(100, 110, RespEvent::Hypopnea) }, { ev(118, 128, RespEvent::Hypopnea) }, 2000).matched.size(), 0);
}

void EventMatcherTests::testGreedyTakesLargestOverlap()
{
    // the device apnea overlaps both; it pairs with the one it overlaps most
    const MatchResult r = matchEvents({ ev(100, 120, RespEvent::Apnea) },
                                      { ev(90, 104, RespEvent::Apnea), ev(101, 119, RespEvent::Apnea) });
    QCOMPARE(r.matched.size(), 1);
    QCOMPARE(r.matched[0], qMakePair(0, 1));
    QCOMPARE(r.analysisOnly, QVector<int>({ 0 }));
}

void EventMatcherTests::testEachEventMatchesOnce()
{
    // two device events, one analysis event between them
    const MatchResult r = matchEvents({ ev(100, 112, RespEvent::Hypopnea), ev(114, 126, RespEvent::Hypopnea) },
                                      { ev(110, 124, RespEvent::Hypopnea) });
    QCOMPARE(r.matched.size(), 1);
    QCOMPARE(r.matched[0], qMakePair(1, 0));   // overlaps it for 10 s (+ tolerance), the other for 2 s
    QCOMPARE(r.deviceOnly, QVector<int>({ 0 }));
}

void EventMatcherTests::testSameGroupBeforeTypeMismatch()
{
    // An analysis apnea over a device hypopnea only: a type mismatch.
    MatchResult r = matchEvents({ ev(100, 115, RespEvent::Hypopnea) }, { ev(100, 115, RespEvent::Apnea) });
    QCOMPARE(r.matched.size(), 1);
    QCOMPARE(r.typeMismatch, 1);

    // With a device apnea next to it as well, the analysis apnea pairs within its group,
    // even though it overlaps the hypopnea more.
    r = matchEvents({ ev(100, 115, RespEvent::Hypopnea), ev(104, 118, RespEvent::ObstructiveApnea) },
                    { ev(100, 115, RespEvent::Apnea) });
    QCOMPARE(r.matched.size(), 1);
    QCOMPARE(r.matched[0], qMakePair(1, 0));
    QCOMPARE(r.typeMismatch, 0);
    QCOMPARE(r.deviceOnly, QVector<int>({ 0 }));
}

void EventMatcherTests::testTypeMatrix()
{
    const MatchResult r = matchEvents({ ev(100, 115, RespEvent::ObstructiveApnea), ev(300, 320, RespEvent::Rera) },
                                      { ev(101, 115, RespEvent::CentralApnea), ev(301, 321, RespEvent::Hypopnea) });
    QCOMPARE(r.matched.size(), 2);
    QCOMPARE(r.typeMismatch, 1);   // RERA against hypopnea; OA against CA is the same group
    QCOMPARE(r.typeMatrix[int(RespEvent::ObstructiveApnea) * kRespEventTypes + int(RespEvent::CentralApnea)], 1);
    QCOMPARE(r.typeMatrix[int(RespEvent::Rera) * kRespEventTypes + int(RespEvent::Hypopnea)], 1);
    int total = 0;
    for (int n : r.typeMatrix) total += n;
    QCOMPARE(total, 2);
    QCOMPARE(groupOf(RespEvent::CentralHypopnea), EventGroup::Hypopnea);
    QCOMPARE(groupOf(RespEvent::Apnea), EventGroup::Apnea);
}

void EventMatcherTests::testAgreement()
{
    // 2 matched, 3 device events, 4 analysis events: 2 / (3 + 4 - 2)
    const MatchResult r = matchEvents({ ev(100, 110, RespEvent::Apnea), ev(200, 210, RespEvent::Apnea), ev(300, 310, RespEvent::Apnea) },
                                      { ev(100, 110, RespEvent::Apnea), ev(200, 210, RespEvent::Apnea),
                                        ev(500, 510, RespEvent::Apnea), ev(600, 610, RespEvent::Apnea) });
    QCOMPARE(r.matched.size(), 2);
    QCOMPARE(r.agreement(), 0.4);
    QCOMPARE(matchEvents({}, {}).agreement(), 1.0);
    QCOMPARE(matchEvents({ ev(100, 110, RespEvent::Apnea) }, {}).agreement(), 0.0);
}

void EventMatcherTests::testDeviceEventConventions()
{
    const qint64 t = 100000;
    DeviceEventConvention end;
    QCOMPARE(deviceEventSpan(t, 12, end).start, qint64(88000));
    QCOMPARE(deviceEventSpan(t, 12, end).end, t);
    DeviceEventConvention onset;
    onset.time = EventTimeConvention::Onset;
    QCOMPARE(deviceEventSpan(t, 12, onset).start, t);
    QCOMPARE(deviceEventSpan(t, 12, onset).end, qint64(112000));
    // no usable duration: 10 s
    QCOMPARE(deviceEventSpan(t, 0, end).start, qint64(90000));
    QCOMPARE(deviceEventSpan(t, -1, end).start, qint64(90000));
    QCOMPARE(deviceEventSpan(t, 600, end).start, qint64(90000));
    QCOMPARE(deviceEventSpan(t, 0, onset).end, qint64(110000));
    // every loader keeps OSCAR's convention until real nights say otherwise
    QVERIFY(deviceEventConvention(QStringLiteral("ResMed")).time == EventTimeConvention::End);
    QVERIFY(deviceEventConvention(QStringLiteral("Prisma")).time == EventTimeConvention::End);
    QCOMPARE(deviceEventConvention(QString()).defaultDurationSec, 10.0);
}
