/* Sleep Analysis Integration Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysisintegrationtests.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QSqlRecord>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <cmath>

#include "SleepLib/analysis/analysis_channels.h"
#include "SleepLib/analysis/analysis_service.h"
#include "SleepLib/analysis/day_analysis.h"
#include "SleepLib/analysis/session_analysis.h"
#include "SleepLib/appsettings.h"
#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "Graphs/gFlagsLine.h"
#include "database/analysis_daily_repository.h"
#include "database/database_manager.h"
#include "database/database_schema.h"
#include "database/preferences_repository.h"
#include "database/profile_repository.h"
#include "database/session_channels_repository.h"
#include "database/session_settings_repository.h"
#include "statistics.h"
#include "doctorreport.h"
#include "nightsummary.h"
#include "pdfreportoptions.h"
#include "pdfreportwriter.h"
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
        m_app = new QApplication(argc, argv);   // the Statistics draw pixmaps
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
    QCOMPARE(all.size(), 21);
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
    QVERIFY(st.flowAnalyzed);
    QCOMPARE(st.flowRateHz, 25.0);
    QVERIFY(st.flScored);
    QVERIFY(st.flowSeconds > 500);
    QVERIFY(st.flBreaths > 100);
    QVERIFY(st.flSum > 0);
    const SessionStamp back = [&] {   // the totals survive the JSON round trip
        Session copy(&mach, 2);
        copy.settings[AN_Stamp] = st.toJson();
        return SessionStamp::read(&copy);
    }();
    QCOMPARE(back.flowSeconds, st.flowSeconds);
    QCOMPARE(back.flBreaths, st.flBreaths);
    QVERIFY(st.glasgowAdapted.breaths > 100);   // the original needs L/min amplitudes; this flow is ±1
    QVERIFY(back.glasgow == st.glasgow);
    QVERIFY(back.glasgowAdapted == st.glasgowAdapted);
    QCOMPARE(back.flLimitedBreaths, st.flLimitedBreaths);
    QCOMPARE(back.unscoreableSeconds, st.unscoreableSeconds);
    QVERIFY(back.flScored);
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

namespace {

AnalysisDailyData sampleDay(qint64 profileId, const QDate &date)
{
    AnalysisDailyData d;
    d.profileId = profileId;
    d.date = date;
    d.algoVersion = kAnalysisAlgoVersion;
    d.paramsHash = QStringLiteral("0123456789abcdef");
    d.inputsHash = QStringLiteral("fedcba9876543210");
    d.hasFlow = true;
    d.flowSeconds = 25200;
    d.flowRateHz = 25;
    d.nObstructiveApnea = 7;
    d.nCentralApnea = 3;
    d.nHypopneaAasm3 = 11;
    d.flSum = 123.5;
    d.flLimitedBreaths = 210;
    d.flLongestSeconds = 840;
    d.glasgow.breaths = 6000;
    for (int k = 0; k < GiComponentCount; ++k) d.glasgow.flagged[k] = 100 + k;
    d.glasgowAdapted.breaths = 5900;
    d.glasgowAdapted.flagged[GiFlatTop] = 450;
    d.hypopneaRule = int(HypopneaRule::Aasm3);
    d.hasOximetry = true;
    d.oxiSeconds = 24000;
    d.oxiScope = QStringLiteral("night");
    d.hasCpap = true;
    d.nDesat3 = 12;
    d.spo2Hist = QVector<int>(51, 0);
    d.spo2Hist[46] = 20000;
    d.spo2Hist[40] = 4000;
    d.spo2Nadir = 84;
    d.linkedDesatArea = 310.5;
    return d;
}

bool isNull(qint64 profileId, const QDate &date, const char *column)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("SELECT %1 IS NULL FROM analysis_daily WHERE profile_id = ? AND date = ?").arg(column));
    q.addBindValue(profileId);
    q.addBindValue(date.toString(Qt::ISODate));
    return q.exec() && q.next() && q.value(0).toBool();
}

} // namespace

void AnalysisIntegrationTests::testDailyRowRoundTrip()
{
    AnalysisDailyRepository repo;
    const QDate date(2026, 3, 1);
    QVERIFY(repo.upsert(sampleDay(m_profileId, date)));

    const AnalysisDailyData d = repo.find(m_profileId, date);
    QVERIFY(d.id > 0);
    QCOMPARE(d.date, date);
    QCOMPARE(d.algoVersion, kAnalysisAlgoVersion);
    QCOMPARE(d.inputsHash, QStringLiteral("fedcba9876543210"));
    QVERIFY(d.computedAt.isValid());
    QVERIFY(d.hasFlow);
    QCOMPARE(d.flowSeconds, 25200);
    QCOMPARE(d.nObstructiveApnea, 7);
    QCOMPARE(d.nHypopneaAasm3, 11);
    QCOMPARE(d.flSum, 123.5);
    QVERIFY(d.hasOximetry);
    QCOMPARE(d.oxiScope, QStringLiteral("night"));
    QCOMPARE(d.spo2Hist.size(), 51);
    QCOMPARE(d.spo2Hist[46], 20000);
    QCOMPARE(d.linkedDesatArea, 310.5);
    QCOMPARE(d.flLimitedBreaths, 210);
    QCOMPARE(d.flLongestSeconds, 840);
    QVERIFY(d.glasgow == sampleDay(m_profileId, date).glasgow);
    QVERIFY(d.glasgowAdapted == sampleDay(m_profileId, date).glasgowAdapted);

    // groups that do not apply are NULL, not 0
    QVERIFY(!d.hasComparison);
    QVERIFY(!d.hasPulse);
    QVERIFY(!d.hasOffsetHint);
    QVERIFY(isNull(m_profileId, date, "cmp_matched"));
    QVERIFY(isNull(m_profileId, date, "pulse_hist"));
    QVERIFY(isNull(m_profileId, date, "oxi_offset_hint_ms"));
    QVERIFY(!isNull(m_profileId, date, "flow_s"));

    // a day without oximetry keeps its flow and has no oximetry columns
    AnalysisDailyData flowOnly = sampleDay(m_profileId, date.addDays(1));
    flowOnly.hasOximetry = false;
    QVERIFY(repo.upsert(flowOnly));
    QVERIFY(!repo.find(m_profileId, date.addDays(1)).hasOximetry);
    QVERIFY(isNull(m_profileId, date.addDays(1), "n_desat3"));
    QVERIFY(isNull(m_profileId, date.addDays(1), "linked_desat_area"));

    QVERIFY(repo.removeRange(m_profileId, date, date.addDays(1)));
}

void AnalysisIntegrationTests::testDailyRowReplacesSameDay()
{
    AnalysisDailyRepository repo;
    const QDate date(2026, 3, 10);
    QVERIFY(repo.upsert(sampleDay(m_profileId, date)));
    AnalysisDailyData again = sampleDay(m_profileId, date);
    again.nObstructiveApnea = 2;
    again.inputsHash = QStringLiteral("1111111111111111");
    QVERIFY(repo.upsert(again));

    const QList<AnalysisDailyData> rows = repo.findRange(m_profileId, date, date);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().nObstructiveApnea, 2);
    QCOMPARE(rows.first().inputsHash, QStringLiteral("1111111111111111"));
    QVERIFY(repo.remove(m_profileId, date));
    QCOMPARE(repo.find(m_profileId, date).id, qint64(0));
}

void AnalysisIntegrationTests::testDailyRowRangeAndRemove()
{
    AnalysisDailyRepository repo;
    const QDate first(2026, 4, 1);
    for (int i = 3; i >= 0; --i) QVERIFY(repo.upsert(sampleDay(m_profileId, first.addDays(i))));

    QList<AnalysisDailyData> rows = repo.findRange(m_profileId, first, first.addDays(3));
    QCOMPARE(rows.size(), 4);
    for (int i = 0; i < rows.size(); ++i) QCOMPARE(rows[i].date, first.addDays(i));

    QVERIFY(repo.removeRange(m_profileId, first.addDays(1), first.addDays(2)));
    rows = repo.findRange(m_profileId, first, first.addDays(3));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].date, first);
    QCOMPARE(rows[1].date, first.addDays(3));
    QCOMPARE(repo.findRange(m_profileId + 1, first, first.addDays(3)).size(), 0);
    QVERIFY(repo.removeRange(m_profileId, first, first.addDays(3)));
}

void AnalysisIntegrationTests::testMigrationAddsAnalysisDaily()
{
    QSqlDatabase db = DatabaseManager::instance().database();
    QCOMPARE(DatabaseSchema::getSchemaVersion(db), DatabaseSchema::CURRENT_SCHEMA_VERSION);

    // back to a v19 database, then upgrade
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("DROP TABLE analysis_daily")));
    QVERIFY(q.exec(QStringLiteral("UPDATE schema_version SET version = 19")));
    QVERIFY(!db.tables().contains(QStringLiteral("analysis_daily")));

    QVERIFY(DatabaseSchema::upgradeSchema(db, 19));
    QCOMPARE(DatabaseSchema::getSchemaVersion(db), DatabaseSchema::CURRENT_SCHEMA_VERSION);
    QVERIFY(db.tables().contains(QStringLiteral("analysis_daily")));
    QVERIFY(AnalysisDailyRepository().upsert(sampleDay(m_profileId, QDate(2026, 5, 1))));
    QVERIFY(AnalysisDailyRepository().remove(m_profileId, QDate(2026, 5, 1)));
}

namespace {

const QDate kNightDate(2023, 11, 14);   // synth::kStart's night

// A CPAP session whose flow falls to 40 % for six breaths at about 400 s: a hypopnea
// candidate with a 60 % reduction.
Session *hypopneaSession(Machine *mach, SessionID id, qint64 machineRow)
{
    Session *sess = new Session(mach, id);
    const FlowChunk c = synth::breathSequence(25, [] {
        QVector<synth::SynthBreath> seq = synth::repeat(100, synth::SynthBreath());
        seq += synth::repeat(6, synth::SynthBreath { 4, 0.4, synth::sineShape });
        seq += synth::repeat(60, synth::SynthBreath());
        return seq;
    }());
    QVector<qint16> raw;
    for (float v : c.samples) raw.append(qint16(std::lround(v * 100)));
    EventList *el = sess->AddEventList(CPAP_FlowRate, EVL_Waveform, 0.01f, 0, 0, 0, c.rateMs);
    el->AddWaveform(c.start, raw.data(), raw.size(), qint64(raw.size() * c.rateMs));
    sess->really_set_first(c.start);
    sess->really_set_last(c.start + qint64(raw.size() * c.rateMs));
    sess->setSessionRowId(insertRow(QStringLiteral("INSERT INTO sessions (session_id, machine_id, start_time, end_time, duration) "
                                                   "VALUES (?, ?, ?, ?, 664)"), { id, machineRow, sess->first(), sess->last() }));
    return sess;
}

// An oximetry session: SpO2 96 %, down to 92 % from 425 s to 445 s.
Session *oximetrySession(Machine *mach, SessionID id, qint64 machineRow)
{
    Session *sess = new Session(mach, id);
    EventList *el = sess->AddEventList(OXI_SPO2, EVL_Event);
    const qint64 t0 = synth::kStart;
    el->AddEvent(t0, 96);
    el->AddEvent(t0 + 425000, 94);
    el->AddEvent(t0 + 428000, 92);
    el->AddEvent(t0 + 445000, 94);
    el->AddEvent(t0 + 448000, 96);
    el->AddEvent(t0 + 900000, 96);
    sess->really_set_first(t0);
    sess->really_set_last(t0 + 900000);
    sess->setSessionRowId(insertRow(QStringLiteral("INSERT INTO sessions (session_id, machine_id, start_time, end_time, duration) "
                                                   "VALUES (?, ?, ?, ?, 900)"), { id, machineRow, sess->first(), sess->last() }));
    return sess;
}

int hypopneaCount(Session &sess)
{
    return eventCount(sess, AN_Hypopnea) + eventCount(sess, AN_ObstructiveHypopnea) + eventCount(sess, AN_CentralHypopnea);
}

} // namespace

void AnalysisIntegrationTests::testDayAnalysisScoresAndStores()
{
    const qint64 oxiRow = insertRow(QStringLiteral("INSERT INTO machines (profile_id, machine_id, loader_name, machine_type, serial_number) "
                                                   "VALUES (?, 3002, 'TestOxi', ?, 'OX1')"), { m_profileId, int(MT_OXIMETER) });
    QVERIFY(oxiRow > 0);
    Machine cpap(p_profile, 40);
    cpap.info.type = MT_CPAP;
    cpap.setDatabaseId(m_machineRow);
    Machine oxi(p_profile, 41);
    oxi.info.type = MT_OXIMETER;
    oxi.setDatabaseId(oxiRow);
    Day day;
    day.setDate(kNightDate);
    Session *cs = hypopneaSession(&cpap, 60, m_machineRow);
    Session *os = oximetrySession(&oxi, 61, oxiRow);
    day.addSession(cs);
    day.addSession(os);

    AnalysisParams params;
    AnalysisDailyRepository repo;
    QVERIFY(dayOutdated(&day, params, repo.find(m_profileId, kNightDate)));

    DayAnalysis a = analyzeDay(&day, params);
    QVERIFY(a.scored);
    QVERIFY(a.stored);
    QVERIFY(!stageOneNeeded(cs, params).any());   // stage 1 ran first ...
    QVERIFY(!stageOneNeeded(os, params).any());
    QVERIFY(!SessionSettingsRepository().findBySetting(cs->sessionRowId(), AN_Stamp).jsonValue.isEmpty());   // ... and was stored
    QCOMPARE(hypopneaCount(*cs), 1);
    QCOMPARE(eventCount(*cs, AN_Desaturation), 0);   // oximetry channels go to the oximeter's session
    QCOMPARE(eventCount(*os, AN_Desaturation), 1);

    AnalysisDailyData row = repo.find(m_profileId, kNightDate);
    QVERIFY(row.id > 0);
    QVERIFY(row.hasFlow);
    QVERIFY(row.flowSeconds > 500);
    QCOMPARE(row.nObstructiveHypopnea + row.nCentralHypopnea + row.nHypopnea, 1);
    QCOMPARE(row.nHypopneaAasm3, 1);
    QCOMPARE(row.nUnconfirmable, 0);
    QVERIFY(row.hasOximetry);
    QVERIFY(row.hasCpap);
    QCOMPARE(row.nDesat3, 1);
    QCOMPARE(row.oxiScope, QStringLiteral("night"));
    QVERIFY(row.hasComparison);
    QCOMPARE(row.cmpAnalysisOnly, 1);   // the device scored nothing
    QCOMPARE(row.inputsHash, dayInputsHash(&day, params));
    QCOMPARE(row.paramsHash, params.dayHash());

    // the hypopnea channel went to the database without the waveform
    {
        Session stored(&cpap, 60);
        stored.setSessionRowId(cs->sessionRowId());
        QVERIFY(stored.LoadEventsFromDatabase(QSet<ChannelID> { AN_Hypopnea, AN_ObstructiveHypopnea, AN_CentralHypopnea }));
        QCOMPARE(hypopneaCount(stored), 1);
    }

    // current: nothing to do
    QVERIFY(!dayOutdated(&day, params, row));
    a = analyzeDay(&day, params);
    QVERIFY(a.upToDate);
    QVERIFY(!a.scored);

    // The oximeter's clock is corrected by 2 minutes: the desaturation no longer follows
    // the reduction, and AASM 3 % no longer confirms it.
    oxi.rebuildCorrections({ TimeCorrectionRow { kNightDate, QDate(), QStringLiteral("offset"), 120000, 0, 0.0 } });
    QCOMPARE(os->correctionMs(), qint64(120000));
    QVERIFY(dayOutdated(&day, params, repo.find(m_profileId, kNightDate)));
    a = analyzeDay(&day, params);
    QVERIFY(a.scored);
    QCOMPARE(hypopneaCount(*cs), 0);
    row = repo.find(m_profileId, kNightDate);
    QCOMPARE(row.nHypopneaAasm3, 0);
    QCOMPARE(row.nHypopneaFlow, 1);
    QVERIFY(row.hasOximetry);

    // ... Flow only does
    params.day.rule = HypopneaRule::FlowOnly;
    QVERIFY(dayOutdated(&day, params, row));
    QVERIFY(analyzeDay(&day, params).stored);
    QCOMPARE(hypopneaCount(*cs), 1);
    QCOMPARE(repo.find(m_profileId, kNightDate).hypopneaRule, int(HypopneaRule::FlowOnly));

    // a disabled session leaves the day
    os->setEnabled(false);
    QCOMPARE(analysableSessions(&day).size(), 1);
    QVERIFY(dayOutdated(&day, params, repo.find(m_profileId, kNightDate)));
    QVERIFY(analyzeDay(&day, params).stored);
    QVERIFY(!repo.find(m_profileId, kNightDate).hasOximetry);

    // switched off: nothing at all
    params.enabled = false;
    QVERIFY(!dayOutdated(&day, params, AnalysisDailyData()));
    QVERIFY(!analyzeDay(&day, params).scored);
    QVERIFY(repo.remove(m_profileId, kNightDate));
}

void AnalysisIntegrationTests::testDayAnalysisLoadsOnlyWhatItNeeds()
{
    Machine cpap(p_profile, 42);
    cpap.info.type = MT_CPAP;
    cpap.setDatabaseId(m_machineRow);
    Day day;
    day.setDate(kNightDate.addDays(1));
    Session *cs = hypopneaSession(&cpap, 62, m_machineRow);
    cs->AddEventList(CPAP_Obstructive, EVL_Event)->AddEvent(synth::kStart + 200000, 12);
    day.addSession(cs);

    // stage 1 at import, then the events are put away
    AnalysisParams params;
    analyzeSession(cs, params);
    const QString loadedHash = dayInputsHash(&day, params);
    QVERIFY(cs->StoreEventsToDatabase());
    cs->TrashEvents();
    QVERIFY(cs->eventlist.isEmpty());
    QCOMPARE(dayInputsHash(&day, params), loadedHash);   // whatever is in memory

    // Day scoring needs only event channels: it loads those, and puts them away again.
    const DayAnalysis a = analyzeDay(&day, params);
    QVERIFY(a.stored);
    QVERIFY(a.result.hasFlow);
    QVERIFY(!a.result.hasOximetry);
    QCOMPARE(a.result.hypopneas.size(), 1);   // without an oximeter, by flow only
    QCOMPARE(a.result.unconfirmable, 1);
    QCOMPARE(a.result.deviceEvents.size(), 1);   // the device's apnea was loaded too
    QCOMPARE(a.result.match.deviceOnly.size(), 1);
    QVERIFY(cs->eventlist.isEmpty());
    QVERIFY(!cs->partialEvents());

    // the waveform is still there, and the hypopnea joined it
    QVERIFY(cs->OpenEvents());
    QVERIFY(cs->eventlist.contains(CPAP_FlowRate));
    QCOMPARE(hypopneaCount(*cs), 1);
    cs->TrashEvents();

    // scoring for display loads and releases the same way, and stores nothing
    QVERIFY(AnalysisDailyRepository().remove(m_profileId, day.date()));
    const DayResult r = scoreStoredDay(&day, params);
    QCOMPARE(r.hypopneas.size(), 1);
    QVERIFY(cs->eventlist.isEmpty());
    QCOMPARE(AnalysisDailyRepository().find(m_profileId, day.date()).id, qint64(0));
}

void AnalysisIntegrationTests::testAnalysisSettingsRoundTrip()
{
    AnalysisSettings *settings = p_profile->analysis;
    QVERIFY(settings != nullptr);
    settings->resetToDefaults();
    const AnalysisParams defaults;
    AnalysisParams p = settings->params();
    QCOMPARE(p.enabled, defaults.enabled);
    QCOMPARE(p.flowHash(), defaults.flowHash());
    QCOMPARE(p.oxiHash(), defaults.oxiHash());
    QCOMPARE(p.dayHash(), defaults.dayHash());

    p.enabled = false;
    p.day.rule = HypopneaRule::Cms4;
    p.day.limitOxiToCpap = true;
    p.day.linkWindowSec = 45;
    p.flow.classifyApneas = false;
    p.flow.flThreshold = 0.6;
    p.oxi.zoneMinDesats = 4;
    p.oxi.tachyBpm = 110;
    settings->setParams(p);
    const AnalysisParams back = settings->params();
    QCOMPARE(back.enabled, false);
    QVERIFY(back.day.rule == HypopneaRule::Cms4);
    QCOMPARE(back.oxi.zoneMinDesats, 4);
    QCOMPARE(back.flowHash(), p.flowHash());
    QCOMPARE(back.oxiHash(), p.oxiHash());
    QCOMPARE(back.dayHash(), p.dayHash());

    // a rule number from elsewhere falls back on Auto
    p_profile->Set(STR_AN_HypopneaRule, 9);
    QVERIFY(settings->params().day.rule == HypopneaRule::Auto);

    QCOMPARE(settings->spo2Thresholds(), AnalysisSettings::defaultSpo2Thresholds());
    settings->setSpo2Thresholds({ 88, 92, 92, 40, 80, 85, 90, 94, 95 });
    QCOMPARE(settings->spo2Thresholds(), QList<double>({ 95, 94, 92, 90, 88, 85 }));   // sorted, deduplicated, at most six
    p_profile->Set(STR_AN_Spo2Thresholds, QStringLiteral("x, 120"));
    QCOMPARE(settings->spo2Thresholds(), AnalysisSettings::defaultSpo2Thresholds());

    settings->resetToDefaults();
    QCOMPARE(settings->params().dayHash(), defaults.dayHash());
    QVERIFY(settings->enabled());
}

void AnalysisIntegrationTests::testAnalysisServiceKeepsDaysCurrent()
{
    Machine cpap(p_profile, 43);
    cpap.info.type = MT_CPAP;
    cpap.setDatabaseId(m_machineRow);
    const QDate date = kNightDate.addDays(5);
    Day *day = p_profile->addDay(date);
    day->OpenSummary();   // the session below is built here, not loaded
    Session *cs = hypopneaSession(&cpap, 63, m_machineRow);
    analyzeSession(cs, AnalysisParams());   // stage 1, as at import
    day->addSession(cs);

    p_profile->analysis->resetToDefaults();
    AnalysisService service;
    service.reloadSettings();
    QSignalSpy changed(&service, &AnalysisService::daysChanged);

    // a new day with stage 1 done: pending, and cheap to bring up to date
    QCOMPARE(service.pendingDays(), QList<QDate>({ date }));
    QCOMPARE(service.updateDays(service.pendingDays()), 1);
    QVERIFY(!cs->partialEvents());   // its events were all in memory: nothing was loaded
    QVERIFY(cs->eventlist.contains(CPAP_FlowRate));
    QCOMPARE(changed.count(), 1);
    QVERIFY(service.row(date).id > 0);
    QCOMPARE(service.rows(date, date).size(), 1);
    QVERIFY(service.pendingDays().isEmpty());
    QVERIFY(service.outdatedDays().isEmpty());

    // new flow parameters: stage 1 is outdated, which waits for the user or the Daily view
    AnalysisParams strict;
    strict.flow.minEventSec = 20;
    p_profile->analysis->setParams(strict);
    service.reloadSettings();
    QCOMPARE(activeParams().flow.minEventSec, 20.0);   // handed on to the loaders
    QVERIFY(service.pendingDays().isEmpty());
    QCOMPARE(service.outdatedCount(), 1);
    QCOMPARE(service.outdatedDays(), QList<QDate>({ date }));
    QCOMPARE(service.updateDays({ date }, [](int, int) { return false; }), 0);   // cancelled
    QCOMPARE(service.outdatedCount(), 1);
    QCOMPARE(service.updateDays(service.outdatedDays()), 1);
    QCOMPARE(service.outdatedCount(), 0);
    QVERIFY(service.outdatedDays().isEmpty());

    // the day for display
    const DayResult r = service.dayResult(day);
    QVERIFY(r.hasFlow);
    QCOMPARE(r.hypopneas.size(), 1);

    // the day's only session switched off: its row goes
    cs->setEnabled(false);
    QCOMPARE(service.pendingDays(), QList<QDate>({ date }));
    QCOMPARE(service.updateDays(service.pendingDays()), 1);
    QCOMPARE(service.row(date).id, qint64(0));
    QCOMPARE(AnalysisDailyRepository().find(m_profileId, date).id, qint64(0));
    QVERIFY(service.pendingDays().isEmpty());

    // switched off altogether: nothing to do
    AnalysisParams off;
    off.enabled = false;
    p_profile->analysis->setParams(off);
    service.reloadSettings();
    cs->setEnabled(true);
    QVERIFY(service.pendingDays().isEmpty());
    QVERIFY(!service.updateDay(date));

    p_profile->analysis->resetToDefaults();
    setActiveParams(AnalysisParams());
    p_profile->daylist.remove(date);
    delete day;
}

void AnalysisIntegrationTests::testFlagsGraphsSplitDeviceAndAnalysis()
{
    Machine cpap(p_profile, 44);
    cpap.info.type = MT_CPAP;
    cpap.setDatabaseId(m_machineRow);
    Machine oxi(p_profile, 45);
    oxi.info.type = MT_OXIMETER;
    Day day;
    day.setDate(kNightDate.addDays(9));
    Session *cs = new Session(&cpap, 64);
    addFlow(*cs);
    cs->AddEventList(CPAP_Obstructive, EVL_Event)->AddEvent(synth::kStart + 200000, 12);
    analyzeSession(cs, AnalysisParams());
    day.addSession(cs);

    auto codes = [](gFlagsGroup &g) {
        QSet<ChannelID> out;
        for (gFlagsLine *line : g.visibleLayers()) out.insert(line->code());
        return out;
    };

    gFlagsGroup device, analysisFlags(true);
    device.SetDay(&day);
    analysisFlags.SetDay(&day);
    QVERIFY(codes(device).contains(CPAP_Obstructive));
    QVERIFY(!codes(device).contains(AN_Apnea));
    QVERIFY(codes(analysisFlags).contains(AN_Apnea));
    QVERIFY(!codes(analysisFlags).contains(CPAP_Obstructive));
    QVERIFY(!codes(analysisFlags).contains(AN_FLScore));   // a waveform, not a flag
    QVERIFY(!analysisFlags.isEmpty());

    // a night with only an oximeter: the analysis graph shows its desaturations
    Day oxiDay;
    oxiDay.setDate(kNightDate.addDays(10));
    Session *os = new Session(&oxi, 65);
    addSpo2(*os);
    analyzeSession(os, AnalysisParams());
    oxiDay.addSession(os);
    gFlagsGroup oxiFlags(true);
    oxiFlags.SetDay(&oxiDay);
    QVERIFY(codes(oxiFlags).contains(AN_Desaturation));
    QVERIFY(!oxiFlags.isEmpty());

    // nothing analysed: the analysis graph hides
    Day plain;
    plain.setDate(kNightDate.addDays(11));
    Session *ps = new Session(&cpap, 66);
    ps->AddEventList(CPAP_Obstructive, EVL_Event)->AddEvent(synth::kStart + 200000, 12);
    ps->really_set_first(synth::kStart);
    ps->really_set_last(synth::kStart + 600000);
    plain.addSession(ps);
    gFlagsGroup plainFlags(true);
    plainFlags.SetDay(&plain);
    QVERIFY(plainFlags.isEmpty());
}

void AnalysisIntegrationTests::testAnalysisReportQuery()
{
    // the system report's query, as the report tree runs it
    QFile orf(QStringLiteral(":/docs/system_reports.orf"));
    QVERIFY(orf.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(orf.readAll());
    const QRegularExpression block(QStringLiteral("=== Report: Analysis/by Day ===.*?Query: <<SQL\\n(.*?)\\nSQL\\n"),
                                   QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = block.match(text);
    QVERIFY(m.hasMatch());
    QString sql = m.captured(1);
    const QDate date(2026, 6, 1);
    sql.replace(QStringLiteral("#PROFILE_ID"), QString::number(m_profileId));
    sql.replace(QStringLiteral("#START_DATE"), QStringLiteral("'%1'").arg(date.toString(Qt::ISODate)));
    sql.replace(QStringLiteral("#END_DATE"), QStringLiteral("'%1'").arg(date.addDays(1).toString(Qt::ISODate)));

    AnalysisDailyData d;
    d.profileId = m_profileId;
    d.date = date;
    d.algoVersion = kAnalysisAlgoVersion;
    d.paramsHash = d.inputsHash = QStringLiteral("x");
    d.hasFlow = true;
    d.flowSeconds = 7200;
    d.nObstructiveApnea = 4;
    d.nHypopnea = 2;
    d.hasOximetry = true;
    d.oxiSeconds = 7200;
    d.nDesat3 = 10;
    d.spo2Hist = QVector<int>(51, 0);
    d.spo2Hist[95 - 50] = 6600;
    d.spo2Hist[89 - 50] = 480;   // 8 minutes below 90
    d.spo2Hist[87 - 50] = 120;   // 2 of them below 88
    QVERIFY(AnalysisDailyRepository().upsert(d));
    AnalysisDailyData flowOnly = d;   // the next day: no oximetry
    flowOnly.date = date.addDays(1);
    flowOnly.hasOximetry = false;
    QVERIFY(AnalysisDailyRepository().upsert(flowOnly));

    QSqlQuery q(DatabaseManager::instance().database());
    QVERIFY2(q.exec(sql), qPrintable(q.lastError().text()));
    QVERIFY(q.next());
    QCOMPARE(q.value(QStringLiteral("AHI")).toDouble(), 3.0);
    QCOMPARE(q.value(QStringLiteral("OAI")).toDouble(), 2.0);
    QCOMPARE(q.value(QStringLiteral("ODI3")).toDouble(), 5.0);
    QCOMPARE(q.value(QStringLiteral("T90_Min")).toDouble(), 10.0);
    QCOMPARE(q.value(QStringLiteral("T88_Min")).toDouble(), 2.0);
    QCOMPARE(q.value(QStringLiteral("Hypopnea_Rule")).toString(), QStringLiteral("Auto"));
    QVERIFY(q.next());
    QCOMPARE(q.value(QStringLiteral("AHI")).toDouble(), 3.0);
    QVERIFY(q.value(QStringLiteral("ODI3")).isNull());   // no oximetry that day
    QVERIFY(q.value(QStringLiteral("T90_Min")).isNull());
    QVERIFY(!q.next());
    QVERIFY(AnalysisDailyRepository().removeRange(m_profileId, date, date.addDays(1)));
}

namespace {

// Statistics' settings periods, opened up for the test
class RXStatistics : public Statistics
{
  public:
    using Statistics::updateRXChanges;
    using Statistics::rxitems;
};

} // namespace

void AnalysisIntegrationTests::testSettingsPeriodCountsCpapHoursOnly()
{
    // A night on the CPAP with an oximeter worn longer: the settings period's usage, and the
    // device AHI divided by it, count the CPAP's time alone.
    const qint64 oxiRow = insertRow(QStringLiteral("INSERT INTO machines (profile_id, machine_id, loader_name, machine_type, serial_number) "
                                                   "VALUES (?, 3003, 'TestOxi', ?, 'OX2')"), { m_profileId, int(MT_OXIMETER) });
    QVERIFY(oxiRow > 0);
    Machine cpap(p_profile, 50);
    cpap.info.type = MT_CPAP;
    cpap.setDatabaseId(m_machineRow);
    Machine oxi(p_profile, 51);
    oxi.info.type = MT_OXIMETER;
    oxi.setDatabaseId(oxiRow);
    Day *day = new Day();
    day->setDate(kNightDate);
    day->addSession(hypopneaSession(&cpap, 70, m_machineRow));
    day->addSession(oximetrySession(&oxi, 71, oxiRow));
    const double cpapHours = day->hours(MT_CPAP);
    QVERIFY(day->hours() > cpapHours);   // the oximeter session runs longer

    p_profile->daylist.insert(kNightDate, day);
    RXStatistics stats;
    stats.updateRXChanges();
    p_profile->daylist.remove(kNightDate);

    QCOMPARE(stats.rxitems.size(), 1);
    QCOMPARE(stats.rxitems.first().hours, cpapHours);
    delete day;
}

void AnalysisIntegrationTests::testSettingsComparisonRowsTrimmedToDates()
{
    // Two nights on the same settings; the comparison for the second night alone counts
    // just that night, its hours recounted from the night itself.
    QFile::remove(p_profile->Get("{" + STR_GEN_DataFolder + "}/RXChanges.cache"));
    const QDate first = kNightDate.addDays(50), second = first.addDays(1);
    Machine cpap(p_profile, 62);
    cpap.info.type = MT_CPAP;
    cpap.setDatabaseId(m_machineRow);
    Day *a = new Day();
    a->setDate(first);
    a->addSession(hypopneaSession(&cpap, 90, m_machineRow));
    Day *b = new Day();
    b->setDate(second);
    b->addSession(hypopneaSession(&cpap, 91, m_machineRow));
    p_profile->daylist.insert(first, a);
    p_profile->daylist.insert(second, b);

    Statistics stats;
    bool showDevice = true;
    const QList<SettingsComparison::Row> rows = stats.settingsComparisonRows(second, second, &showDevice);
    p_profile->daylist.remove(first);
    p_profile->daylist.remove(second);

    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().group.dates, QList<QDate>({ second }));
    QCOMPARE(rows.first().group.hours, double(b->hours(MT_CPAP)));
    QVERIFY(!showDevice);
    delete a;
    delete b;
}

void AnalysisIntegrationTests::testDoctorReportCountsCpapHoursOnly()
{
    // A two-day report around one night on the CPAP with an oximeter worn longer.
    QFile::remove(p_profile->Get("{" + STR_GEN_DataFolder + "}/RXChanges.cache"));
    const qint64 oxiRow = insertRow(QStringLiteral("INSERT INTO machines (profile_id, machine_id, loader_name, machine_type, serial_number) "
                                                   "VALUES (?, 3004, 'TestOxi', ?, 'OX3')"), { m_profileId, int(MT_OXIMETER) });
    QVERIFY(oxiRow > 0);
    Machine cpap(p_profile, 63);
    cpap.info.type = MT_CPAP;
    cpap.setDatabaseId(m_machineRow);
    Machine oxi(p_profile, 64);
    oxi.info.type = MT_OXIMETER;
    oxi.setDatabaseId(oxiRow);
    const QDate date = kNightDate.addDays(60);
    Day *day = new Day();
    day->setDate(date);
    day->addSession(hypopneaSession(&cpap, 92, m_machineRow));
    day->addSession(oximetrySession(&oxi, 93, oxiRow));
    const double cpapHours = day->hours(MT_CPAP);
    p_profile->daylist.insert(date, day);

    Statistics stats;
    const DoctorReport r = stats.doctorReport(date.addDays(-1), date);
    p_profile->daylist.remove(date);

    QCOMPARE(r.days(), 2);
    QCOMPARE(r.nights, 1);
    QCOMPARE(r.nightList.size(), 2);
    QVERIFY(std::isnan(r.nightList[0].hours));
    QCOMPARE(r.nightList[1].hours, cpapHours);
    QCOMPARE(r.meanHours, cpapHours);
    QCOMPARE(r.comparison.size(), 1);
    QCOMPARE(r.settingsSince, date);
    QVERIFY(r.settingsChanges.isEmpty());
    delete day;
}

void AnalysisIntegrationTests::testDoctorReportSettingsSince()
{
    // Settings unchanged since the night before the period: «since» names that night.
    QFile::remove(p_profile->Get("{" + STR_GEN_DataFolder + "}/RXChanges.cache"));
    Machine cpap(p_profile, 65);
    cpap.info.type = MT_CPAP;
    cpap.setDatabaseId(m_machineRow);
    const QDate first = kNightDate.addDays(70), second = first.addDays(1);
    Day *a = new Day();
    a->setDate(first);
    a->addSession(hypopneaSession(&cpap, 94, m_machineRow));
    Day *b = new Day();
    b->setDate(second);
    b->addSession(hypopneaSession(&cpap, 95, m_machineRow));
    p_profile->daylist.insert(first, a);
    p_profile->daylist.insert(second, b);

    Statistics stats;
    const DoctorReport r = stats.doctorReport(second, second);
    p_profile->daylist.remove(first);
    p_profile->daylist.remove(second);

    QCOMPARE(r.nights, 1);
    QCOMPARE(r.settingsSince, first);
    QVERIFY(r.settingsChanges.isEmpty());
    delete a;
    delete b;
}

void AnalysisIntegrationTests::testDoctorReportAnalysisOnCpapNightsOnly()
{
    // A CPAP night and, the next day, a baseline night on the oximeter alone: the report's
    // oximetry is the CPAP night's, as in the settings table.
    QFile::remove(p_profile->Get("{" + STR_GEN_DataFolder + "}/RXChanges.cache"));
    Machine cpap(p_profile, 66);
    cpap.info.type = MT_CPAP;
    cpap.setDatabaseId(m_machineRow);
    const QDate date = kNightDate.addDays(80), baselineDate = date.addDays(1);
    Day *day = new Day();
    day->setDate(date);
    day->addSession(hypopneaSession(&cpap, 96, m_machineRow));
    p_profile->daylist.insert(date, day);

    AnalysisDailyData night, baseline;
    night.date = date;
    night.hasCpap = true;
    night.hasOximetry = true;
    night.oxiSeconds = 3600;
    night.nDesat3 = 6;
    baseline.date = baselineDate;
    baseline.hasOximetry = true;
    baseline.oxiSeconds = 3600;
    baseline.nDesat3 = 60;

    Statistics stats;
    const DoctorReport r = stats.doctorReport(date, baselineDate, { night, baseline }, {});
    QCOMPARE(r.oximetryNights, 1);
    QCOMPARE(r.odi3, 6.0);
    QCOMPARE(r.analysisMissing, 0);

    // out of date, or never analysed: the analysis' figures are left out, and counted
    const DoctorReport stale = stats.doctorReport(date, baselineDate, { night, baseline }, { date });
    const DoctorReport none = stats.doctorReport(date, baselineDate, {}, {});
    p_profile->daylist.remove(date);
    QCOMPARE(stale.analysisMissing, 1);
    QVERIFY(std::isnan(stale.odi3));
    QVERIFY(std::isnan(stale.analysisAhi));
    QCOMPARE(none.analysisMissing, 1);
    QVERIFY(std::isnan(none.below90));
    delete day;
}

void AnalysisIntegrationTests::testNightSummaryTimeAtMaximum()
{
    // An APAP hour at 10 with its last 10 minutes at the upper limit of 14: the start screen
    // says 10 min at the maximum.
    Machine cpap(p_profile, 67);
    cpap.info.type = MT_CPAP;
    cpap.setDatabaseId(m_machineRow);
    const QDate date = kNightDate.addDays(90);
    const qint64 start = QDateTime(date, QTime(23, 0)).toMSecsSinceEpoch();
    Session *sess = new Session(&cpap, 97);
    QVector<qint16> pressure(3600, 1000);
    for (int i = 3000; i < 3600; ++i) pressure[i] = 1400;
    EventList *el = sess->AddEventList(CPAP_Pressure, EVL_Waveform, 0.01f, 0, 0, 0, 1000);
    el->AddWaveform(start, pressure.data(), pressure.size(), qint64(pressure.size()) * 1000);
    sess->settings[CPAP_Mode] = int(MODE_APAP);
    sess->settings[CPAP_PressureMax] = 14.0;
    sess->really_set_first(start);
    sess->really_set_last(start + qint64(pressure.size()) * 1000);
    sess->UpdateSummaries();
    Day *day = new Day();
    day->setDate(date);
    day->addSession(sess);
    p_profile->daylist.insert(date, day);

    const NightSummary s = buildNightSummaryFor(p_profile, nullptr, date, date, false);
    p_profile->daylist.remove(date);

    QCOMPARE(s.pressureMax, 14.0);
    QVERIFY2(std::abs(s.secondsAtMax - 600) < 2, qPrintable(QString::number(s.secondsAtMax)));
    QVERIFY2(s.pressureMaxNote().contains(QCoreApplication::translate("NightSummary", "%1 min").arg(10)),
             qPrintable(s.pressureMaxNote()));
    delete day;
}

namespace {

// One CPAP night on a machine the profile knows, for the Statistics tests; undone in the destructor.
struct StatisticsNight {
    Machine cpap;
    QDate date;
    Day *day = nullptr;
    StatisticsNight(qint64 machineRow, SessionID id, const QDate &d) : cpap(p_profile, 68 + id), date(d)
    {
        QFile::remove(p_profile->Get("{" + STR_GEN_DataFolder + "}/RXChanges.cache"));
        cpap.info.type = MT_CPAP;
        cpap.info.brand = QStringLiteral("TestBrand");
        cpap.info.model = QStringLiteral("TestModel");
        cpap.info.serial = QStringLiteral("SN12345");
        cpap.setDatabaseId(machineRow);
        p_profile->AddMachine(&cpap);
        day = p_profile->addDay(date);   // keeps the profile's first and last day up to date
        day->addSession(hypopneaSession(&cpap, id, machineRow));
        cpap.day.insert(date, day);       // the machine's own list of days, as an import fills it
    }
    ~StatisticsNight()
    {
        cpap.day.remove(date);
        p_profile->daylist.remove(date);
        p_profile->DelMachine(&cpap);
        delete day;
    }
};

} // namespace

void AnalysisIntegrationTests::testPeriodHtmlSections()
{
    StatisticsNight night(m_machineRow, 100, kNightDate.addDays(100));
    Statistics stats;
    StatisticsSections all;
    all.settingsChanges = all.oximetry = all.devices = true;
    const QString full = stats.periodHtml(night.date, night.date, all);
    QVERIFY(full.contains(Statistics::tr("Changes to Device Settings")));
    QVERIFY(full.contains(Statistics::tr("Device Information")));

    StatisticsSections none = all;
    none.settingsChanges = none.devices = false;
    const QString bare = stats.periodHtml(night.date, night.date, none);
    QVERIFY(!bare.contains(Statistics::tr("Changes to Device Settings")));
    QVERIFY(!bare.contains(Statistics::tr("Device Information")));
    QVERIFY(bare.contains(Statistics::tr("CPAP Statistics")));
}

void AnalysisIntegrationTests::testPeriodHtmlPersonalData()
{
    StatisticsNight night(m_machineRow, 101, kNightDate.addDays(101));
    const QString first = p_profile->user->firstName();
    p_profile->user->setFirstName(QStringLiteral("Иван"));
    const bool shown = AppSetting->showPersonalData();
    AppSetting->setShowPersonalData(true);

    Statistics stats;
    StatisticsSections s;
    s.personalData = false;
    s.serialNumbers = false;
    const QString hidden = stats.periodHtml(night.date, night.date, s);
    s.personalData = true;
    const QString visible = stats.periodHtml(night.date, night.date, s);
    const bool stillShown = AppSetting->showPersonalData();

    p_profile->user->setFirstName(first);
    AppSetting->setShowPersonalData(shown);
    QVERIFY(!hidden.contains(QStringLiteral("Иван")));
    QVERIFY(visible.contains(QStringLiteral("Иван")));
    QVERIFY(stillShown);   // the report's choice never touches the user's setting
}

void AnalysisIntegrationTests::testPeriodHtmlClampsToData()
{
    StatisticsNight night(m_machineRow, 102, kNightDate.addDays(102));
    const int mode = p_profile->general->statReportMode();
    const QDate rangeStart = p_profile->general->statReportRangeStart();
    Statistics stats;
    const QString html = stats.periodHtml(night.date.addDays(-400), night.date.addDays(400), StatisticsSections());
    QVERIFY(html.contains(Statistics::tr("CPAP Statistics")));
    QCOMPARE(p_profile->general->statReportMode(), mode);
    QCOMPARE(p_profile->general->statReportRangeStart(), rangeStart);
}

namespace {

int pdfPageCount(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return -1;
    return int(QString::fromLatin1(f.readAll()).count(QRegularExpression(QStringLiteral("/Type\\s*/Page[^s]"))));
}

