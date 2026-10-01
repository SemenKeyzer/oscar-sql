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
#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/day.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/loader_plugins/contec_ble_import.h"
#include "SleepLib/loader_plugins/contec_ble_loader.h"
#include "database/database_manager.h"
#include "bluetoothoximeterpage.h"
#include "SleepLib/oximetry_summary.h"
#include "database/analysis_daily_repository.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlQuery>
#include <QTemporaryDir>

using namespace ContecBle;

namespace {
const QString kProfileName = QStringLiteral("ContecBleUnitTest");

RecordHeader header(const QDateTime &start, int samples)
{
    RecordHeader h;
    h.l = 1;
    h.m = 1;
    h.year = start.date().year();
    h.month = start.date().month();
    h.day = start.date().day();
    h.hour = start.time().hour();
    h.minute = start.time().minute();
    h.second = start.time().second();
    h.samples = samples;
    return h;
}

Record record(const QDateTime &start, int samples)
{
    Record r;
    r.header = header(start, samples);
    for (int i = 0; i < samples; ++i) {
        r.spo2.append(95 + i % 3);
        r.pulse.append(60 + i % 5);
    }
    return r;
}
} // namespace

void ContecBleImportTests::initTestCase()
{
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    if (QCoreApplication::instance() == nullptr) m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) DatabaseManager::instance().close();
    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-contecbletests-XXXXXX"));
    QVERIFY(m_tempDir->isValid());
    m_previousAppData = GetAppData();
    SetAppData(m_tempDir->path());

    p_profile = nullptr;
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));

    if (CPAP_Obstructive == 0) { schema::init(); }
    ContecBleLoader::Register();
    Profiles::Scan();
    const QString profileDir = m_tempDir->path() + QStringLiteral("/Profiles/") + kProfileName;
    p_profile = Profiles::Get(kProfileName);
    if (p_profile == nullptr) p_profile = Profiles::Create(kProfileName, &profileDir);
    QVERIFY(p_profile != nullptr);
}

void ContecBleImportTests::cleanupTestCase()
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

void ContecBleImportTests::testDecideRecord()
{
    const RecordHeader h = header(QDateTime(QDate(2026, 9, 26), QTime(23, 59, 13)), 600);
    QCOMPARE(decideRecord(h, ExistingSession(), false), Decision::Import);
    QCOMPARE(decideRecord(h, ExistingSession(), true), Decision::ConflictOtherOximeter);
    QCOMPARE(decideRecord(h, ExistingSession{ true, 600 }, false), Decision::AlreadyPresent);
    QCOMPARE(decideRecord(h, ExistingSession{ true, 400 }, false), Decision::ReplaceShorter);
    RecordHeader bad = h;
    bad.hour = 30;
    QCOMPARE(decideRecord(bad, ExistingSession(), false), Decision::InvalidStart);
}

void ContecBleImportTests::testPredictNight()
{
    const QTime noon(12, 0, 0);
    QCOMPARE(predictNight(QDateTime(QDate(2026, 9, 26), QTime(23, 59, 13)), noon), QDate(2026, 9, 26));
    QCOMPARE(predictNight(QDateTime(QDate(2026, 9, 27), QTime(5, 41, 54)), noon), QDate(2026, 9, 26));
    QCOMPARE(predictNight(QDateTime(QDate(2026, 9, 27), QTime(12, 0, 0)), noon), QDate(2026, 9, 27));
}

