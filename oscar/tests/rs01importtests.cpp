/* Contec RS01 Import Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "rs01importtests.h"
#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "SleepLib/loader_plugins/rs01_import.h"
#include "database/database_manager.h"
#include "tests/rs01_synth.h"
#include <QApplication>
#include <QCheckBox>
#include <QTreeWidget>
#include "rs01page.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QTemporaryDir>

using namespace rs01synth;

namespace {

const QString kProfileName = QStringLiteral("RS01Import");

// A recording of \a seconds steady seconds in a fresh folder under \a root.
QString steadyRecording(const QString &root, const QString &name, const QString &start, int seconds)
{
    return recording(root, name, header(start), { { 1, segment(1, seconds, [](int i) { return record(i, i % 4 ? 2008 : 1990, 62, 95); }) } });
}

QMap<QString, QDateTime> snapshot(const QString &dir)
{
    QMap<QString, QDateTime> out;
    QDirIterator it(dir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        out.insert(f, QFileInfo(f).lastModified());
    }
    return out;
}

} // namespace

void RS01ImportTests::initTestCase()
{
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    if (QCoreApplication::instance() == nullptr) m_app = new QApplication(argc, argv);   // the page is a widget

    if (DatabaseManager::instance().isOpen()) DatabaseManager::instance().close();
    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-rs01importtests-XXXXXX"));
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
    p_profile = Profiles::Get(kProfileName);
    if (p_profile == nullptr) p_profile = Profiles::Create(kProfileName, &profileDir);
    QVERIFY(p_profile != nullptr);
}

void RS01ImportTests::cleanupTestCase()
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

// The nasal flow is a waveform of the oximeter in Pa, not a CPAP's L/min.
void RS01ImportTests::testNasalFlowChannel()
{
    schema::Channel &ch = schema::channel[OXI_NasalFlow];
    QCOMPARE(OXI_NasalFlow, ChannelID(0x1806));
    QCOMPARE(ch.machtype(), MT_OXIMETER);
    QCOMPARE(ch.type(), schema::WAVEFORM);
    QCOMPARE(ch.code(), QStringLiteral("NasalFlow"));
    QCOMPARE(ch.units(), QObject::tr("Pa"));
}

// A recording becomes one oximeter session with SpO2, pulse, pleth and the nasal flow.
void RS01ImportTests::testImportBuildsSession()
{
    QTemporaryDir device;
    const QString dir = steadyRecording(device.path(), QStringLiteral("10170150"), QStringLiteral("20261017015053"), 120);
    RS01Importer importer;
    QString error;
    QCOMPARE(int(importer.import(dir, &error)), int(RS01Importer::Result::Imported));
    Machine *mach = p_profile->lookupMachine(QStringLiteral("RS01"), RS01Importer::loaderName());
    QVERIFY(mach);
    QCOMPARE(mach->type(), MT_OXIMETER);
    QCOMPARE(mach->brand(), QStringLiteral("Contec"));
    Session *s = mach->SessionExists(RS01Importer::sessionIdFor(QDateTime(QDate(2026, 10, 17), QTime(1, 50, 53))));
    QVERIFY(s);
    QCOMPARE(s->first(), QDateTime(QDate(2026, 10, 17), QTime(1, 50, 53)).toMSecsSinceEpoch());
    for (ChannelID c : { OXI_SPO2, OXI_Pulse, OXI_Plethy, OXI_NasalFlow }) QVERIFY2(s->channelExists(c), qPrintable(QString::number(c, 16)));
    QCOMPARE(s->Min(OXI_SPO2), EventDataType(95));
    // the waveforms in their own units: 18 counts below the zero = 4.49 Pa breathing in; pleth 60 of 127 = 47.2 %
    QVERIFY2(qAbs(s->Max(OXI_NasalFlow) - 18.0f * 1022.0f / 4095.0f) < 0.1f, qPrintable(QString::number(s->Max(OXI_NasalFlow))));
    QVERIFY2(qAbs(s->Max(OXI_Plethy) - 60.0f * 100.0f / 127.0f) < 0.1f, qPrintable(QString::number(s->Max(OXI_Plethy))));
}

// The device names its folders MMDDhhmm, without the year: a later recording with the same
// name is a new copy with its own data, never last year's.
void RS01ImportTests::testSameFolderNameNextYear()
{
    QTemporaryDir lastYear, thisYear;
    const QString a = recording(lastYear.path(), QStringLiteral("10230150"), header(QStringLiteral("20251023015053")),
                                { { 1, segment(1, 60, [](int i) { return record(i, 2008, 62, 97); }) } });
    const QString b = recording(thisYear.path(), QStringLiteral("10230150"), header(QStringLiteral("20261023015053")),
                                { { 1, segment(1, 60, [](int i) { return record(i, 2008, 62, 91); }) } });
    RS01Importer importer;
    QCOMPARE(int(importer.import(a, nullptr)), int(RS01Importer::Result::Imported));
    QCOMPARE(int(importer.import(b, nullptr)), int(RS01Importer::Result::Imported));
    Machine *mach = p_profile->lookupMachine(QStringLiteral("RS01"), RS01Importer::loaderName());
    Session *s = mach->SessionExists(RS01Importer::sessionIdFor(QDateTime(QDate(2026, 10, 23), QTime(1, 50, 53))));
    QVERIFY(s);
    QCOMPARE(s->Min(OXI_SPO2), EventDataType(91));
}

void RS01ImportTests::testImportTwiceAlreadyThere()
{
    QTemporaryDir device;
    const QString dir = steadyRecording(device.path(), QStringLiteral("10180150"), QStringLiteral("20261018015053"), 60);
    RS01Importer importer;
    QCOMPARE(int(importer.import(dir, nullptr)), int(RS01Importer::Result::Imported));
    QVERIFY(importer.alreadyImported(dir));
    QCOMPARE(int(importer.import(dir, nullptr)), int(RS01Importer::Result::AlreadyThere));
}

// The device is only read: its files stay as they were; the copy goes to the profile's backup.
void RS01ImportTests::testCopyLeavesSourceUntouched()
{
    QTemporaryDir device;
    const QString dir = steadyRecording(device.path(), QStringLiteral("10190150"), QStringLiteral("20261019015053"), 60);
    const QMap<QString, QDateTime> before = snapshot(device.path());
    RS01Importer importer;
    QCOMPARE(int(importer.import(dir, nullptr)), int(RS01Importer::Result::Imported));
    QCOMPARE(snapshot(device.path()), before);
    const QString copy = QDir(importer.backupFolder()).filePath(QStringLiteral("20261019015053/10190150"));
    QVERIFY(QFileInfo::exists(QDir(copy).filePath(QStringLiteral("10190150.HD"))));
    QVERIFY(QFileInfo::exists(QDir(copy).filePath(QStringLiteral("001.SEG"))));
    QVERIFY(importer.backupFolder().startsWith(p_profile->dataFolder()));
}

void RS01ImportTests::testHdWithoutSegmentsFails()
{
    QTemporaryDir device;
    const QString dir = recording(device.path(), QStringLiteral("10200150"), header(QStringLiteral("20261020015053")), {});
    RS01Importer importer;
    QString error;
    QCOMPARE(int(importer.import(dir, &error)), int(RS01Importer::Result::Failed));
    QVERIFY(!error.isEmpty());
}

// The same start is the same session, whatever the run: re-importing never duplicates it.
void RS01ImportTests::testSessionIdStable()
{
    const QDateTime start(QDate(2026, 3, 29), QTime(2, 30, 0));   // around a DST change
    QCOMPARE(RS01Importer::sessionIdFor(start), RS01Importer::sessionIdFor(start));
    QCOMPARE(RS01Importer::sessionIdFor(start), SessionID(start.toUTC().toSecsSinceEpoch()));
}

// A volume mounted writable on macOS: the page says how to remount it read-only.
void RS01ImportTests::testWritableWarningText()
{
    const QString w = RS01Page::writableWarning(QStringLiteral("/Volumes/SLEEP METER"), QStringLiteral("/dev/disk5s1"));
    QVERIFY2(w.contains(QStringLiteral("diskutil unmount")), qPrintable(w));
    QVERIFY(w.contains(QStringLiteral("diskutil mount readOnly /dev/disk5s1")));
}

// The page lists the recordings: new ones ticked, the one in OSCAR marked and not ticked.
void RS01ImportTests::testPageListsRecordings()
{
    QTemporaryDir device;
    const QString done = steadyRecording(device.path(), QStringLiteral("10210150"), QStringLiteral("20261021015053"), 60);
    steadyRecording(device.path(), QStringLiteral("10220150"), QStringLiteral("20261022015053"), 90);
    RS01Importer importer;
    QCOMPARE(int(importer.import(done, nullptr)), int(RS01Importer::Result::Imported));

    RS01Page page;
    page.listRecordings(device.path());
    QTreeWidget *list = page.findChild<QTreeWidget *>(QStringLiteral("rs01Recordings"));
    QVERIFY(list);
    QCOMPARE(list->topLevelItemCount(), 2);
    QCOMPARE(list->topLevelItem(0)->checkState(0), Qt::Unchecked);
    QVERIFY(list->topLevelItem(0)->text(1).contains(RS01Page::tr("already in OSCAR")));
    QCOMPARE(list->topLevelItem(1)->checkState(0), Qt::Checked);
    QVERIFY2(list->topLevelItem(1)->text(0).contains(QStringLiteral("1:30")), qPrintable(list->topLevelItem(1)->text(0)));   // 90 s
}
