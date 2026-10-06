/* Manual Scoring Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "manualscoringtests.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "SleepLib/manual_scoring.h"
#include "database/database_manager.h"
#include "database/database_schema.h"
#include "database/manual_scoring_repository.h"
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
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QCoreApplication(argc, argv);
    }
    if (CPAP_Obstructive == 0) schema::init();
    if (DatabaseManager::instance().isOpen()) DatabaseManager::instance().close();
    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-manualscoring-XXXXXX"));
    QVERIFY(m_tempDir->isValid());
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));
    QSqlQuery q(DatabaseManager::instance().database());
    QVERIFY(q.exec(QStringLiteral("INSERT INTO profiles (username, data_folder) VALUES ('scoring', '/tmp/scoring')")));
    const qint64 profile = q.lastInsertId().toLongLong();
    m_profileRow = profile;
    q.prepare(QStringLiteral("INSERT INTO machines (profile_id, machine_id, loader_name, machine_type) VALUES (?, 1, 'test', 1)"));
    q.addBindValue(profile);
    QVERIFY(q.exec());
    m_machineRow = q.lastInsertId().toLongLong();
}

void ManualScoringTests::cleanupTestCase()
{
    DatabaseManager::instance().close();
    delete m_tempDir;
    m_tempDir = nullptr;
    delete m_app;
    m_app = nullptr;
}

qint64 ManualScoringTests::sessionRow(qint64 deviceSessionId)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("INSERT INTO sessions (session_id, machine_id, start_time, end_time, duration) VALUES (?, ?, 0, 3600000, 3600)"));
    q.addBindValue(deviceSessionId);
    q.addBindValue(m_machineRow);
    if (!q.exec()) return 0;
    return q.lastInsertId().toLongLong();
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

void ManualScoringTests::testExcludeDropsRera()
{
    // a RERA cannot be edited, but a stretch left out leaves it out of the RDI too
    QList<DeviceEvent> device = deviceEvents();
    device.append({ CPAP_RERA, 250 * kSec, 8 });
    const Result r = apply(device, { edit(1, Kind::Exclude, 0, 150 * kSec, 350 * kSec) }, oneSession());
    QCOMPARE(r.delta.value(CPAP_RERA), -1);
}

// ---- storage

SessionKey ManualScoringTests::key(SessionID session) const
{
    SessionKey k;
    k.profileId = m_profileRow;
    k.serial = QStringLiteral("S1");
    k.session = session;
    return k;
}

void ManualScoringTests::testStoreAndLoadEdits()
{
    QList<Edit> stored = { edit(0, Kind::Add, CPAP_Hypopnea, 480 * kSec, 500 * kSec),
                           edit(0, Kind::Remove, CPAP_Obstructive, 185 * kSec, 200 * kSec),
                           edit(0, Kind::Retype, CPAP_ClearAirway, 289 * kSec, 300 * kSec, CPAP_Obstructive),
                           edit(0, Kind::Exclude, 0, 150 * kSec, 350 * kSec) };
    stored[3].note = QStringLiteral("awake");
    for (Edit &e : stored) {
        e.key = key(101);
        e.id = ManualScoringRepository::add(e);
        QVERIFY(e.id > 0);
    }
    const QList<Edit> loaded = ManualScoringRepository::editsForSession(key(101));
    QCOMPARE(loaded.size(), 4);
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(loaded[i].id, stored[i].id);
        QVERIFY(loaded[i].key == key(101));
        QCOMPARE(int(loaded[i].kind), int(stored[i].kind));
        QCOMPARE(loaded[i].channel, stored[i].channel);
        QCOMPARE(loaded[i].newChannel, stored[i].newChannel);
        QCOMPARE(loaded[i].startMs, stored[i].startMs);
        QCOMPARE(loaded[i].endMs, stored[i].endMs);
        QCOMPARE(loaded[i].note, stored[i].note);
        QVERIFY(loaded[i].createdAt.isValid());
    }
    QVERIFY(ManualScoringRepository::editsForSession(key(999)).isEmpty());
}

void ManualScoringTests::testRemoveEdit()
{
    Edit e = edit(0, Kind::Add, CPAP_Hypopnea, 1, 2);
    e.key = key(102);
    const qint64 a = ManualScoringRepository::add(e);
    const qint64 b = ManualScoringRepository::add(e);
    QVERIFY(ManualScoringRepository::remove(a));
    const QList<Edit> left = ManualScoringRepository::editsForSession(key(102));
    QCOMPARE(left.size(), 1);
    QCOMPARE(left.first().id, b);
    QVERIFY(ManualScoringRepository::removeAllForSession(key(102)));
    QVERIFY(ManualScoringRepository::editsForSession(key(102)).isEmpty());
}

void ManualScoringTests::testSummaryRoundTrip()
{
    Result r;
    r.delta.insert(CPAP_Obstructive, -1);
    r.delta.insert(CPAP_Hypopnea, 2);
    r.excludedMs = 120000;
    r.notFound = { 7, 9 };
    QVERIFY(ManualScoringRepository::storeSummary(key(103), r));
    QHash<ChannelID, int> delta;
    qint64 excluded = 0;
    int notFound = 0;
    QVERIFY(ManualScoringRepository::loadSummary(key(103), delta, excluded, notFound));
    QCOMPARE(delta, r.delta);
    QCOMPARE(excluded, qint64(120000));
    QCOMPARE(notFound, 2);
    QVERIFY(ManualScoringRepository::removeSummary(key(103)));
    QVERIFY(!ManualScoringRepository::loadSummary(key(103), delta, excluded, notFound));
}

void ManualScoringTests::testEditsOutliveSessionRows()
{
    // a rebuild deletes the session rows and imports them again: the edits stay
    const qint64 row = sessionRow(104);
    Edit e = edit(0, Kind::Exclude, 0, 1, 2);
    e.key = key(104);
    ManualScoringRepository::add(e);
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("DELETE FROM sessions WHERE id = ?"));
    q.addBindValue(row);
    QVERIFY(q.exec());
    QCOMPARE(ManualScoringRepository::editsForSession(key(104)).size(), 1);
}

void ManualScoringTests::testProfileDeleteCascades()
{
    QSqlQuery q(DatabaseManager::instance().database());
    QVERIFY(q.exec(QStringLiteral("INSERT INTO profiles (username, data_folder) VALUES ('gone', '/tmp/gone')")));
    SessionKey k = key(105);
    k.profileId = q.lastInsertId().toLongLong();
    Edit e = edit(0, Kind::Add, CPAP_Hypopnea, 1, 2);
    e.key = k;
    ManualScoringRepository::add(e);
    Result r;
    r.excludedMs = 1;
    ManualScoringRepository::storeSummary(k, r);
    q.prepare(QStringLiteral("DELETE FROM profiles WHERE id = ?"));
    q.addBindValue(k.profileId);
    QVERIFY(q.exec());
    QVERIFY(ManualScoringRepository::editsForSession(k).isEmpty());
    QHash<ChannelID, int> delta;
    qint64 excluded = 0;
    int notFound = 0;
    QVERIFY(!ManualScoringRepository::loadSummary(k, delta, excluded, notFound));
}

void ManualScoringTests::testMigration21To22()
{
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("DROP TABLE manual_scoring")));
    QVERIFY(q.exec(QStringLiteral("DROP TABLE manual_scoring_summary")));
    QVERIFY(q.exec(QStringLiteral("UPDATE schema_version SET version = 21")));
    QVERIFY(DatabaseSchema::upgradeSchema(db, 21));
    QCOMPARE(DatabaseSchema::getSchemaVersion(db), 22);
    QVERIFY(db.tables().contains(QStringLiteral("manual_scoring")));
    QVERIFY(db.tables().contains(QStringLiteral("manual_scoring_summary")));
}