PdfReportOptions oneNight(const QDate &date)
{
    PdfReportOptions o;
    o.apply(PdfReportOptions::Brief);
    o.statistics = true;
    o.period = PdfReportOptions::Custom;
    o.from = o.to = date;
    return o;
}

} // namespace

void AnalysisIntegrationTests::testWriterSummaryAndStatistics()
{
    StatisticsNight night(m_machineRow, 103, kNightDate.addDays(103));
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("report.pdf"));
    PdfReportWriter writer(nullptr, nullptr);
    QString error;
    QVERIFY2(writer.write(oneNight(night.date), night.date, path, nullptr, &error), qPrintable(error));
    QVERIFY(pdfPageCount(path) >= 2);   // the summary page, then the statistics
}

void AnalysisIntegrationTests::testWriterRefusesEmptyPeriod()
{
    StatisticsNight night(m_machineRow, 104, kNightDate.addDays(104));
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("empty.pdf"));
    PdfReportOptions o = oneNight(night.date.addDays(-20));   // a day without nights
    PdfReportWriter writer(nullptr, nullptr);
    QString error;
    QVERIFY(!writer.write(o, night.date, path, nullptr, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFile::exists(path));
}

void AnalysisIntegrationTests::testWriterRestoresAndCleansUp()
{
    StatisticsNight night(m_machineRow, 105, kNightDate.addDays(105));
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("cancelled.pdf"));
    PdfReportWriter writer(nullptr, nullptr);
    writer.cancel();
    QString error;
    QVERIFY(!writer.write(oneNight(night.date), night.date, path, nullptr, &error));
    QVERIFY(writer.cancelled());
    QVERIFY(!QFile::exists(path));
}