void ContecBleImportTests::testToOxiRecords()
{
    Record r;
    r.header = header(QDateTime(QDate(2026, 9, 26), QTime(23, 0, 0)), 4);
    r.header.hasPI = true;
    r.spo2 = { 97, 127, 0, 101 };
    r.pulse = { 64, 255, 0, 70 };
    r.pi = { 23, 255, 0, 5 };
    const QVector<OxiRecord> o = toOxiRecords(r);
    QCOMPARE(o.size(), 4);
    QCOMPARE(int(o[0].spo2), 97);
    QCOMPARE(int(o[0].pulse), 64);
    QCOMPARE(int(o[0].perf), 230);
    for (int i : { 1, 2 }) {
        QCOMPARE(int(o[i].spo2), 0);
        QCOMPARE(int(o[i].pulse), 0);
        QCOMPARE(int(o[i].perf), 0);
    }
    QCOMPARE(int(o[3].spo2), 0);        // > 100 % is not a reading
    QCOMPARE(int(o[3].pulse), 70);
}

void ContecBleImportTests::testCanErase()
{
    EraseInput ok;
    ok.eraseEnabled = true;
    ok.downloadCompleted = true;
    ok.outcomes = { Outcome::Imported, Outcome::AlreadyPresent, Outcome::Updated };
    ok.headersOnDevice = 3;
    ok.downloadStarted = QDateTime(QDate(2026, 9, 27), QTime(9, 0, 0));
    ok.lastRecordEnd = ok.downloadStarted.addSecs(-301);
    ok.clockTrusted = true;
    QCOMPARE(canErase(ok), EraseVerdict::Erase);

    EraseInput e = ok; e.eraseEnabled = false;
    QCOMPARE(canErase(e), EraseVerdict::Disabled);
    e = ok; e.downloadCompleted = false;
    QCOMPARE(canErase(e), EraseVerdict::DownloadIncomplete);
    e = ok; e.headersOnDevice = 0; e.outcomes.clear();
    QCOMPARE(canErase(e), EraseVerdict::NothingToErase);
    e = ok; e.outcomes[1] = Outcome::ConflictOtherOximeter;
    QCOMPARE(canErase(e), EraseVerdict::NotAllSaved);
    e = ok; e.outcomes.removeLast();
    QCOMPARE(canErase(e), EraseVerdict::NotAllSaved);
    for (Outcome bad : { Outcome::InvalidStart, Outcome::NotDownloaded, Outcome::SaveFailed }) {
        e = ok; e.outcomes[0] = bad;
        QCOMPARE(canErase(e), EraseVerdict::NotAllSaved);
    }
    e = ok; e.lastRecordEnd = ok.downloadStarted.addSecs(-120);
    QCOMPARE(canErase(e), EraseVerdict::StillRecording);
    // Until OSCAR has set the oximeter clock once, its record times can't be compared with ours.
    e = ok; e.clockTrusted = false;
    QCOMPARE(canErase(e), EraseVerdict::ClockNotSynced);
}

void ContecBleImportTests::testEraseSettingDefaultsOff()
{
    QVERIFY(!p_profile->oxi->bleEraseAfterImport());
    p_profile->oxi->setBleEraseAfterImport(true);
    QVERIFY(p_profile->oxi->bleEraseAfterImport());
    p_profile->oxi->setBleEraseAfterImport(false);
}

void ContecBleImportTests::testImporterImportsNewRecord()
{
    Machine *mach = p_profile->CreateMachine(ContecBleLoader::infoForModel(QStringLiteral("CMS50FW")));
    QVERIFY(mach != nullptr);
    ContecBleImporter imp(mach);
    const QDateTime start(QDate(2026, 9, 26), QTime(23, 59, 13));
    const Record r = record(start, 600);
    QCOMPARE(imp.decide(r.header), Decision::Import);
    QCOMPARE(imp.save(r, Decision::Import), Outcome::Imported);

    const SessionID sid = SessionID(start.toUTC().toSecsSinceEpoch());
    QVERIFY(mach->sessionlist.contains(sid));
    Session *s = mach->sessionlist[sid];
    QCOMPARE(s->realFirst(), qint64(sid) * 1000);
    QCOMPARE(s->realLast(), qint64(sid) * 1000 + 599 * 1000);
    QCOMPARE(s->night(), QDate(2026, 9, 26));
    QCOMPARE(imp.decide(r.header), Decision::AlreadyPresent);
}

