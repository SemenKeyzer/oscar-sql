/* Machine Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "machinetests.h"
#include "SleepLib/machine.h"
#include "SleepLib/session.h"
#include "SleepLib/schema.h"
#include "database/device_time_correction_repository.h"

// AddEventList() looks the channel up in schema::channel, so the channel table must
// exist. Guarded as in apextests.cpp: another test class may have initialised it.
void MachineTests::initTestCase()
{
    if (CPAP_Obstructive == 0) { schema::init(); }
}

// An import-time session: events live in EventLists, m_cnt is still empty.
void MachineTests::testReportedChannelsFromEventLists()
{
    Machine mach(nullptr, 1);
    Session sess(&mach, 1);
    EventList *ch = sess.AddEventList(CPAP_CentralHypopnea, EVL_Event);
    ch->AddEvent(1000, 0);

    QVERIFY(!mach.reportsHypopneaMechanism());
    mach.noteReportedChannels(&sess);
    QVERIFY(mach.hasReportedEvents(CPAP_CentralHypopnea));
    QVERIFY(!mach.hasReportedEvents(CPAP_ObstructiveHypopnea));
    QVERIFY(mach.reportsHypopneaMechanism());
}

// A database-loaded session: EventLists are empty, m_cnt came from session_channels.
void MachineTests::testReportedChannelsFromCounts()
{
    Machine mach(nullptr, 2);
    Session sess(&mach, 1);
    sess.m_cnt[CPAP_ObstructiveHypopnea] = 3;
    sess.m_cnt[CPAP_RERA] = 0;

    mach.noteReportedChannels(&sess);
    QVERIFY(mach.hasReportedEvents(CPAP_ObstructiveHypopnea));
    QVERIFY(!mach.hasReportedEvents(CPAP_RERA));      // count 0 is not evidence
    QVERIFY(mach.reportsHypopneaMechanism());
}

// The empty lists the OH/CH loaders used to create must not count as evidence.
void MachineTests::testEmptyListDoesNotReport()
{
    Machine mach(nullptr, 3);
    Session sess(&mach, 1);
    sess.AddEventList(CPAP_ObstructiveHypopnea, EVL_Event);
    sess.AddEventList(CPAP_CentralHypopnea, EVL_Event);

    mach.noteReportedChannels(&sess);
    QVERIFY(!mach.reportsHypopneaMechanism());
}

// Sessions a loader stores itself are noted just before they are written, so
// their summary rows get counts instead of NULLs (no database: no rebuild).
void MachineTests::testSettleReportedChannelsForLoaderStoredSession()
{
    Machine mach(nullptr, 4);
    Session sess(&mach, 1);
    sess.AddEventList(CPAP_Obstructive, EVL_Event)->AddEvent(1000, 12);

    QVERIFY(!mach.hasReportedEvents(CPAP_Obstructive));
    mach.settleReportedChannels(&sess);
    QVERIFY(mach.hasReportedEvents(CPAP_Obstructive));
    mach.settleReportedChannels(&sess);            // idempotent
    QVERIFY(mach.hasReportedEvents(CPAP_Obstructive));
}

// Every slice mask-off: no hours, and the per-hour indices are 0, not NaN/Inf.
void MachineTests::testIndicesWithoutMaskOnTimeAreZero()
{
    Machine mach(nullptr, 5);
    Session sess(&mach, 1);
    sess.really_set_first(0);
    sess.really_set_last(3600000);
    sess.m_slices.append(SessionSlice(0, 3600000, MaskOff));
    sess.AddEventList(CPAP_Obstructive, EVL_Event)->AddEvent(1000, 12);
    sess.setCount(CPAP_Obstructive, 1);

    QCOMPARE(sess.hours(), 0.0);
    QCOMPARE(sess.cph(CPAP_Obstructive), EventDataType(0));
    QCOMPARE(sess.sph(CPAP_Obstructive), EventDataType(0));
}

// The shared DB-row -> TimeCorrectionRow conversion keeps every field.
void MachineTests::testRowFromDataCopiesFields()
{
    DeviceTimeCorrectionData d;
    d.dateFrom = QStringLiteral("2026-09-20");
    d.dateTo   = QString();                       // open-ended
    d.type     = QStringLiteral("travel");
    d.offsetMs = 3600000;
    d.c0Ms     = 5;
    d.c1       = 0.0;

    const TimeCorrectionRow r = Machine::rowFromData(d);
    QCOMPARE(r.dateFrom, QDate(2026, 9, 20));
    QVERIFY(r.dateTo.isNull());
    QCOMPARE(r.type, QStringLiteral("travel"));
    QCOMPARE(r.offsetMs, qint64(3600000));
    QCOMPARE(r.c0Ms, qint64(5));
    QCOMPARE(r.c1, 0.0);
}

// Drift rows were once stored with c1 = slope + 1.0; only drift rows carry that sentinel.
void MachineTests::testRowFromDataStripsLegacyDriftSentinel()
{
    DeviceTimeCorrectionData legacy;
    legacy.dateFrom = QStringLiteral("2026-01-01");
    legacy.dateTo   = QStringLiteral("2026-12-31");
    legacy.type     = QStringLiteral("drift");
    legacy.c1       = 1.5;
    QCOMPARE(Machine::rowFromData(legacy).c1, 0.5);

    DeviceTimeCorrectionData current = legacy;
    current.c1 = 0.25;
    QCOMPARE(Machine::rowFromData(current).c1, 0.25);

    DeviceTimeCorrectionData offset = legacy;
    offset.type = QStringLiteral("offset");
    offset.c1   = 1.5;
    QCOMPARE(Machine::rowFromData(offset).c1, 1.5);
}

void MachineTests::testCorrectionMsSumsRowsInRange()
{
    Machine mach(nullptr, 4);

    TimeCorrectionRow range;                     // open-ended range
    range.dateFrom = QDate(2026, 9, 1);
    range.type     = QStringLiteral("travel");
    range.offsetMs = 3600000;

    TimeCorrectionRow night;                     // single night
    night.dateFrom = night.dateTo = QDate(2026, 9, 24);
    night.type     = QStringLiteral("offset");
    night.offsetMs = 600000;

    TimeCorrectionRow august;
    august.dateFrom = QDate(2026, 8, 1);
    august.dateTo   = QDate(2026, 8, 31);
    august.type     = QStringLiteral("offset");
    august.offsetMs = 999;

    mach.rebuildCorrections({ range, night, august });
    QCOMPARE(mach.correctionMs(QDate(2026, 9, 24)), qint64(4200000));
    QCOMPARE(mach.correctionMs(QDate(2026, 9, 25)), qint64(3600000));
    QCOMPARE(mach.correctionMs(QDate(2026, 8, 15)), qint64(999));
    QCOMPARE(mach.correctionMs(QDate(2026, 7, 1)),  qint64(0));
}

// A drift row with no slope subtracts its intercept.
void MachineTests::testCorrectionMsAppliesDriftRow()
{
    Machine mach(nullptr, 5);
    TimeCorrectionRow drift;
    drift.dateFrom = QDate(2026, 9, 1);
    drift.type     = QStringLiteral("drift");
    drift.c0Ms     = 2000;
    drift.c1       = 0.0;
    mach.rebuildCorrections({ drift });
    QCOMPARE(mach.correctionMs(QDate(2026, 9, 24)), qint64(-2000));
}
