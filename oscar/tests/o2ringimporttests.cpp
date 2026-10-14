/* O2Ring import Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "o2ringimporttests.h"
#include "o2ringtestdata.h"
#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/loader_plugins/o2ring_import.h"
#include "SleepLib/loader_plugins/viatom_loader.h"
#include "database/database_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace {
const QString kProfileName = QStringLiteral("O2RingImportUnitTest");
const QString kSerial = QStringLiteral("2682C06554");
}

void O2RingImportTests::initTestCase()
{
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    if (QCoreApplication::instance() == nullptr) m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) DatabaseManager::instance().close();
    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-o2ringimporttests-XXXXXX"));
    QVERIFY(m_tempDir->isValid());
    m_previousAppData = GetAppData();
    SetAppData(m_tempDir->path());

    p_profile = nullptr;
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));

    if (CPAP_Obstructive == 0) { schema::init(); }
    ViatomLoader::Register();
    Profiles::Scan();
    const QString profileDir = m_tempDir->path() + QStringLiteral("/Profiles/") + kProfileName;
    p_profile = Profiles::Get(kProfileName);
    if (p_profile == nullptr) p_profile = Profiles::Create(kProfileName, &profileDir);
    QVERIFY(p_profile != nullptr);
}

void O2RingImportTests::cleanupTestCase()
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

// A downloaded record is kept as is in the profile's backup folder and becomes a Viatom session.
void O2RingImportTests::testImportSavesAndParses()
{
    const QString name = QStringLiteral("20261012230005");
    O2RingImporter importer(kSerial);
    QVERIFY(importer.folder().endsWith(QStringLiteral("/Backup/O2Ring_") + kSerial));
    QVERIFY(!importer.alreadyImported(name));

    QString error;
    const QByteArray data = O2RingTestData::makeFile(QDateTime(QDate(2026, 10, 12), QTime(23, 0, 5)), 450);
    QCOMPARE(importer.importFile(name, data, &error), O2RingImporter::Result::Imported);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QFile saved(importer.folder() + QStringLiteral("/") + name);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), data);
    QVERIFY(!QFile::exists(saved.fileName() + QStringLiteral(".part")));

    Machine *mach = p_profile->lookupMachine(QStringLiteral("O2Ring_") + kSerial, viatom_class_name);
    QVERIFY(mach != nullptr);
    QCOMPARE(O2RingImporter::sessionIdFor(name),
             SessionID(QDateTime(QDate(2026, 10, 12), QTime(23, 0, 5)).toSecsSinceEpoch()));
    QVERIFY(mach->SessionExists(O2RingImporter::sessionIdFor(name)) != nullptr);
}

// The same record is then known as already in OSCAR and is not imported twice.
void O2RingImportTests::testAlreadyImported()
{
    const QString name = QStringLiteral("20261012230005");
    O2RingImporter importer(kSerial);
    QVERIFY(importer.alreadyImported(name));
    QVERIFY(!importer.alreadyImported(QStringLiteral("20261013220000")));
    QVERIFY(!O2RingImporter(QStringLiteral("1111A00000")).alreadyImported(name));

    QString error;
    const QByteArray data = O2RingTestData::makeFile(QDateTime(QDate(2026, 10, 12), QTime(23, 0, 5)), 450);
    QCOMPARE(importer.importFile(name, data, &error), O2RingImporter::Result::AlreadyThere);
}

// A file the loader cannot read is refused before anything is written.
void O2RingImportTests::testInvalidFileNotSaved()
{
    const QString name = QStringLiteral("20261014230000");
    O2RingImporter importer(QStringLiteral("9999B01234"));
    QByteArray data = O2RingTestData::makeFile(QDateTime(QDate(2026, 10, 14), QTime(23, 0, 0)), 100);
    data[0] = char(0x07);

    QString error;
    QCOMPARE(importer.importFile(name, data, &error), O2RingImporter::Result::Failed);
    QVERIFY(!error.isEmpty());
    QVERIFY(QDir(importer.folder()).entryList(QDir::Files).isEmpty());
}

void O2RingImportTests::testSetClockSettingDefault()
{
    QVERIFY(p_profile->oxi->o2ringSetClock());
    p_profile->oxi->setO2RingSetClock(false);
    QVERIFY(!p_profile->oxi->o2ringSetClock());
    p_profile->oxi->setO2RingSetClock(true);
}
