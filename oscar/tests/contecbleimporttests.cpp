/* Contec BLE Import Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contecbleimporttests.h"
#include "SleepLib/machine.h"
#include "SleepLib/oximetry_session_builder.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"

void ContecBleImportTests::initTestCase()
{
    if (CPAP_Obstructive == 0) { schema::init(); }
}

// A run of zeros ends the current event list; the next valid value starts a new one.
void ContecBleImportTests::testOximetryEventsSplitAtGaps()
{
    Machine mach(nullptr, 7);
    Session sess(&mach, 1000);
    const qint64 start = 1000000;
    const QVector<OxiRecord> recs = { OxiRecord(60, 95), OxiRecord(61, 96), OxiRecord(0, 0),
                                      OxiRecord(0, 0), OxiRecord(62, 97) };
    const qint64 last = addOximetryEvents(&sess, start, recs, 1000, false);
    QCOMPARE(last, start + 4000);
    const QVector<EventList *> &pulse = sess.eventlist[OXI_Pulse];
    QCOMPARE(pulse.size(), 2);
    QCOMPARE(pulse[0]->first(), start);
    QCOMPARE(pulse[0]->last(), start + 2000);
    QCOMPARE(pulse[1]->first(), start + 4000);
    QCOMPARE(sess.eventlist[OXI_SPO2].size(), 2);
    QVERIFY(!sess.eventlist.contains(OXI_Perf));
}