// The report window's choices are kept in the profile's database rows, and come back.
void AnalysisIntegrationTests::testPdfReportOptionsSurviveProfileSave()
{
    PdfReportOptions o;
    o.apply(PdfReportOptions::Detailed);
    o.period = PdfReportOptions::Custom;
    o.from = QDate(2026, 9, 5);
    o.to = QDate(2026, 10, 4);
    o.personalData = false;
    const QVariantMap saved = p_profile->general->pdfReportOptions();
    p_profile->general->setPdfReportOptions(o.toMap());

    PreferencesRepository repo;
    QVERIFY(repo.saveAllPreferences(m_profileId, nullptr, nullptr, nullptr, nullptr, p_profile->general));
    p_profile->general->setPdfReportOptions(QVariantMap());
    QVERIFY(repo.loadAllPreferences(m_profileId, nullptr, nullptr, nullptr, nullptr, p_profile->general));
    const PdfReportOptions back = PdfReportOptions::fromMap(p_profile->general->pdfReportOptions());
    p_profile->general->setPdfReportOptions(saved);

    QCOMPARE(back.toMap(), o.toMap());
}

// The device table shows serial numbers only when the report asks for them.
void AnalysisIntegrationTests::testPeriodHtmlSerialNumbers()
{
    StatisticsNight night(m_machineRow, 103, kNightDate.addDays(103));
    Statistics stats;
    StatisticsSections s;
    s.devices = true;
    s.serialNumbers = false;
    const QString hidden = stats.periodHtml(night.date, night.date, s);
    s.serialNumbers = true;
    const QString shown = stats.periodHtml(night.date, night.date, s);
    QVERIFY(hidden.contains(QStringLiteral("TestModel")));
    QVERIFY(!hidden.contains(QStringLiteral("SN12345")));
    QVERIFY(shown.contains(QStringLiteral("SN12345")));
}

