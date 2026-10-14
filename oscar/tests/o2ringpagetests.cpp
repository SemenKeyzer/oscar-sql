/* O2Ring wizard page Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "o2ringpagetests.h"
#include "fakeo2ring.h"
#include "o2ringtestdata.h"
#include "o2ringpage.h"
#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/loader_plugins/o2ring_import.h"
#include "SleepLib/loader_plugins/viatom_loader.h"
#include "database/database_manager.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

namespace {
const QString kProfileName = QStringLiteral("O2RingPageUnitTest");

//! A record that starts at \a start, keyed by its ring file name.
QPair<QString, QByteArray> record(const QDateTime &start)
{
    return { start.toString(QStringLiteral("yyyyMMddHHmmss")), O2RingTestData::makeFile(start, 300) };
}

QTableWidget *table(O2RingPage &page) { return page.findChild<QTableWidget *>(QStringLiteral("o2ringRecords")); }
QPushButton *downloadButton(O2RingPage &page) { return page.findChild<QPushButton *>(QStringLiteral("downloadButton")); }
QCheckBox *setClock(O2RingPage &page) { return page.findChild<QCheckBox *>(QStringLiteral("setClock")); }
QString summary(O2RingPage &page) { return page.findChild<QLabel *>(QStringLiteral("o2ringSummary"))->text(); }

int countOf(const FakeO2Ring &ring, quint8 cmd)
{
    int n = 0;
    for (const auto &r : ring.requests) n += r.cmd == cmd ? 1 : 0;
    return n;
}
} // namespace

void O2RingPageTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
    if (DatabaseManager::instance().isOpen()) DatabaseManager::instance().close();
    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-o2ringpagetests-XXXXXX"));
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

void O2RingPageTests::cleanupTestCase()
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

// The ring's records are listed; the one already in OSCAR is marked and left unticked.
void O2RingPageTests::testRecordsListed()
{
    FakeO2Ring ring;
    const auto old = record(QDateTime(QDate(2026, 10, 10), QTime(23, 0, 0)));
    const auto fresh = record(QDateTime(QDate(2026, 10, 11), QTime(23, 0, 0)));
    ring.files.insert(old.first, old.second);
    ring.files.insert(fresh.first, fresh.second);
    QString error;
    QCOMPARE(O2RingImporter(ring.serial).importFile(old.first, old.second, &error), O2RingImporter::Result::Imported);

    O2RingPage page;
    page.startWithLink(&ring, QStringLiteral("O2Ring 6554"));
    QTRY_COMPARE_WITH_TIMEOUT(table(page)->rowCount(), 2, 3000);
    QCOMPARE(table(page)->item(0, 0)->checkState(), Qt::Unchecked);
    QVERIFY(table(page)->item(0, 1)->text().contains(QStringLiteral("already in OSCAR")));
    QCOMPARE(table(page)->item(1, 0)->checkState(), Qt::Checked);
    QVERIFY(table(page)->item(1, 1)->text().contains(QStringLiteral("new")));
    QVERIFY(downloadButton(page)->isEnabled());
    page.cancel();
}

// Download fetches only the ticked records and imports them.
void O2RingPageTests::testDownloadImportsChecked()
{
    FakeO2Ring ring;
    const auto old = record(QDateTime(QDate(2026, 10, 10), QTime(23, 0, 0)));     // imported above
    const auto fresh = record(QDateTime(QDate(2026, 10, 12), QTime(22, 30, 0)));
    ring.files.insert(old.first, old.second);
    ring.files.insert(fresh.first, fresh.second);

    O2RingPage page;
    QSignalSpy ended(&page, &O2RingPage::ended);
    page.startWithLink(&ring, QStringLiteral("O2Ring 6554"));
    QTRY_COMPARE_WITH_TIMEOUT(table(page)->rowCount(), 2, 3000);
    downloadButton(page)->click();
    QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 5000);
    QCOMPARE(ended[0][0].toBool(), false);
    QCOMPARE(countOf(ring, O2Ring::CmdStart), 1);
    QVERIFY(O2RingImporter(ring.serial).alreadyImported(fresh.first));
    QVERIFY2(summary(page).contains(QStringLiteral("1 imported")), qPrintable(summary(page)));
    QVERIFY(page.importedAny());
    QVERIFY(!page.isBusy());
}

// The clock checkbox follows and writes the setting; on, the ring's clock is set before the download.
void O2RingPageTests::testSetClockFollowsSetting()
{
    p_profile->oxi->setO2RingSetClock(true);
    {
        FakeO2Ring ring;
        const auto r = record(QDateTime(QDate(2026, 10, 13), QTime(23, 0, 0)));
        ring.files.insert(r.first, r.second);
        O2RingPage page;
        QSignalSpy ended(&page, &O2RingPage::ended);
        page.startWithLink(&ring, QStringLiteral("O2Ring 6554"));
        QVERIFY(setClock(page)->isChecked());
        QTRY_COMPARE_WITH_TIMEOUT(table(page)->rowCount(), 1, 3000);
        downloadButton(page)->click();
        QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 5000);
        QCOMPARE(countOf(ring, O2Ring::CmdSync), 1);
        int sync = -1, start = -1;
        for (int i = 0; i < ring.requests.size(); ++i) {
            if (ring.requests[i].cmd == O2Ring::CmdSync && sync < 0) sync = i;
            if (ring.requests[i].cmd == O2Ring::CmdStart && start < 0) start = i;
        }
        QVERIFY(sync >= 0 && start > sync);
        setClock(page)->setChecked(false);
        QVERIFY(!p_profile->oxi->o2ringSetClock());
    }
    {
        FakeO2Ring ring;
        const auto r = record(QDateTime(QDate(2026, 10, 14), QTime(23, 0, 0)));
        ring.files.insert(r.first, r.second);
        O2RingPage page;
        QSignalSpy ended(&page, &O2RingPage::ended);
        page.startWithLink(&ring, QStringLiteral("O2Ring 6554"));
        QVERIFY(!setClock(page)->isChecked());
        QTRY_COMPARE_WITH_TIMEOUT(table(page)->rowCount(), 1, 3000);
        downloadButton(page)->click();
        QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 5000);
        QCOMPARE(countOf(ring, O2Ring::CmdSync), 0);
        QCOMPARE(countOf(ring, O2Ring::CmdStart), 1);
    }
    p_profile->oxi->setO2RingSetClock(true);
}

// Cancel in the middle of a record: the ring is told the read is over, nothing half-written stays.
void O2RingPageTests::testCancelRemovesPart()
{
    FakeO2Ring ring;
    const auto r = record(QDateTime(QDate(2026, 10, 15), QTime(23, 0, 0)));
    ring.files.insert(r.first, r.second);
    ring.lateFile = r.first;
    ring.lateSeq = 0;
    ring.lateMs = 3000;

    O2RingPage page;
    QSignalSpy ended(&page, &O2RingPage::ended);
    page.startWithLink(&ring, QStringLiteral("O2Ring 6554"));
    QTRY_COMPARE_WITH_TIMEOUT(table(page)->rowCount(), 1, 3000);
    downloadButton(page)->click();
    QTRY_VERIFY_WITH_TIMEOUT(countOf(ring, O2Ring::CmdData) > 0, 3000);
    page.cancel();
    QCOMPARE(ended.size(), 1);
    QCOMPARE(ended[0][0].toBool(), true);
    QVERIFY(!page.isBusy());
    QCOMPARE(ring.requests.last().cmd, quint8(O2Ring::CmdEnd));
    const int sent = ring.requests.size();
    QTest::qWait(200);
    QCOMPARE(ring.requests.size(), sent);

    O2RingImporter importer(ring.serial);
    QVERIFY(!importer.alreadyImported(r.first));
    const QStringList left = QDir(importer.folder()).entryList({ r.first + QStringLiteral("*") }, QDir::Files);
    QVERIFY2(left.isEmpty(), qPrintable(left.join(QStringLiteral(", "))));
}

// The ring goes away in the middle of a record: the page stops and offers Retry.
void O2RingPageTests::testLinkLostOffersRetry()
{
    FakeO2Ring ring;
    const auto r = record(QDateTime(QDate(2026, 10, 16), QTime(23, 0, 0)));
    ring.files.insert(r.first, r.second);
    ring.cutOn = r.first;

    O2RingPage page;
    QSignalSpy ended(&page, &O2RingPage::ended);
    page.startWithLink(&ring, QStringLiteral("O2Ring 6554"));
    QTRY_COMPARE_WITH_TIMEOUT(table(page)->rowCount(), 1, 3000);
    downloadButton(page)->click();
    QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 5000);
    QCOMPARE(ended[0][0].toBool(), true);
    QVERIFY(!page.importedAny());
    QVERIFY2(summary(page).contains(QStringLiteral("lost")), qPrintable(summary(page)));
}

// Cancel pressed while a downloaded record is being imported (the loader lets events through):
// nothing crashes, the record is still imported and the summary counts it.
void O2RingPageTests::testCancelDuringImport()
{
    FakeO2Ring ring;
    const auto r = record(QDateTime(QDate(2026, 10, 17), QTime(23, 0, 0)));
    ring.files.insert(r.first, r.second);
    O2RingPage page;
    QSignalSpy ended(&page, &O2RingPage::ended);
    const int pieces = (r.second.size() + ring.chunk - 1) / ring.chunk;
    ring.afterRequest = [&](const FakeO2Ring::Request &req) {
        // queued behind the last piece's answer: runs inside the import of the record
        if (req.cmd == O2Ring::CmdData && int(req.seq) == pieces - 1) QTimer::singleShot(0, &page, [&page]() { page.cancel(); });
    };
    page.startWithLink(&ring, QStringLiteral("O2Ring 6554"));
    QTRY_COMPARE_WITH_TIMEOUT(table(page)->rowCount(), 1, 3000);
    downloadButton(page)->click();
    QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 5000);
    QTest::qWait(100);
    QVERIFY(O2RingImporter(ring.serial).alreadyImported(r.first));
    QVERIFY(page.importedAny());
    QVERIFY2(summary(page).contains(QStringLiteral("1 imported")), qPrintable(summary(page)));
}

// The ring does not confirm its new clock: the page says so, and still downloads.
void O2RingPageTests::testClockFailureShown()
{
    p_profile->oxi->setO2RingSetClock(true);
    FakeO2Ring ring;
    const auto r = record(QDateTime(QDate(2026, 10, 18), QTime(23, 0, 0)));
    ring.files.insert(r.first, r.second);
    ring.lose = { { O2Ring::CmdSync, 1 }, { O2Ring::CmdSync, 2 }, { O2Ring::CmdSync, 3 }, { O2Ring::CmdSync, 4 } };
    O2RingPage page;
    page.setResponseTimeout(50);
    QSignalSpy ended(&page, &O2RingPage::ended);
    page.startWithLink(&ring, QStringLiteral("O2Ring 6554"));
    QTRY_COMPARE_WITH_TIMEOUT(table(page)->rowCount(), 1, 3000);
    downloadButton(page)->click();
    QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 5000);
    QVERIFY(O2RingImporter(ring.serial).alreadyImported(r.first));
    QVERIFY2(summary(page).contains(QStringLiteral("clock could not be set")), qPrintable(summary(page)));
}

// A serial number that cannot name the ring's folder is refused before anything is written.
void O2RingPageTests::testOddSerialRefused()
{
    QVERIFY(O2RingImporter::validSerial(QStringLiteral("2682C06554")));
    QVERIFY(!O2RingImporter::validSerial(QString()));
    QVERIFY(!O2RingImporter::validSerial(QStringLiteral("../../x1234")));
    QVERIFY(!O2RingImporter::validSerial(QStringLiteral("ABCDEFGH")));   // the Viatom loader needs 4 digits at the end

    FakeO2Ring ring;
    ring.serial = QStringLiteral("../../x1234");
    const auto r = record(QDateTime(QDate(2026, 10, 19), QTime(23, 0, 0)));
    ring.files.insert(r.first, r.second);
    O2RingPage page;
    QSignalSpy ended(&page, &O2RingPage::ended);
    page.startWithLink(&ring, QStringLiteral("O2Ring 6554"));
    QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 3000);
    QCOMPARE(ended[0][0].toBool(), true);
    QCOMPARE(table(page)->rowCount(), 0);
    QVERIFY2(summary(page).contains(QStringLiteral("serial number")), qPrintable(summary(page)));
}
