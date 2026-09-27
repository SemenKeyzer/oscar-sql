/* Time Alignment Session Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "timealignsessiontests.h"
#include "timealignsession.h"
#include "SleepLib/machine.h"
#include "database/database_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <cmath>

namespace {

const QDate kNight(2026, 9, 24);
const qint64 kOxiDbId = 10;

DeviceTimeCorrectionData row(qint64 machineId, const QString& from, const QString& to,
                             const QString& type, qint64 offsetMs)
{
    DeviceTimeCorrectionData d;
    d.machineId = machineId;
    d.dateFrom  = from;
    d.dateTo    = to;
    d.type      = type;
    d.offsetMs  = offsetMs;
    return d;
}

// In-memory stand-in for the repository with the same upsert semantics
// (the previous row for the night is marked undone, 0 only removes).
class FakeStore : public TimeAlignStore
{
public:
    QList<DeviceTimeCorrectionData> rows;
    int  writes = 0;
    bool failWrites = false;

    QList<DeviceTimeCorrectionData> findActive(qint64 machineId) override
    {
        QList<DeviceTimeCorrectionData> out;
        for (const auto& d : rows) {
            if (d.machineId == machineId && d.undoneAt.isEmpty()) out.append(d);
        }
        return out;
    }

    QList<DeviceTimeCorrectionData> findManualOffsetRows(qint64 machineId) override
    {
        QList<DeviceTimeCorrectionData> out;
        for (const auto& d : findActive(machineId)) {
            if (d.type == QLatin1String("offset") && d.dateFrom == d.dateTo) out.append(d);
        }
        return out;
    }

    bool upsertOffset(qint64 machineId, const QString& date, qint64 offsetMs) override
    {
        if (failWrites) return false;
        ++writes;
        for (auto& d : rows) {
            if (d.machineId == machineId && d.undoneAt.isEmpty() && d.type == QLatin1String("offset")
                    && d.dateFrom == date && d.dateTo == date) {
                d.undoneAt = QStringLiteral("undone");
            }
        }
        if (offsetMs != 0) rows.append(row(machineId, date, date, QStringLiteral("offset"), offsetMs));
        return true;
    }

    QList<DeviceTimeCorrectionData> nightRows(qint64 machineId, const QString& date)
    {
        QList<DeviceTimeCorrectionData> out;
        for (const auto& d : findManualOffsetRows(machineId)) {
            if (d.dateFrom == date) out.append(d);
        }
        return out;
    }
};

} // namespace

void TimeAlignSessionTests::initTestCase()
{
    // QtSql needs an application object; other suites delete theirs in cleanupTestCase().
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QCoreApplication(argc, argv);
    }
}

void TimeAlignSessionTests::testBeginReadsSavedAndOtherCorrections()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-24", "2026-09-24", "offset", 600000),
                    row(kOxiDbId, "2026-09-01", "", "travel", 3600000),
                    row(99, "2026-09-24", "2026-09-24", "offset", 5) };   // another device
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);

    QVERIFY(s.begin(&mach, kNight));
    QVERIFY(s.isActive());
    QCOMPARE(s.savedMs(), qint64(600000));
    QCOMPARE(s.offsetMs(), qint64(600000));
    QVERIFY(!s.isDirty());
    QCOMPARE(s.otherCorrectionsMs(), qint64(3600000));
    QCOMPARE(mach.correctionMs(kNight), qint64(4200000));
}

void TimeAlignSessionTests::testBeginRejectsMachineWithoutDatabaseId()
{
    TimeAlignSession s(new FakeStore);
    Machine mach(nullptr, 2);                     // never saved: database id 0
    QVERIFY(!s.begin(&mach, kNight));
    QVERIFY(!s.isActive());
}

void TimeAlignSessionTests::testPreviewDoesNotWrite()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-01", "", "travel", 3600000) };
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    QSignalSpy spy(&s, &TimeAlignSession::offsetChanged);
    s.setOffsetMs(900000);

    QCOMPARE(store->writes, 0);
    QVERIFY(s.isDirty());
    QCOMPARE(mach.correctionMs(kNight), qint64(4500000));
    QCOMPARE(mach.correctionMs(kNight.addDays(1)), qint64(3600000));   // other nights untouched
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toLongLong(), qint64(900000));
}

void TimeAlignSessionTests::testCommitReplacesNightRow()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-24", "2026-09-24", "offset", 600000) };
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    s.setOffsetMs(900000);
    QVERIFY(s.commit());

    QCOMPARE(store->writes, 1);
    const auto rows = store->nightRows(kOxiDbId, "2026-09-24");
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().offsetMs, qint64(900000));
    QVERIFY(!s.isDirty());
    QCOMPARE(s.savedMs(), qint64(900000));
    QCOMPARE(mach.correctionMs(kNight), qint64(900000));
}

void TimeAlignSessionTests::testCommitWithoutChangeDoesNotWrite()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-24", "2026-09-24", "offset", 600000) };
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    QVERIFY(s.commit());
    QCOMPARE(store->writes, 0);
}

void TimeAlignSessionTests::testZeroCommitRemovesNightRow()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-24", "2026-09-24", "offset", 600000) };
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    s.setOffsetMs(0);
    QVERIFY(s.commit());
    QVERIFY(store->nightRows(kOxiDbId, "2026-09-24").isEmpty());
    QCOMPARE(mach.correctionMs(kNight), qint64(0));
}

void TimeAlignSessionTests::testCancelRestoresStoredCorrections()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-24", "2026-09-24", "offset", 600000),
                    row(kOxiDbId, "2026-09-01", "", "travel", 3600000) };
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    s.setOffsetMs(-300000);
    QCOMPARE(mach.correctionMs(kNight), qint64(3300000));
    s.cancel();

    QCOMPARE(s.offsetMs(), qint64(600000));
    QVERIFY(!s.isDirty());
    QCOMPARE(mach.correctionMs(kNight), qint64(4200000));
    QCOMPARE(store->writes, 0);
}

void TimeAlignSessionTests::testCommitFailureKeepsPreview()
{
    auto *store = new FakeStore;
    store->failWrites = true;
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    s.setOffsetMs(900000);
    QVERIFY(!s.commit());
    QVERIFY(s.isDirty());
    QCOMPARE(mach.correctionMs(kNight), qint64(900000));
}

void TimeAlignSessionTests::testBeginOnAnotherMachineRestoresFirst()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-24", "2026-09-24", "offset", 600000) };
    TimeAlignSession s(store);
    Machine oxi(nullptr, 1);
    oxi.setDatabaseId(kOxiDbId);
    Machine other(nullptr, 2);
    other.setDatabaseId(11);

    QVERIFY(s.begin(&oxi, kNight));
    s.setOffsetMs(1200000);
    QCOMPARE(oxi.correctionMs(kNight), qint64(1200000));

    QVERIFY(s.begin(&other, kNight));
    QCOMPARE(oxi.correctionMs(kNight), qint64(600000));   // first device's preview dropped
    QCOMPARE(s.machine(), &other);
    QCOMPARE(store->writes, 0);
}

void TimeAlignSessionTests::testPreviousNightOffset()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-20", "2026-09-20", "offset", 300000),
                    row(kOxiDbId, "2026-09-22", "2026-09-22", "offset", -120000),
                    row(kOxiDbId, "2026-09-23", "", "travel", 3600000),     // ranges don't count
                    row(kOxiDbId, "2026-09-25", "2026-09-25", "offset", 999) };
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);

    QVERIFY(s.begin(&mach, kNight));
    QVERIFY(s.previousNightOffset().has_value());
    QCOMPARE(*s.previousNightOffset(), qint64(-120000));

    QVERIFY(s.begin(&mach, QDate(2026, 9, 19)));
    QVERIFY(!s.previousNightOffset().has_value());
}

void TimeAlignSessionTests::testOffsetIsClamped()
{
    TimeAlignSession s(new FakeStore);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    s.setOffsetMs(13LL * 3600 * 1000);
    QCOMPARE(s.offsetMs(), TimeAlignSession::kMaxOffsetMs);
    s.nudge(-30LL * 3600 * 1000);
    QCOMPARE(s.offsetMs(), -TimeAlignSession::kMaxOffsetMs);
    s.setOffsetMs(0);
    s.nudge(600000);
    s.nudge(600000);
    QCOMPARE(s.offsetMs(), qint64(1200000));
}

void TimeAlignSessionTests::testSnapStep()
{
    QCOMPARE(TimeAlignSession::snapStepMs(30000.0), qint64(60000));   // whole night on screen
    QCOMPARE(TimeAlignSession::snapStepMs(20001.0), qint64(60000));
    QCOMPARE(TimeAlignSession::snapStepMs(5000.0),  qint64(10000));   // 1-2 hours on screen
    QCOMPARE(TimeAlignSession::snapStepMs(2000.0),  qint64(1000));
    QCOMPARE(TimeAlignSession::snapStepMs(100.0),   qint64(1000));
    QCOMPARE(TimeAlignSession::snapStepMs(0.0),     qint64(1000));
}

void TimeAlignSessionTests::testSnapDelta()
{
    QCOMPARE(TimeAlignSession::snapDelta(95000.0, 30000.0),  qint64(120000));
    QCOMPARE(TimeAlignSession::snapDelta(-95000.0, 30000.0), qint64(-120000));
    QCOMPARE(TimeAlignSession::snapDelta(14999.0, 5000.0),   qint64(10000));
    QCOMPARE(TimeAlignSession::snapDelta(-1499.0, 100.0),    qint64(-1000));
    QCOMPARE(TimeAlignSession::snapDelta(0.0, 0.0),          qint64(0));
    QCOMPARE(TimeAlignSession::snapDelta(std::nan(""), 100.0), qint64(0));
}

void TimeAlignSessionTests::testFormatOffset()
{
    QCOMPARE(TimeAlignSession::formatOffset(0),        QStringLiteral("+00:00:00"));
    QCOMPARE(TimeAlignSession::formatOffset(630000),   QStringLiteral("+00:10:30"));
    QCOMPARE(TimeAlignSession::formatOffset(-3600000), QStringLiteral("-01:00:00"));
    QCOMPARE(TimeAlignSession::formatOffset(TimeAlignSession::kMaxOffsetMs), QStringLiteral("+12:00:00"));
}

// The real repository, against a fresh temporary database.
void TimeAlignSessionTests::testRepositoryStoreRoundTrip()
{
    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }
    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-timealigntests-XXXXXX"));
    QVERIFY(m_tempDir->isValid());
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));

    QSqlQuery q(DatabaseManager::instance().database());
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO profiles (username, data_folder) VALUES ('timealign', '/tmp/timealign')")));
    const qint64 profileId = q.lastInsertId().toLongLong();
    QVERIFY(q.prepare(QStringLiteral(
        "INSERT INTO machines (profile_id, machine_id, loader_name, machine_type) "
        "VALUES (:profile_id, 1, 'MD300W1', :type)")));
    q.bindValue(QStringLiteral(":profile_id"), profileId);
    q.bindValue(QStringLiteral(":type"), int(MT_OXIMETER));
    QVERIFY(q.exec());
    const qint64 machineId = q.lastInsertId().toLongLong();

    RepositoryTimeAlignStore store;
    QVERIFY(store.upsertOffset(machineId, QStringLiteral("2026-09-24"), 600000));
    QVERIFY(store.upsertOffset(machineId, QStringLiteral("2026-09-24"), 900000));
    const auto active = store.findActive(machineId);
    QCOMPARE(active.size(), 1);
    QCOMPARE(active.first().offsetMs, qint64(900000));
    QCOMPARE(store.findManualOffsetRows(machineId).size(), 1);

    QVERIFY(store.upsertOffset(machineId, QStringLiteral("2026-09-24"), 0));
    QVERIFY(store.findActive(machineId).isEmpty());
}

void TimeAlignSessionTests::cleanupTestCase()
{
    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }
    delete m_tempDir;
    m_tempDir = nullptr;
    delete m_app;
    m_app = nullptr;
}