// Unticking oximetry drops the device's rows and the analysis' oximetry and pulse rows.
void AnalysisIntegrationTests::testOximetryRows()
{
    QVERIFY(Statistics::isOximetryRow(StatisticsRow(QStringLiteral("SPO2"), SC_AVG, MT_OXIMETER)));
    for (const char *key : { "#oximetry", "odi3", "odi4", "below:90", "zones", "nadir", "hb", "#pulse", "pri", "dhr" }) {
        QVERIFY2(Statistics::isOximetryRow(StatisticsRow(QString::fromLatin1(key), SC_ANALYSIS, MT_UNKNOWN)), key);
    }
    for (const char *key : { "#breathing", "ahi", "fl", "agreement" }) {
        QVERIFY2(!Statistics::isOximetryRow(StatisticsRow(QString::fromLatin1(key), SC_ANALYSIS, MT_UNKNOWN)), key);
    }
    QVERIFY(!Statistics::isOximetryRow(StatisticsRow(QStringLiteral("AHI"), SC_CPH, MT_CPAP)));
}

// A v20 database: its analysis rows survive the upgrade, with the new figures empty.
void AnalysisIntegrationTests::testMigrationV21KeepsRows()
{
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    for (const char *column : { "fl_limited_breaths", "fl_longest_s", "gi_breaths", "gi_counts", "gia_breaths", "gia_counts" }) {
        QVERIFY2(q.exec(QStringLiteral("ALTER TABLE analysis_daily DROP COLUMN %1").arg(QLatin1String(column))), column);
    }
    QVERIFY(q.exec(QStringLiteral("UPDATE schema_version SET version = 20")));
    const QDate date(2026, 6, 1);
    q.prepare(QStringLiteral("INSERT INTO analysis_daily (profile_id, date, algo_version, params_hash, inputs_hash, computed_at, flow_s, fl_time_s) "
                             "VALUES (?, ?, 2, 'p', 'i', '2026-06-02T08:00:00', 25000, 900)"));
    q.addBindValue(m_profileId);
    q.addBindValue(date.toString(Qt::ISODate));
    QVERIFY(q.exec());

    QVERIFY(DatabaseSchema::upgradeSchema(db, 20));
    QCOMPARE(DatabaseSchema::getSchemaVersion(db), 21);
    const AnalysisDailyData d = AnalysisDailyRepository().find(m_profileId, date);
    QVERIFY(d.id > 0);
    QCOMPARE(d.flSeconds, 900);
    QCOMPARE(d.flLimitedBreaths, 0);
    QCOMPARE(d.flLongestSeconds, 0);
    QVERIFY(d.glasgow.isEmpty());
    QVERIFY(d.glasgowAdapted.isEmpty());
    QVERIFY(AnalysisDailyRepository().remove(m_profileId, date));
}