void ContecBleImportTests::testImporterReplacesShorterRecord()
{
    Machine *mach = p_profile->CreateMachine(ContecBleLoader::infoForModel(QStringLiteral("CMS50FW")));
    ContecBleImporter imp(mach);
    const QDateTime start(QDate(2026, 9, 20), QTime(22, 0, 0));
    QCOMPARE(imp.save(record(start, 600), Decision::Import), Outcome::Imported);
    const Record longer = record(start, 900);
    QCOMPARE(imp.decide(longer.header), Decision::ReplaceShorter);
    QCOMPARE(imp.save(longer, Decision::ReplaceShorter), Outcome::Updated);
    const SessionID sid = SessionID(start.toUTC().toSecsSinceEpoch());
    QCOMPARE(mach->sessionlist[sid]->realLast(), qint64(sid) * 1000 + 899 * 1000);
    QCOMPARE(imp.decide(longer.header), Decision::AlreadyPresent);
}

void ContecBleImportTests::testImporterRefusesSecondOximeterOnANight()
{
    const MachineInfo otherInfo(MT_OXIMETER, 0, QStringLiteral("DummyOximeter"), QStringLiteral("Test"),
                                QStringLiteral("Other Oximeter"), QString(), QStringLiteral("dummy-oxi"),
                                QStringLiteral("Test"), QDateTime::currentDateTime(), 1);
    Machine *other = p_profile->CreateMachine(otherInfo);
    const QDateTime otherStart(QDate(2026, 9, 10), QTime(23, 0, 0));
    const SessionID otherSid = SessionID(otherStart.toUTC().toSecsSinceEpoch());
    Session *os = new Session(other, otherSid);
    os->really_set_first(qint64(otherSid) * 1000);
    const qint64 last = addOximetryEvents(os, qint64(otherSid) * 1000, toOxiRecords(record(otherStart, 600)), 1000, false);
    finishOximetrySession(os, last, false);
    QVERIFY(other->AddSession(os));

    Machine *mach = p_profile->CreateMachine(ContecBleLoader::infoForModel(QStringLiteral("CMS50FW")));
    ContecBleImporter imp(mach);
    const QDateTime start(QDate(2026, 9, 11), QTime(1, 0, 0));    // same night (before the noon split)
    const Record r = record(start, 600);
    QVERIFY(imp.nightHasOtherOximeter(r.header));
    QCOMPARE(imp.otherOximeterName(r.header), QStringLiteral("Other Oximeter"));
    QCOMPARE(imp.decide(r.header), Decision::ConflictOtherOximeter);
    // Even if asked to import, the session must not be left behind in the device.
    QCOMPARE(imp.save(r, Decision::Import), Outcome::ConflictOtherOximeter);
    QVERIFY(!mach->sessionlist.contains(SessionID(start.toUTC().toSecsSinceEpoch())));
}

// A longer copy that OSCAR refuses (here: older than the ignore date) must not cost the stored one.
void ContecBleImportTests::testImporterKeepsOldSessionWhenReplacementIsRejected()
{
    Machine *mach = p_profile->CreateMachine(ContecBleLoader::infoForModel(QStringLiteral("CMS50FW")));
    ContecBleImporter imp(mach);
    const QDateTime start(QDate(2026, 8, 20), QTime(22, 0, 0));
    QCOMPARE(imp.save(record(start, 600), Decision::Import), Outcome::Imported);
    p_profile->session->setIgnoreOlderSessions(true);
    p_profile->session->setIgnoreOlderSessionsDate(start.date().addDays(7));
    const Outcome o = imp.save(record(start, 900), Decision::ReplaceShorter);
    p_profile->session->setIgnoreOlderSessions(false);
    QCOMPARE(o, Outcome::SaveFailed);
    const SessionID sid = SessionID(start.toUTC().toSecsSinceEpoch());
    QVERIFY(mach->sessionlist.contains(sid));
    QCOMPARE(mach->sessionlist[sid]->realLast(), qint64(sid) * 1000 + 599 * 1000);
}

