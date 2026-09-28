/* Sleep Analysis Integration Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysisintegrationtests.h"

#include "SleepLib/analysis/analysis_channels.h"
#include "SleepLib/machine.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"

// The channel table must exist; another test class may already have built it.
void AnalysisIntegrationTests::initTestCase()
{
    if (CPAP_Obstructive == 0) { schema::init(); }
}

void AnalysisIntegrationTests::testAnalysisChannelsAreComputed()
{
    QList<ChannelID> all = analysis::flowChannels() + analysis::hypopneaChannels() + analysis::oximetryChannels();
    all << AN_Stamp;
    QCOMPARE(all.size(), 19);
    for (ChannelID code : all) {
        const schema::Channel &ch = schema::channel[code];
        QVERIFY2(!schema::channel[code].isNull() && ch.id() == code, qPrintable(QString::number(code, 16)));
        QVERIFY2(ch.isComputed(), qPrintable(QString::number(code, 16)));
        QVERIFY(analysis::isAnalysisChannel(code));
    }
    QCOMPARE(schema::channel[AN_Desaturation].machtype(), MT_OXIMETER);
    QCOMPARE(schema::channel[AN_ObstructiveApnea].machtype(), MT_CPAP);
    QVERIFY(!schema::channel[CPAP_Obstructive].isComputed());
    QVERIFY(!analysis::isAnalysisChannel(CPAP_Obstructive));
    // candidates and unscoreable time are hidden until the user asks for them
    QVERIFY(!schema::channel[AN_FlowReduction].enabled());
    QVERIFY(!schema::channel[AN_Unscoreable].enabled());
    QVERIFY(schema::channel[AN_ObstructiveApnea].enabled());
}

// The analysis must never change what OSCAR believes the device reports (the OH/CH
// capability and the NULL-versus-0 summary columns depend on it).
void AnalysisIntegrationTests::testComputedChannelsAreNotReportedByDevice()
{
    Machine mach(nullptr, 21);
    Session sess(&mach, 1);
    sess.AddEventList(AN_ObstructiveHypopnea, EVL_Event)->AddEvent(1000, 12);
    sess.AddEventList(AN_CentralHypopnea, EVL_Event)->AddEvent(2000, 12);
    sess.m_cnt[AN_ObstructiveApnea] = 4;
    mach.noteReportedChannels(&sess);
    QVERIFY(!mach.hasReportedEvents(AN_ObstructiveHypopnea));
    QVERIFY(!mach.hasReportedEvents(AN_ObstructiveApnea));
    QVERIFY(!mach.reportsHypopneaMechanism());
}

void AnalysisIntegrationTests::testAnalysisChannelsAreNotInAhi()
{
    for (ChannelID code : analysis::flowChannels() + analysis::hypopneaChannels()) {
        QVERIFY(!ahiChannels.contains(code));
        QVERIFY(!cahiChannels.contains(code));
    }
}