void AnalysisIntegrationTests::testToDailyRowCarriesGlasgow()
{
    DayResult r;
    r.hasFlow = true;
    r.flowSeconds = 20000;
    r.flLimitedBreaths = 77;
    r.flLongestSeconds = 300;
    r.glasgow.breaths = 1000;
    r.glasgow.flagged[GiSkew] = 50;
    r.glasgowAdapted.breaths = 990;
    r.glasgowAdapted.flagged[GiNoPause] = 12;
    const AnalysisDailyData d = toDailyRow(r, AnalysisParams(), QStringLiteral("x"), QString());
    QCOMPARE(d.flLimitedBreaths, 77);
    QCOMPARE(d.flLongestSeconds, 300);
    QVERIFY(d.glasgow == r.glasgow);
    QVERIFY(d.glasgowAdapted == r.glasgowAdapted);
}

namespace {

AnalysisDailyData flNight(int flSeconds, int limited, int longest, int flagged)
{
    AnalysisDailyData d;
    d.hasFlow = true;
    d.flowSeconds = 25000;
    d.flSeconds = flSeconds;
    d.flBreaths = 1000;
    d.flLimitedBreaths = limited;
    d.flLongestSeconds = longest;
    d.glasgow.breaths = 1000;
    d.glasgow.flagged[GiSkew] = flagged;
    d.glasgowAdapted.breaths = 1000;
    d.glasgowAdapted.flagged[GiSkew] = flagged / 2;
    return d;
}

} // namespace