// Machine::Save() only logs a failed write; the import must notice and not report the record as stored.
void ContecBleImportTests::testImporterReportsDatabaseFailure()
{
    Machine *mach = p_profile->CreateMachine(ContecBleLoader::infoForModel(QStringLiteral("CMS50FW")));
    ContecBleImporter imp(mach);
    const QDateTime start(QDate(2026, 8, 30), QTime(23, 0, 0));
    DatabaseManager::instance().close();
    const Outcome o = imp.save(record(start, 600), Decision::Import);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));
    QCOMPARE(o, Outcome::SaveFailed);
    QVERIFY(!mach->sessionlist.contains(SessionID(start.toUTC().toSecsSinceEpoch())));
}

// Session::Store() ignores a failed event write: the row is saved and the session looks clean,
// but the night's samples are missing. That must not count as imported (it would unlock erase).
void ContecBleImportTests::testImporterReportsMissingSamples()
{
    Machine *mach = p_profile->CreateMachine(ContecBleLoader::infoForModel(QStringLiteral("CMS50FW")));
    ContecBleImporter imp(mach);
    const QDateTime start(QDate(2026, 9, 5), QTime(23, 0, 0));
    QSqlQuery q(DatabaseManager::instance().database());
    QVERIFY(q.exec(QStringLiteral("CREATE TRIGGER test_block_samples BEFORE INSERT ON event_lists "
                                  "BEGIN SELECT RAISE(ABORT, 'blocked by test'); END")));
    const Outcome o = imp.save(record(start, 600), Decision::Import);
    QVERIFY(q.exec(QStringLiteral("DROP TRIGGER test_block_samples")));
    QCOMPARE(o, Outcome::SaveFailed);
    QVERIFY(!mach->sessionlist.contains(SessionID(start.toUTC().toSecsSinceEpoch())));
}

// The wizard's Bluetooth button shows the oximeter picture with a Bluetooth badge in its
// lower-right corner, so it reads as "the same device, over Bluetooth" next to the cable button.
void ContecBleImportTests::testBluetoothBadgeIcon()
{
    QImage base(32, 32, QImage::Format_ARGB32);
    base.fill(QColor(Qt::red));
    const QImage icon = BluetoothOximeterPage::badgedIcon(base, 128);
    QCOMPARE(icon.size(), QSize(128, 128));
    QCOMPARE(icon.pixelColor(10, 10), QColor(Qt::red));                   // the picture stays
    const QColor badge = icon.pixelColor(80, 98);                          // badge disc, left of the rune
    QVERIFY2(badge.blue() > 150 && badge.red() < 80, qPrintable(badge.name()));
    const QColor rune = icon.pixelColor(98, 98);                           // badge centre: the white rune
    QVERIFY2(rune.red() > 200 && rune.green() > 200 && rune.blue() > 200, qPrintable(rune.name()));
}

