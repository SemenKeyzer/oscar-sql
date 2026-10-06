/* Backup / Restore Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "backuprestoretests.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/machine_common.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "database/database_manager.h"
#include "database/profile_repository.h"
#include "database/backup/profile_backup.h"
#include "database/backup/profile_restore.h"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "SleepLib/thirdparty/miniz.h"
#include "zip.h"

namespace {

const QString kProfileName = QStringLiteral("Alice Smith");
const QString kSerial      = QStringLiteral("SN-ALICE-123");
const QString kNote        = QStringLiteral("my private journal note");
const QString kCardPath    = QStringLiteral("/home/alice/sdcard");

bool exec(const QString& sql)
{
    QSqlQuery q(DatabaseManager::instance().database());
    if (!q.exec(sql)) {
        qWarning() << "BackupRestoreTests:" << sql << q.lastError().text();
        return false;
    }
    return true;
}

qint64 insertMachine(qint64 profileId, int machineId, int type, const QString& serial)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral(
        "INSERT INTO machines (profile_id, machine_id, loader_name, machine_type, serial_number, properties) "
        "VALUES (?, ?, 'Test', ?, ?, ?)"));
    q.addBindValue(profileId);
    q.addBindValue(machineId);
    q.addBindValue(type);
    q.addBindValue(serial);
    q.addBindValue(QStringLiteral("{\"serial\":\"%1\"}").arg(serial));
    if (!q.exec()) return 0;
    return q.lastInsertId().toLongLong();
}

qint64 insertSession(qint64 machineDbId, int sessionId, qint64 startMs)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral(
        "INSERT INTO sessions (session_id, machine_id, start_time, end_time, duration) VALUES (?, ?, ?, ?, 3600)"));
    q.addBindValue(sessionId);
    q.addBindValue(machineDbId);
    q.addBindValue(startMs);
    q.addBindValue(startMs + 3600000);
    if (!q.exec()) return 0;
    return q.lastInsertId().toLongLong();
}

// All text in an extracted package: manifest and SQL files.
QString packageText(const QString& packagePath, const QString& extractDir)
{
    UnzipFile zip;
    if (!zip.Open(packagePath) || !zip.ExtractAll(extractDir)) return QString();
    zip.Close();
    QString text;
    QDirIterator it(extractDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile f(it.next());
        if (f.open(QIODevice::ReadOnly)) text += QString::fromUtf8(f.readAll());
    }
    return text;
}

} // namespace

void BackupRestoreTests::initTestCase()
{
    // QtSql needs an application object; other suites delete theirs in cleanupTestCase().
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QCoreApplication(argc, argv);
    }
    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-backuprestoretests-XXXXXX"));
    QVERIFY(m_tempDir->isValid());
    m_previousAppData = GetAppData();
    SetAppData(m_tempDir->path());

    p_profile = nullptr;
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));
    schema::init();
    Profiles::Scan();

    const QString profileDir = m_tempDir->path() + QStringLiteral("/Profiles/") + kProfileName;
    p_profile = Profiles::Create(kProfileName, &profileDir);
    QVERIFY(p_profile != nullptr);
    m_profileId = ProfileRepository().findByUsername(kProfileName).id;
    QVERIFY(m_profileId > 0);
    p_profile->setDatabaseId(m_profileId);

    // A CPAP with a session, and a journal with a private note.
    const qint64 cpap    = insertMachine(m_profileId, 1001, MT_CPAP, kSerial);
    const qint64 journal = insertMachine(m_profileId, 1002, MT_JOURNAL, QString());
    QVERIFY(cpap > 0 && journal > 0);
    const qint64 start = QDateTime(QDate(2026, 9, 1), QTime(23, 0)).toMSecsSinceEpoch();
    QVERIFY(insertSession(cpap, 1, start) > 0);
    const qint64 journalSession = insertSession(journal, 2, start);
    QVERIFY(journalSession > 0);
    QVERIFY(exec(QStringLiteral("INSERT INTO session_settings (session_id, profile_id, channel_id, value, json_value) "
                                "VALUES (%1, %2, 4096, 0, '%3')").arg(journalSession).arg(m_profileId).arg(kNote)));
    QVERIFY(exec(QStringLiteral("INSERT INTO profile_preferences (profile_id, category, key, value) "
                                "VALUES (%1, 'profile', 'LastCPAPPath', '%2')").arg(m_profileId).arg(kCardPath)));
    QVERIFY(exec(QStringLiteral("INSERT INTO profile_preferences (profile_id, category, key, value) "
                                "VALUES (%1, 'profile', 'SomeFolder', 'C:\\Users\\alice\\x')").arg(m_profileId)));
}

void BackupRestoreTests::testProfileNameProblem_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("ok");
    QTest::newRow("plain")        << QStringLiteral("Alice") << true;
    QTest::newRow("space")        << QStringLiteral("Alice Smith") << true;
    QTest::newRow("shared")       << QStringLiteral("Bob (Shared)") << true;
    QTest::newRow("unicode")      << QStringLiteral("Семён") << true;
    QTest::newRow("empty")        << QString() << false;
    QTest::newRow("blank")        << QStringLiteral("   ") << false;
    QTest::newRow("dot")          << QStringLiteral(".") << false;
    QTest::newRow("dotdot")       << QStringLiteral("..") << false;
    QTest::newRow("traversal")    << QStringLiteral("../../x") << false;
    QTest::newRow("slash")        << QStringLiteral("a/b") << false;
    QTest::newRow("backslash")    << QStringLiteral("a\\b") << false;
    QTest::newRow("drive")        << QStringLiteral("C:x") << false;
    QTest::newRow("trailing dot") << QStringLiteral("name.") << false;
    QTest::newRow("leading space")<< QStringLiteral(" name") << false;
    QTest::newRow("control")      << QStringLiteral("a\nb") << false;
    QTest::newRow("reserved")     << QStringLiteral("con") << false;
    QTest::newRow("reserved ext") << QStringLiteral("LPT1.txt") << false;
    QTest::newRow("not reserved") << QStringLiteral("Console") << true;
}

void BackupRestoreTests::testProfileNameProblem()
{
    QFETCH(QString, name);
    QFETCH(bool, ok);
    QCOMPARE(Profiles::nameProblem(name).isEmpty(), ok);
}

void BackupRestoreTests::testReadOnlyScopeBlocksWrites()
{
    const QString conn = QStringLiteral("readonly-scope-test");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE t (v INTEGER)")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO t VALUES (1)")));
        {
            ReadOnlyScope readOnly(db);
            QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM t")));
            QVERIFY(!q.exec(QStringLiteral("DELETE FROM t")));
            QVERIFY(!q.exec(QStringLiteral("WITH a AS (SELECT 1) DELETE FROM t")));
        }
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM t")) && q.next());
        QCOMPARE(q.value(0).toInt(), 1);
        QVERIFY(q.exec(QStringLiteral("DELETE FROM t")));        // writable again
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
}

QString BackupRestoreTests::makePackage(bool privacy, const QString& subdir)
{
    const QString outDir = m_tempDir->path() + QLatin1Char('/') + subdir;
    QDir().mkpath(outDir);
    ProfileBackup backup(m_profileId);
    backup.setOutputPath(outDir);
    backup.setPrivacyMode(privacy);
    backup.setIncludeSDData(false);
    backup.setFilename(QStringLiteral("package.oscar"));
    if (!backup.createBackup()) {
        qWarning() << "BackupRestoreTests: backup failed:" << backup.getErrorMessage();
        return QString();
    }
    return backup.getBackupPath();
}

void BackupRestoreTests::testPrivacyPackageLeavesOutPersonalData()
{
    const QString normal = makePackage(false, QStringLiteral("normal"));
    QVERIFY(!normal.isEmpty());
    const QString normalText = packageText(normal, m_tempDir->path() + QStringLiteral("/normal-x"));
    // Sanity: an ordinary backup does carry them.
    QVERIFY(normalText.contains(kProfileName));
    QVERIFY(normalText.contains(kSerial));
    QVERIFY(normalText.contains(kNote));

    const QString shared = makePackage(true, QStringLiteral("private"));
    QVERIFY(!shared.isEmpty());
    const QString text = packageText(shared, m_tempDir->path() + QStringLiteral("/private-x"));
    QVERIFY(!text.isEmpty());
    QVERIFY2(!text.contains(kProfileName), "profile name leaked");
    QVERIFY2(!text.contains(kSerial), "serial number leaked");
    QVERIFY2(!text.contains(kNote), "journal note leaked");
    QVERIFY2(!text.contains(kCardPath), "folder path leaked");
    QVERIFY2(!text.contains(QStringLiteral("alice")), "folder path with the account name leaked");
    QVERIFY(text.contains(QStringLiteral("p%1").arg(m_profileId)));
}

void BackupRestoreTests::testRestoreRejectsUnsafeProfileName()
{
    const QString package = makePackage(false, QStringLiteral("unsafe"));
    QVERIFY(!package.isEmpty());

    ProfileRestore restore(package);
    QVERIFY2(restore.validatePackage(), qPrintable(restore.getErrorMessage()));
    QVERIFY(restore.checkCompatibility());
    restore.setNewUsername(QStringLiteral("../evil"));
    restore.setConflictResolution(ConflictResolution::Rename);
    QVERIFY(!restore.restoreProfile());
    QVERIFY(restore.getErrorMessage().contains(QStringLiteral("../evil")));

    QVERIFY(!QDir(m_tempDir->path() + QStringLiteral("/evil")).exists());
    QSqlQuery q(DatabaseManager::instance().database());
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM profiles WHERE username = '../evil'")) && q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

void BackupRestoreTests::testRestoreRebuildsDataFolder()
{
    const QString package = makePackage(false, QStringLiteral("rename"));
    QVERIFY(!package.isEmpty());

    ProfileRestore restore(package);
    QVERIFY2(restore.validatePackage(), qPrintable(restore.getErrorMessage()));
    QVERIFY(restore.checkCompatibility());
    restore.setNewUsername(QStringLiteral("Bob"));
    restore.setConflictResolution(ConflictResolution::Rename);
    QVERIFY2(restore.restoreProfile(), qPrintable(restore.getErrorMessage()));

    QSqlQuery q(DatabaseManager::instance().database());
    QVERIFY(q.exec(QStringLiteral("SELECT data_folder FROM profiles WHERE username = 'Bob'")) && q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("%PROFDIR%/Bob"));
    QVERIFY(QDir(p_pref->Get(QStringLiteral("{home}/Profiles")) + QStringLiteral("/Bob")).exists());

    // The restored rows belong to the new profile only; the original keeps its own.
    const qint64 bobId = restore.getRestoredProfileId();
    QVERIFY(bobId > 0 && bobId != m_profileId);
    auto machineCount = [&q](qint64 profileId) {
        return (q.exec(QStringLiteral("SELECT COUNT(*) FROM machines WHERE profile_id = %1").arg(profileId)) && q.next())
                   ? q.value(0).toInt() : -1;
    };
    QVERIFY(machineCount(m_profileId) >= 2);
    QCOMPARE(machineCount(bobId), machineCount(m_profileId));
}

void BackupRestoreTests::cleanupTestCase()
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

namespace {

bool writeOneEntryZip(const QString &zipPath, const char *entryName)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) return false;
    void *buf = nullptr;
    size_t size = 0;
    bool ok = mz_zip_writer_add_mem(&zip, entryName, "x", 1, MZ_DEFAULT_COMPRESSION)
           && mz_zip_writer_finalize_heap_archive(&zip, &buf, &size);
    if (ok) {
        QFile out(zipPath);
        ok = out.open(QIODevice::WriteOnly) && out.write(static_cast<const char *>(buf), qint64(size)) == qint64(size);
    }
    mz_free(buf);
    mz_zip_writer_end(&zip);
    return ok;
}

bool extract(const QString &zipPath, const QString &destDir)
{
    UnzipFile zip;
    if (!zip.Open(zipPath)) return false;
    const bool ok = zip.ExtractAll(destDir);
    zip.Close();
    return ok;
}

} // namespace

// The temp folder the restore extracts into is reached through a symlink on macOS
// (/var -> /private/var). Ordinary entries must still extract there, while an entry that
// climbs out of the folder is refused and nothing is written outside it.
// the doctor's manual scoring travels with the profile, keyed by the device's own session number
void BackupRestoreTests::testBackupKeepsManualScoring()
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("INSERT INTO manual_scoring (profile_id, machine_serial, session_id, kind, channel, new_channel, start_ms, end_ms, note, created_at) "
                             "VALUES (?, 'SN1', 424242, 'exclude', 0, 0, 1000, 2000, 'awake', '2026-10-07T08:00:00')"));
    q.addBindValue(m_profileId);
    QVERIFY(q.exec());
    q.prepare(QStringLiteral("INSERT INTO manual_scoring_summary (profile_id, machine_serial, session_id, deltas, excluded_ms, not_found) "
                             "VALUES (?, 'SN1', 424242, '', 1000, 0)"));
    q.addBindValue(m_profileId);
    QVERIFY(q.exec());

    const QString package = makePackage(false, QStringLiteral("scoring"));
    QVERIFY(!package.isEmpty());
    ProfileRestore restore(package);
    QVERIFY2(restore.validatePackage(), qPrintable(restore.getErrorMessage()));
    QVERIFY(restore.checkCompatibility());
    restore.setNewUsername(QStringLiteral("Carol"));
    restore.setConflictResolution(ConflictResolution::Rename);
    QVERIFY2(restore.restoreProfile(), qPrintable(restore.getErrorMessage()));
    const qint64 carol = restore.getRestoredProfileId();
    QVERIFY(carol > 0 && carol != m_profileId);

    q.prepare(QStringLiteral("SELECT session_id, note FROM manual_scoring WHERE profile_id = ?"));
    q.addBindValue(carol);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toLongLong(), qint64(424242));   // the device's number, not remapped
    QCOMPARE(q.value(1).toString(), QStringLiteral("awake"));
    q.prepare(QStringLiteral("SELECT excluded_ms FROM manual_scoring_summary WHERE profile_id = ? AND session_id = 424242"));
    q.addBindValue(carol);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toLongLong(), qint64(1000));
}

void BackupRestoreTests::testExtractKeepsEntriesInsideRoot()
{
    QTemporaryDir tmp(QDir::tempPath() + QStringLiteral("/oscar-zipslip-XXXXXX"));
    QVERIFY(tmp.isValid());
    const QString good = tmp.path() + QStringLiteral("/good.zip");
    const QString evil = tmp.path() + QStringLiteral("/evil.zip");
    QVERIFY(writeOneEntryZip(good, "data/file.txt"));
    QVERIFY(writeOneEntryZip(evil, "../escaped.txt"));

    const QString root = tmp.path() + QStringLiteral("/root");
    QVERIFY(QDir().mkpath(root));
    QVERIFY(extract(good, root));
    QVERIFY(QFile::exists(root + QStringLiteral("/data/file.txt")));

    const QString root2 = tmp.path() + QStringLiteral("/root2");
    QVERIFY(QDir().mkpath(root2));
    QVERIFY(!extract(evil, root2));
    QVERIFY(!QFile::exists(tmp.path() + QStringLiteral("/escaped.txt")));
}