void AnalysisIntegrationTests::testGlasgowFigures()
{
    const QList<AnalysisDailyData> rows { flNight(600, 200, 120, 100), flNight(1200, 400, 300, 300) };
    QCOMPARE(analysisFigureValue(QStringLiteral("flmin"), rows), 15.0);
    QCOMPARE(analysisFigureValue(QStringLiteral("fllong"), rows), 5.0);
    QCOMPARE(analysisFigureValue(QStringLiteral("flbr"), rows), 30.0);
    QCOMPARE(analysisFigureValue(QStringLiteral("gi"), rows), 0.2);
    QCOMPARE(analysisFigureValue(QStringLiteral("gia"), rows), 0.1);
}

// A night without flow limitation scoring (below 10 Hz) does not pull the averages down.
void AnalysisIntegrationTests::testGlasgowPeriodSkipsEmptyNights()
{
    AnalysisDailyData slow;
    slow.hasFlow = true;
    slow.flowSeconds = 25000;
    const QList<AnalysisDailyData> rows { flNight(600, 200, 120, 100), flNight(1200, 400, 300, 300), slow };
    QCOMPARE(analysisFigureValue(QStringLiteral("flmin"), rows), 15.0);
    QCOMPARE(analysisFigureValue(QStringLiteral("fllong"), rows), 5.0);
    QCOMPARE(analysisFigureValue(QStringLiteral("flbr"), rows), 30.0);
    QCOMPARE(analysisFigureValue(QStringLiteral("gi"), rows), 0.2);
    QVERIFY(std::isnan(analysisFigureValue(QStringLiteral("gi"), { slow })));
}