// The start screen's oximetry summary without an analysed night: figures from the cleaned
// samples over the time with valid readings (a probe-off stretch doesn't dilute them), and
// the classic SpO2 drop count with the thresholds it was counted with.
void ContecBleImportTests::testOximetryNightSummary()
{
    Machine *mach = p_profile->CreateMachine(ContecBleLoader::infoForModel(QStringLiteral("CMS50FW")));
    ContecBleImporter imp(mach);
    const QDateTime start(QDate(2026, 9, 12), QTime(23, 0, 0));
    Record r;
    r.header = header(start, 720);
    for (int i = 0; i < 720; ++i) {
        const int j = i - 120;   // the first two minutes: probe off
        r.spo2.append(j < 0 ? 0 : j >= 300 && j < 360 ? 88 : 96);
        r.pulse.append(j < 0 ? 0 : j < 300 ? 60 : 70);
    }
    QCOMPARE(imp.save(r, Decision::Import), Outcome::Imported);
    Day *day = p_profile->GetDay(QDate(2026, 9, 12), MT_OXIMETER);
    QVERIFY(day);
    for (Session *s : day->sessions) s->TrashEvents();   // as at start-up: summaries only
    const OximetryNight n = summarizeOximetry(day, MT_OXIMETER);
    for (Session *s : day->sessions) QVERIFY(s->eventlist.isEmpty());   // put away again
    QVERIFY(n.valid);
    QVERIFY(!n.fromAnalysis);
    QVERIFY(!n.spotChecks);
    QCOMPARE(qRound(n.hours * 60), 10);
    QCOMPARE(n.spo2Min, 88.0);
    QVERIFY2(qAbs(n.spo2Avg - (540 * 96 + 60 * 88) / 600.0) < 0.2, qPrintable(QString::number(n.spo2Avg)));
    QCOMPARE(qRound(n.minutesBelow90), 1);
    QCOMPARE(qRound(n.percentBelow90), 10);
    QCOMPARE(n.pulseMin, 60.0);
    QCOMPARE(n.pulseMax, 70.0);
    QCOMPARE(n.desaturations, int(day->count(OXI_SPO2Drop)));
    QCOMPARE(n.dropPercent, p_profile->oxi->spO2DropPercentage());
    QCOMPARE(n.dropSeconds, p_profile->oxi->spO2DropDuration());
}

// With an analysed night the start screen shows the analysis' own figures: ODI 3 %, the time
// below 90 % from its histogram over the valid time, the nadir, problem zones and pulse.
void ContecBleImportTests::testOximetryNightFromAnalysis()
{
    AnalysisDailyData row;
    QVERIFY(!summarizeOximetry(row).valid);   // no stored row
    row.id = 7;
    QVERIFY(!summarizeOximetry(row).valid);   // a row without oximetry
    row.hasOximetry = true;
    row.oxiSeconds = 7200;
    row.oxiSource = QStringLiteral("Contec CMS50FW");
    row.spo2Hist.fill(0, 51);
    row.spo2Hist[88 - 50] = 360;              // 6 min at 88 %
    row.spo2Hist[89 - 50] = 360;              // 6 min at 89 %
    row.spo2Hist[90 - 50] = 480;              // 90 % is not below 90
    row.spo2Hist[95 - 50] = 6000;
    row.spo2Sum = 88.0 * 360 + 89.0 * 360 + 90.0 * 480 + 95.0 * 6000;
    row.spo2Nadir = 86;
    row.nDesat3 = 9;
    row.nDesat4 = 4;
    row.nZones = 2;
    row.zoneSeconds = 900;
    row.hasPulse = true;
    row.pulseSeconds = 7000;
    row.pulseSum = 62.0 * 7000;
    row.pulseMin = 51;
    row.pulseMax = 98;

    const OximetryNight n = summarizeOximetry(row);
    QVERIFY(n.valid);
    QVERIFY(n.fromAnalysis);
    QCOMPARE(n.device, QStringLiteral("Contec CMS50FW"));
    QCOMPARE(n.hours, 2.0);
    QCOMPARE(n.spo2Min, 86.0);
    QVERIFY(qAbs(n.spo2Avg - row.spo2Sum / 7200) < 1e-9);
    QCOMPARE(n.minutesBelow90, 12.0);
    QCOMPARE(n.percentBelow90, 10.0);
    QCOMPARE(n.desaturations, 9);
    QCOMPARE(n.desaturations4, 4);
    QCOMPARE(n.zones, 2);
    QCOMPARE(n.zoneMinutes, 15.0);
    QCOMPARE(n.pulseAvg, 62.0);
    QCOMPARE(n.pulseMin, 51.0);
    QCOMPARE(n.pulseMax, 98.0);
}

