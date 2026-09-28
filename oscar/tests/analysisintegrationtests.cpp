/* Sleep Analysis Integration Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysisintegrationtests.h"

#include <QCoreApplication>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <cmath>

#include "SleepLib/analysis/analysis_channels.h"
#include "SleepLib/analysis/session_analysis.h"
#include "SleepLib/appsettings.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "database/database_manager.h"
#include "database/profile_repository.h"
#include "database/session_channels_repository.h"
#include "database/session_settings_repository.h"
#include "tests/analysis_synth.h"

using namespace analysis;

namespace {

const QString kProfileName = QStringLiteral("Analysis Tester");

qint64 insertRow(const QString &sql, const QVariantList &values)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(sql);
    for (const QVariant &v : values) q.addBindValue(v);
    return q.exec() ? q.lastInsertId().toLongLong() : 0;
}

// A CPAP session with a synthetic 25 Hz flow waveform (a 16 s apnea at 400 s).
void addFlow(Session &sess)
{
    const FlowChunk c = synth::breathSequence(25, [] {
        QVector<synth::SynthBreath> seq = synth::repeat(100, synth::SynthBreath());
        seq += synth::repeat(4, synth::SynthBreath { 4, 0.03, synth::sineShape });
        seq += synth::repeat(50, synth::SynthBreath());
        return seq;
    }());
    QVector<qint16> raw;
    for (float v : c.samples) raw.append(qint16(std::lround(v * 100)));
    EventList *el = sess.AddEventList(CPAP_FlowRate, EVL_Waveform, 0.01f, 0, 0, 0, c.rateMs);
    el->AddWaveform(c.start, raw.data(), raw.size(), qint64(raw.size() * c.rateMs));
    sess.really_set_first(c.start);
    sess.really_set_last(c.start + qint64(raw.size() * c.rateMs));
}

// SpO2 stored "on change": 96 %, a 5-point dip at 300 s for 30 s.
void addSpo2(Session &sess)
{
    EventList *el = sess.AddEventList(OXI_SPO2, EVL_Event);
    const qint64 t0 = synth::kStart;
    el->AddEvent(t0, 96);
    el->AddEvent(t0 + 300000, 94);
    el->AddEvent(t0 + 302000, 91);
    el->AddEvent(t0 + 332000, 94);
    el->AddEvent(t0 + 334000, 96);
    el->AddEvent(t0 + 900000, 96);
    sess.really_set_first(t0);
    sess.really_set_last(t0 + 900000);
}

int eventCount(Session &sess, ChannelID code)
{
    int n = 0;
    for (EventList *el : sess.eventlist.value(code)) n += int(el->count());
    return n;
}

int apneaCount(Session &sess)
{
    return eventCount(sess, AN_ObstructiveApnea) + eventCount(sess, AN_CentralApnea) + eventCount(sess, AN_Apnea);
}

} // namespace

void AnalysisIntegrationTests::initTestCase()
{
    // QtSql needs an application object; other suites delete theirs in cleanupTestCase().
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QCoreApplication(argc, argv);
    }
    if (DatabaseManager::instance().isOpen()) DatabaseManager::instance().close();

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-analysistests-XXXXXX"));
    QVERIFY(m_tempDir->isValid());
    m_previousAppData = GetAppData();
    SetAppData(m_tempDir->path());
    p_profile = nullptr;
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));
    if (CPAP_Obstructive == 0) schema::init();
    Profiles::Scan();
    const QString profileDir = m_tempDir->path() + QStringLiteral("/Profiles/") + kProfileName;
    p_profile = Profiles::Create(kProfileName, &profileDir);
    QVERIFY(p_profile != nullptr);
    m_profileId = ProfileRepository().findByUsername(kProfileName).id;
    QVERIFY(m_profileId > 0);
    p_profile->setDatabaseId(m_profileId);
    m_machineRow = insertRow(QStringLiteral("INSERT INTO machines (profile_id, machine_id, loader_name, machine_type, serial_number) "
                                            "VALUES (?, 3001, 'Test', ?, 'AN1')"), { m_profileId, int(MT_CPAP) });
    QVERIFY(m_machineRow > 0);
    setActiveParams(AnalysisParams());
}

void AnalysisIntegrationTests::cleanupTestCase()
{
    Profiles::profiles.clear();
    delete p_profile;
    p_profile = nullptr;
    delete AppSetting;
    AppSetting = nullptr;
    delete p_pref;
    p_pref = nullptr;
    DatabaseManager::instance().close();
    SetAppData(m_previousAppData);
    delete m_tempDir;
    m_tempDir = nullptr;
    delete m_app;
    m_app = nullptr;
}

void AnalysisIntegrationTests::testAnalysisChannelsAreComputed()
{
    QList<ChannelID> all = flowChannels() + hypopneaChannels() + oximetryChannels();
    all << AN_Stamp;
    QCOMPARE(all.size(), 19);
    for (ChannelID code : all) {
        const schema::Channel &ch = schema::channel[code];
        QVERIFY2(!schema::channel[code].isNull() && ch.id() == code, qPrintable(QString::number(code, 16)));
        QVERIFY2(ch.isComputed(), qPrintable(QString::number(code, 16)));
        QVERIFY(isAnalysisChannel(code));
    }
    QCOMPARE(schema::channel[AN_Desaturation].machtype(), MT_OXIMETER);
    QCOMPARE(schema::channel[AN_ObstructiveApnea].machtype(), MT_CPAP);
    QVERIFY(!schema::channel[CPAP_Obstructive].isComputed());
    QVERIFY(!isAnalysisChannel(CPAP_Obstructive));
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
    for (ChannelID code : flowChannels() + hypopneaChannels()) {
        QVERIFY(!ahiChannels.contains(code));
        QVERIFY(!cahiChannels.contains(code));
    }
}

void AnalysisIntegrationTests::testStageOneWritesFlowChannelsAndStamp()
{
    Machine mach(p_profile, 22);
    mach.info.type = MT_CPAP;
    Session sess(&mach, 1);
    addFlow(sess);
    AnalysisParams params;

    const QList<ChannelID> written = analyzeSession(&sess, params);
    QVERIFY(written.contains(AN_Apnea));
    QCOMPARE(apneaCount(sess), 1);
    QCOMPARE(sess.count(AN_ObstructiveApnea) + sess.count(AN_CentralApnea) + sess.count(AN_Apnea), 1.0f);
    QVERIFY(eventCount(sess, AN_FLScore) > 100);
    QVERIFY(!sess.eventlist.contains(OXI_SPO2));
    QVERIFY(sess.m_availableChannels.contains(AN_FLScore));

    const SessionStamp st = SessionStamp::read(&sess);
    QCOMPARE(st.flowVersion, kAnalysisAlgoVersion);
    QCOMPARE(st.flowHash, params.flowHash());
    QVERIFY(!stageOneNeeded(&sess, params).any());
    QVERIFY(analyzeSession(&sess, params, true).isEmpty());   // up to date: nothing to do

    params.flow.minEventSec = 20;   // the apnea is 16 s: gone under the new parameters
    QVERIFY(stageOneNeeded(&sess, params).flow);
    QVERIFY(!stageOneNeeded(&sess, params).oxi);
    QVERIFY(!analyzeSession(&sess, params, true).isEmpty());
    QCOMPARE(apneaCount(sess), 0);
    QCOMPARE(sess.count(AN_Apnea), 0.0f);
}

void AnalysisIntegrationTests::testStageOneOximetry()
{
    Machine mach(p_profile, 23);
    mach.info.type = MT_CPAP;
    Session sess(&mach, 1);
    addSpo2(sess);
    analyzeSession(&sess, AnalysisParams());
    QCOMPARE(eventCount(sess, AN_Desaturation), 1);
    EventList *el = sess.eventlist.value(AN_Desaturation).first();
    QCOMPARE(el->data2(0), 5.0f);     // depth
    QVERIFY(el->data(0) >= 28);       // duration, s
    QCOMPARE(sess.count(AN_Desaturation), 1.0f);
}

void AnalysisIntegrationTests::testStageOneSkipsSessionWithoutEvents()
{
    Machine mach(p_profile, 24);
    mach.info.type = MT_CPAP;
    Session sess(&mach, 1);
    QVERIFY(analyzeSession(&sess, AnalysisParams()).isEmpty());
    QVERIFY(!sess.settings.contains(AN_Stamp));
}

void AnalysisIntegrationTests::testStageOneOffDoesNothing()
{
    Machine mach(p_profile, 25);
    mach.info.type = MT_CPAP;
    Session sess(&mach, 1);
    addFlow(sess);
    AnalysisParams off;
    off.enabled = false;
    QVERIFY(analyzeSession(&sess, off).isEmpty());
    QCOMPARE(apneaCount(sess), 0);
}

void AnalysisIntegrationTests::testStoreChannelEventsLeavesWaveform()
{
    Machine mach(p_profile, 26);
    mach.info.type = MT_CPAP;
    mach.setDatabaseId(m_machineRow);
    Session sess(&mach, 41);
    addFlow(sess);
    sess.setSessionRowId(insertRow(QStringLiteral("INSERT INTO sessions (session_id, machine_id, start_time, end_time, duration) "
                                                  "VALUES (41, ?, ?, ?, 600)"), { m_machineRow, sess.first(), sess.last() }));
    QVERIFY(sess.sessionRowId() > 0);
    analyzeSession(&sess, AnalysisParams());
    QVERIFY(sess.StoreEventsToDatabase());
    const int samples = int(sess.eventlist.value(CPAP_FlowRate).first()->count());

    // re-analysed with stricter parameters: only the analysis channels are rewritten
    AnalysisParams strict;
    strict.flow.minEventSec = 20;
    const QList<ChannelID> written = analyzeSession(&sess, strict, true);
    QVERIFY(sess.StoreChannelEvents(written));

    Session fresh(&mach, 41);
    fresh.setSessionRowId(sess.sessionRowId());
    QVERIFY(fresh.LoadEventsFromDatabase());
    QCOMPARE(int(fresh.eventlist.value(CPAP_FlowRate).first()->count()), samples);
    QCOMPARE(apneaCount(fresh), 0);
    QVERIFY(eventCount(fresh, AN_FLScore) > 100);
    QCOMPARE(SessionChannelsRepository().findByChannel(sess.sessionRowId(), AN_Apnea).id, qint64(0));
    QVERIFY(SessionChannelsRepository().findByChannel(sess.sessionRowId(), AN_FLScore).id > 0);
}

void AnalysisIntegrationTests::testPartialSessionIsNeverStoredInFull()
{
    Machine mach(p_profile, 27);
    mach.info.type = MT_CPAP;
    mach.setDatabaseId(m_machineRow);
    Session sess(&mach, 42);
    addFlow(sess);
    sess.setSessionRowId(insertRow(QStringLiteral("INSERT INTO sessions (session_id, machine_id, start_time, end_time, duration) "
                                                  "VALUES (42, ?, ?, ?, 600)"), { m_machineRow, sess.first(), sess.last() }));
    analyzeSession(&sess, AnalysisParams());
    QVERIFY(sess.StoreEventsToDatabase());

    Session partial(&mach, 42);
    partial.setSessionRowId(sess.sessionRowId());
    QVERIFY(partial.LoadEventsFromDatabase(QSet<ChannelID> { AN_Apnea, AN_FlowReduction }));
    QVERIFY(partial.partialEvents());
    QVERIFY(!partial.eventlist.contains(CPAP_FlowRate));
    QCOMPARE(apneaCount(partial), 1);
    QVERIFY(!partial.StoreEventsToDatabase());   // would delete the waveform

    QVERIFY(partial.OpenEvents());                // a full load replaces the partial one
    QVERIFY(!partial.partialEvents());
    QVERIFY(partial.eventlist.contains(CPAP_FlowRate));
    QCOMPARE(apneaCount(partial), 1);
}

void AnalysisIntegrationTests::testStampIsStoredAsText()
{
    Machine mach(p_profile, 28);
    mach.info.type = MT_CPAP;
    mach.setDatabaseId(m_machineRow);
    Session sess(&mach, 43);
    addSpo2(sess);
    sess.setSessionRowId(insertRow(QStringLiteral("INSERT INTO sessions (session_id, machine_id, start_time, end_time, duration) "
                                                  "VALUES (43, ?, ?, ?, 900)"), { m_machineRow, sess.first(), sess.last() }));
    analyzeSession(&sess, AnalysisParams());
    QVERIFY(sess.StoreSetting(AN_Stamp));
    const SessionSettingData row = SessionSettingsRepository().findBySetting(sess.sessionRowId(), AN_Stamp);
    QCOMPARE(row.dataType, QString("text"));
    QCOMPARE(row.jsonValue, SessionStamp::read(&sess).toJson());
    QVERIFY(sess.StoreSetting(AN_Stamp));   // again: an update, not a second row
    QCOMPARE(SessionSettingsRepository().findBySession(sess.sessionRowId()).size(), 1);
}
