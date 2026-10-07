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
#include <QSqlRecord>
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

// the OA ending at 100 s (12 s long) given new bounds
Edit resize(qint64 id, qint64 startMs, qint64 endMs, qint64 matchEndMs = 100 * kSec)
{
    Edit e = edit(id, Kind::Resize, CPAP_Obstructive, startMs, endMs);
    e.matchEndMs = matchEndMs;
    return e;
}

const EffectiveEvent *eventMatching(const Result &r, qint64 originalEndMs)
{
    for (const EffectiveEvent &e : r.events) {
        if (e.originalEndMs == originalEndMs) return &e;
    }
    return nullptr;
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

// every OA of the night becomes a hypopnea: device events get an edit, a retyped one is edited
// again, an added one is added anew as the new type
void ManualScoringTests::testBulkRetypeAll()
{
    const QList<Edit> edits = { edit(1, Kind::Retype, CPAP_ClearAirway, 289 * kSec, 300 * kSec, CPAP_Obstructive),
                                edit(2, Kind::Add, CPAP_Obstructive, 480 * kSec, 500 * kSec) };
    const Result before = apply(deviceEvents(), edits, oneSession());
    QCOMPARE(before.delta.value(CPAP_Obstructive), 2);   // 2 device + retyped + added
    const BulkPlan plan = bulkEdits(before, CPAP_Obstructive, CPAP_Hypopnea);
    QCOMPARE(plan.count, 4);
    QCOMPARE(plan.undo, QList<qint64>({ 2 }));   // the added OA is taken back…
    QList<Edit> after = edits;
    after.removeAt(1);
    qint64 id = 10;
    for (Edit e : plan.add) {
        e.id = ++id;
        after.append(e);
    }
    const Result r = apply(deviceEvents(), after, oneSession());
    QCOMPARE(r.delta.value(CPAP_Obstructive), -2);   // none left
    QCOMPARE(r.delta.value(CPAP_ClearAirway), -1);
    QCOMPARE(r.delta.value(CPAP_Hypopnea), 4);       // …and added again as a hypopnea
}

void ManualScoringTests::testBulkRemoveAll()
{
    const Result before = apply(deviceEvents(), { edit(1, Kind::Add, CPAP_Obstructive, 480 * kSec, 500 * kSec) }, oneSession());
    const BulkPlan plan = bulkEdits(before, CPAP_Obstructive, 0);
    QCOMPARE(plan.count, 3);
    QCOMPARE(plan.undo, QList<qint64>({ 1 }));
    QCOMPARE(plan.add.size(), 2);
    for (const Edit &e : plan.add) {
        QCOMPARE(int(e.kind), int(Kind::Remove));
        QCOMPARE(e.channel, CPAP_Obstructive);
    }
    QList<Edit> after;
    qint64 id = 10;
    for (Edit e : plan.add) {
        e.id = ++id;
        after.append(e);
    }
    QCOMPARE(apply(deviceEvents(), after, oneSession()).delta.value(CPAP_Obstructive), -2);
}

// stepping through the events of one type, as the doctor reviews them: by event, so two events
// that end at the same moment are both visited
void ManualScoringTests::testNextEvent()
{
    QList<DeviceEvent> device = deviceEvents();
    device.append({ CPAP_Hypopnea, 300 * kSec, 20 });   // ends with the CA at 300 s
    const Result r = apply(device, { edit(1, Kind::Remove, CPAP_Obstructive, 188 * kSec, 200 * kSec) }, oneSession());
    auto end = [&](int i) { return i < 0 ? qint64(-1) : r.events.at(i).endMs; };
    // OA at 100 s and 200 s (removed, still visited to review it), CA at 300 s, H at 300 s and 400 s
    int i = stepEvent(r.events, CPAP_Obstructive, 0, -1, true);
    QCOMPARE(end(i), qint64(100 * kSec));
    i = stepEvent(r.events, CPAP_Obstructive, end(i), i, true);
    QCOMPARE(end(i), qint64(200 * kSec));
    QCOMPARE(stepEvent(r.events, CPAP_Obstructive, end(i), i, true), -1);
    QCOMPARE(end(stepEvent(r.events, CPAP_Obstructive, end(i), i, false)), qint64(100 * kSec));
    // any scored type: both events ending at 300 s are visited
    i = stepEvent(r.events, 0, 250 * kSec, -1, true);
    QCOMPARE(end(i), qint64(300 * kSec));
    const int j = stepEvent(r.events, 0, end(i), i, true);
    QCOMPARE(end(j), qint64(300 * kSec));
    QVERIFY(j != i);
    QCOMPARE(end(stepEvent(r.events, 0, end(j), j, true)), qint64(400 * kSec));
}

// how many events of a type count (not removed, not in an excluded stretch)
void ManualScoringTests::testCountOf()
{
    const Result r = apply(deviceEvents(), { edit(1, Kind::Remove, CPAP_Obstructive, 188 * kSec, 200 * kSec),
                                              edit(2, Kind::Exclude, 0, 290 * kSec, 310 * kSec) }, oneSession());
    QCOMPARE(countOf(r.events, CPAP_Obstructive), 1);
    QCOMPARE(countOf(r.events, CPAP_ClearAirway), 0);
    QCOMPARE(countOf(r.events, 0), 2);   // OA@100, H@400
}

// changing all OA back to CA undoes an earlier CA → OA instead of adding "CA → CA"
void ManualScoringTests::testBulkRetypeBackUndoes()
{
    const Result r = apply(deviceEvents(), { edit(5, Kind::Retype, CPAP_ClearAirway, 289 * kSec, 300 * kSec, CPAP_Obstructive) }, oneSession());
    const BulkPlan plan = bulkEdits(r, CPAP_Obstructive, CPAP_ClearAirway);
    QVERIFY(plan.undo.contains(5));
    for (const Edit &e : plan.add) QVERIFY(!(e.kind == Kind::Retype && e.newChannel == e.channel));
    QCOMPARE(plan.add.size(), 2);   // the two device OAs
}

// devices that split hypopneas by mechanism (Prisma: OH/CH) lose those in an excluded stretch too
void ManualScoringTests::testExcludeDropsMechanismHypopneas()
{
    QList<DeviceEvent> device = deviceEvents();
    device.append({ CPAP_ObstructiveHypopnea, 250 * kSec, 14 });
    device.append({ CPAP_CentralHypopnea, 500 * kSec, 14 });
    const Result r = apply(device, { edit(1, Kind::Exclude, 0, 150 * kSec, 350 * kSec) }, oneSession());
    QCOMPARE(r.delta.value(CPAP_ObstructiveHypopnea), -1);
    QCOMPARE(r.delta.value(CPAP_CentralHypopnea), 0);
    // a RERA (or OH/CH) is never a step target, nor offered for editing
    device.append({ CPAP_RERA, 260 * kSec, 8 });
    const Result all = apply(device, {}, oneSession());
    QCOMPARE(all.events.at(stepEvent(all.events, 0, 240 * kSec, -1, true)).endMs, qint64(300 * kSec));
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
    QCOMPARE(DatabaseSchema::getSchemaVersion(db), DatabaseSchema::CURRENT_SCHEMA_VERSION);
    QVERIFY(db.tables().contains(QStringLiteral("manual_scoring")));
    QVERIFY(db.tables().contains(QStringLiteral("manual_scoring_summary")));
}

// which sessions have manual scoring, known without a query per session
void ManualScoringTests::testHasScoringFollowsEdits()
{
    QVERIFY(!ManualScoringRepository::hasScoring(key(201)));
    Edit e = edit(0, Kind::Add, CPAP_Hypopnea, 1, 2);
    e.key = key(201);
    const qint64 id = ManualScoringRepository::add(e);
    QVERIFY(ManualScoringRepository::hasScoring(key(201)));
    QVERIFY(!ManualScoringRepository::hasScoring(key(202)));
    QVERIFY(ManualScoringRepository::remove(id));
    QVERIFY(!ManualScoringRepository::hasScoring(key(201)));
    e.key = key(203);
    ManualScoringRepository::add(e);
    QVERIFY(ManualScoringRepository::removeAllForSession(key(203)));
    QVERIFY(!ManualScoringRepository::hasScoring(key(203)));
}

// a cache read that fails is tried again next time, not kept as "no scoring"
void ManualScoringTests::testFailedCacheReadIsNotKept()
{
    Edit e = edit(0, Kind::Add, CPAP_Hypopnea, 1, 2);
    e.key = key(301);
    ManualScoringRepository::add(e);
    ManualScoringRepository::remove(-1);   // clears the cache
    QSqlQuery q(DatabaseManager::instance().database());
    QVERIFY(q.exec(QStringLiteral("ALTER TABLE manual_scoring RENAME TO manual_scoring_away")));
    QVERIFY(!ManualScoringRepository::hasScoring(key(301)));   // the read fails
    QVERIFY(q.exec(QStringLiteral("ALTER TABLE manual_scoring_away RENAME TO manual_scoring")));
    QVERIFY(ManualScoringRepository::hasScoring(key(301)));
    ManualScoringRepository::removeAllForSession(key(301));
}

void ManualScoringTests::testResize()
{
    const Result r = apply(deviceEvents(), { resize(7, 90 * kSec, 106 * kSec) }, oneSession());
    const EffectiveEvent *e = eventMatching(r, 100 * kSec);
    QVERIFY(e);
    QCOMPARE(e->endMs, 106 * kSec);
    QCOMPARE(e->durationSec, 16.0);
    QCOMPARE(e->originalDurationSec, 12.0);
    QCOMPARE(e->resizeEditId, qint64(7));
    QCOMPARE(e->origin, Origin::Device);
    QVERIFY(r.delta.isEmpty());
    QVERIFY(r.notFound.isEmpty());
}

// the event counts at its new end: inside an excluded stretch it is left out, outside again counted
void ManualScoringTests::testResizeIntoExclude()
{
    const Edit exclude = edit(1, Kind::Exclude, 0, 105 * kSec, 150 * kSec);
    Result r = apply(deviceEvents(), { exclude, resize(2, 90 * kSec, 106 * kSec) }, oneSession());
    QCOMPARE(r.delta.value(CPAP_Obstructive), -1);
    r = apply(deviceEvents(), { exclude, resize(2, 90 * kSec, 104 * kSec) }, oneSession());
    QCOMPARE(r.delta.value(CPAP_Obstructive), 0);
}

// a retype and new bounds of one event, in either order
void ManualScoringTests::testResizeAndRetype()
{
    const Edit retype = edit(0, Kind::Retype, CPAP_Obstructive, 88 * kSec, 100 * kSec, CPAP_Hypopnea);
    for (bool retypeFirst : { true, false }) {
        Edit a = retype, b = resize(0, 90 * kSec, 106 * kSec);
        a.id = retypeFirst ? 1 : 2;
        b.id = retypeFirst ? 2 : 1;
        const Result r = apply(deviceEvents(), { a, b }, oneSession());
        const EffectiveEvent *e = eventMatching(r, 100 * kSec);
        QVERIFY(e);
        QCOMPARE(e->channel, CPAP_Hypopnea);
        QCOMPARE(e->endMs, 106 * kSec);
        QCOMPARE(r.delta.value(CPAP_Hypopnea), 1);
        QVERIFY(r.notFound.isEmpty());
    }
}

void ManualScoringTests::testResizeNotFound()
{
    const Result r = apply(deviceEvents(), { resize(3, 890 * kSec, 906 * kSec, 900 * kSec) }, oneSession());
    QCOMPARE(r.notFound, QList<qint64>({ 3 }));
}

void ManualScoringTests::testExcludeEditsListed()
{
    const Result r = apply(deviceEvents(), { edit(1, Kind::Exclude, 0, 150 * kSec, 250 * kSec),
                                             edit(2, Kind::Exclude, 0, 200 * kSec, 350 * kSec) }, oneSession());
    QCOMPARE(r.excludeEdits.size(), 2);
    QCOMPARE(r.excludeEdits.at(0).editId, qint64(1));
    QCOMPARE(r.excludeEdits.at(0).startMs, 150 * kSec);
    QCOMPARE(r.excludeEdits.at(1).endMs, 350 * kSec);
    QCOMPARE(r.excludedSpans.size(), 1);   // merged as before
}

// schema 23: the device event's own end of a Resize edit
void ManualScoringTests::testMigration22To23()
{
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("DROP TABLE manual_scoring")));
    QVERIFY(q.exec(QStringLiteral("CREATE TABLE manual_scoring (id INTEGER PRIMARY KEY AUTOINCREMENT, profile_id INTEGER NOT NULL, "
                                  "machine_serial TEXT NOT NULL, session_id INTEGER NOT NULL, kind TEXT NOT NULL, "
                                  "channel INTEGER NOT NULL DEFAULT 0, new_channel INTEGER NOT NULL DEFAULT 0, start_ms INTEGER NOT NULL, "
                                  "end_ms INTEGER NOT NULL, note TEXT, created_at TEXT NOT NULL)")));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO manual_scoring (profile_id, machine_serial, session_id, kind, start_ms, end_ms, created_at) "
                                  "VALUES (1, 'S', 5, 'exclude', 10, 20, '2026-10-07T08:00:00')")));
    QVERIFY(q.exec(QStringLiteral("UPDATE schema_version SET version = 22")));
    QVERIFY(DatabaseSchema::upgradeSchema(db, 22));
    QCOMPARE(DatabaseSchema::getSchemaVersion(db), 23);
    QVERIFY(db.record(QStringLiteral("manual_scoring")).contains(QStringLiteral("match_end_ms")));
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM manual_scoring")) && q.next());
    QCOMPARE(q.value(0).toInt(), 1);
    QVERIFY(q.exec(QStringLiteral("DELETE FROM manual_scoring")));
}

void ManualScoringTests::testStoreResize()
{
    Edit e = edit(0, Kind::Resize, CPAP_Obstructive, 90 * kSec, 106 * kSec);
    e.matchEndMs = 100 * kSec;
    e.key = key(131);
    e.id = ManualScoringRepository::add(e);
    QVERIFY(e.id > 0);
    const QList<Edit> loaded = ManualScoringRepository::editsForSession(key(131));
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded[0].kind, Kind::Resize);
    QCOMPARE(loaded[0].matchEndMs, 100 * kSec);
    QVERIFY(ManualScoringRepository::update(e.id, 92 * kSec, 108 * kSec));
    const Edit back = ManualScoringRepository::editsForSession(key(131)).first();
    QCOMPARE(back.startMs, 92 * kSec);
    QCOMPARE(back.endMs, 108 * kSec);
    QCOMPARE(back.matchEndMs, 100 * kSec);
    QVERIFY(ManualScoringRepository::removeAllForSession(key(131)));
}
