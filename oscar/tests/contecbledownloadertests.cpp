/* Contec BLE Download Session Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contecbledownloadertests.h"
#include "fakecontecdevice.h"
#include "SleepLib/loader_plugins/contec_ble_downloader.h"

#include <QCoreApplication>

using namespace ContecBle;

namespace {

FakeContecDevice::Rec makeRec(const QDateTime &start, int n, int spo2Base, int pulseBase)
{
    FakeContecDevice::Rec r;
    r.start = start;
    for (int i = 0; i < n; ++i) {
        r.spo2.append(spo2Base + i % 4);
        r.pulse.append(pulseBase + i % 7);
    }
    return r;
}

// Two nights' worth of segments; pulse values cross 127 so the high bits are exercised, and
// one sample of each channel carries the device's "no data" marker.
void addTwoRecords(FakeContecDevice &dev)
{
    FakeContecDevice::Rec a = makeRec(QDateTime(QDate(2026, 9, 26), QTime(23, 59, 13)), 50, 93, 125);
    a.spo2[3] = 127;
    a.pulse[3] = 255;
    dev.records = { a, makeRec(QDateTime(QDate(2026, 9, 27), QTime(5, 41, 54)), 30, 95, 60) };
}

struct Collected {
    QList<Record> records;
    bool finished = false;
    QString error;
    int count = -1;
    QString model, firmware;
    int version = -1;
    int clock = -1;
    int erase = -1;
};

void collect(ContecBleDownloader &d, Collected &c)
{
    QObject::connect(&d, &ContecBleDownloader::recordDownloaded, [&c](const Record &r) { c.records.append(r); });
    QObject::connect(&d, &ContecBleDownloader::downloadFinished, [&c]() { c.finished = true; });
    QObject::connect(&d, &ContecBleDownloader::failed, [&c](const QString &e) { c.error = e; });
    QObject::connect(&d, &ContecBleDownloader::recordCountKnown, [&c](int n) { c.count = n; });
    QObject::connect(&d, &ContecBleDownloader::deviceIdentified, [&c](const QString &m, const QString &f, int v) {
        c.model = m; c.firmware = f; c.version = v;
    });
    QObject::connect(&d, &ContecBleDownloader::clockSet, [&c](bool ok) { c.clock = ok ? 1 : 0; });
    QObject::connect(&d, &ContecBleDownloader::eraseFinished, [&c](bool ok) { c.erase = ok ? 1 : 0; });
}

bool hasCommand(const FakeContecDevice &dev, const QByteArray &prefix)
{
    for (const QByteArray &c : dev.commands) {
        if (c.startsWith(prefix)) return true;
    }
    return false;
}

} // namespace

void ContecBleDownloaderTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QCoreApplication(argc, argv);
    }
}

void ContecBleDownloaderTests::testDownloadsAllRecords()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished || !c.error.isEmpty(), 5000);
    QVERIFY2(c.error.isEmpty(), qPrintable(c.error));
    QCOMPARE(c.model, QStringLiteral("CMS50FW"));
    QCOMPARE(c.firmware, QStringLiteral("2.0.0"));
    QCOMPARE(c.version, 13);
    QCOMPARE(c.count, 2);
    QCOMPARE(c.records.size(), 2);
    QCOMPARE(c.records[0].header.start(), dev.records[0].start);
    QCOMPARE(c.records[0].spo2, dev.records[0].spo2);
    QCOMPARE(c.records[0].pulse, dev.records[0].pulse);
    QCOMPARE(c.records[1].header.start(), dev.records[1].start);
    QCOMPARE(c.records[1].pulse, dev.records[1].pulse);
    QVERIFY(!d.isEncrypted());
    for (const QByteArray &cmd : dev.commands) QVERIFY(!isDestructive(cmd));
}

void ContecBleDownloaderTests::testSkipsUnwantedRecord()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    ContecBleDownloader d;
    d.setWantRecord([](const RecordHeader &h) { return h.m != 1; });
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished || !c.error.isEmpty(), 5000);
    QVERIFY(c.error.isEmpty());
    QCOMPARE(c.records.size(), 1);
    QCOMPARE(c.records[0].header.m, 2);
    QVERIFY(!hasCommand(dev, frame({0x9D, 0x03, 0x01, 0x01, 0x01, 0, 0, 0}).left(5)));
}

void ContecBleDownloaderTests::testRetriesCorruptedPacket()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    dev.corruptChannel = ChSpO2;
    dev.corruptPacket = 1;
    ContecBleDownloader d;
    d.setRetryPause(20);
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished || !c.error.isEmpty(), 5000);
    QVERIFY2(c.error.isEmpty(), qPrintable(c.error));
    QCOMPARE(c.records[0].spo2, dev.records[0].spo2);
    QVERIFY(hasCommand(dev, cmdChannelAbort(ChSpO2, 1, 1)));
    QVERIFY(hasCommand(dev, cmdChannel(FmtOriginal, ChSpO2, 1, 1, 1)));
}

void ContecBleDownloaderTests::testTimeoutFails()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    dev.silentCommands.insert(0x9C);
    ContecBleDownloader d;
    d.setResponseTimeout(100);
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(!c.error.isEmpty(), 3000);
    QVERIFY(!c.finished);
}

void ContecBleDownloaderTests::testRejectedCommandFails()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    dev.rejectCommands.insert(0x90);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(!c.error.isEmpty(), 3000);
    QVERIFY(c.error.contains(QStringLiteral("rejected")));
}

void ContecBleDownloaderTests::testUnsupportedModels()
{
    FakeContecDevice dev;
    ContecBleDownloader k;
    Collected ck;
    collect(k, ck);
    k.start(&dev, QStringLiteral("SpO209"));
    QVERIFY(!ck.error.isEmpty());
    ContecBleDownloader unknown;
    Collected cu;
    collect(unknown, cu);
    unknown.start(&dev, QStringLiteral("Headphones"));
    QVERIFY(!cu.error.isEmpty());
    QVERIFY(dev.commands.isEmpty());
}

void ContecBleDownloaderTests::testEraseNeedsPermission()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished, 5000);
    d.eraseAllRecords();
    QVERIFY(!c.error.isEmpty());
    QTest::qWait(50);
    QVERIFY(!dev.erased);
    QVERIFY(!hasCommand(dev, cmdEraseAllRecords()));
}

void ContecBleDownloaderTests::testEraseWithPermission()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished, 5000);
    d.allowDestructive(true);
    d.eraseAllRecords();
    QTRY_COMPARE_WITH_TIMEOUT(c.erase, 1, 3000);
    QVERIFY(dev.erased);
    QVERIFY(c.error.isEmpty());
}

void ContecBleDownloaderTests::testSetClock()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished, 5000);
    const QDateTime now(QDate(2026, 9, 28), QTime(7, 30, 15));
    d.setClock(now);
    QTRY_COMPARE_WITH_TIMEOUT(c.clock, 1, 3000);
    QCOMPARE(dev.clockSetTo, now);
}

void ContecBleDownloaderTests::testEncryptedSession()
{
    FakeContecDevice dev(14);
    addTwoRecords(dev);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO211"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished || !c.error.isEmpty(), 5000);
    QVERIFY2(c.error.isEmpty(), qPrintable(c.error));
    QVERIFY(d.isEncrypted());
    QVERIFY(dev.sawEncryptedCommand);
    QCOMPARE(c.version, 14);
    QCOMPARE(c.records.size(), 2);
    QCOMPARE(c.records[0].spo2, dev.records[0].spo2);
    QCOMPARE(c.records[1].pulse, dev.records[1].pulse);
}

void ContecBleDownloaderTests::testStorageFallsBackToPrepare()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    dev.silentOnceCommand = 0x9F;
    ContecBleDownloader d;
    d.setResponseTimeout(100);
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished || !c.error.isEmpty(), 5000);
    QVERIFY2(c.error.isEmpty(), qPrintable(c.error));
    QVERIFY(hasCommand(dev, cmdPrepare()));
    QCOMPARE(c.records.size(), 2);
}

void ContecBleDownloaderTests::testNoRecords()
{
    FakeContecDevice dev;
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished || !c.error.isEmpty(), 5000);
    QVERIFY(c.error.isEmpty());
    QCOMPARE(c.count, 0);
    QVERIFY(c.records.isEmpty());
    QVERIFY(!hasCommand(dev, cmdCountRecords()));
}

void ContecBleDownloaderTests::testLinkLostFails()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    dev.silentCommands.insert(0x9C);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_COMPARE_WITH_TIMEOUT(c.count, 2, 3000);
    dev.dropLink();
    QVERIFY(!c.error.isEmpty());
    QVERIFY(!c.finished);
}

// A growing record can leave packets in flight; one may be handled while the record is being
// stored (if anything pumps events there). It must be ignored, not read past the channel list.
void ContecBleDownloaderTests::testExtraPacketWhileDeliveringIsIgnored()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    dev.extraPackets = 1;
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    QObject::connect(&d, &ContecBleDownloader::recordDownloaded, [](const Record &) {
        QCoreApplication::processEvents();
    });
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished || !c.error.isEmpty(), 5000);
    QVERIFY2(c.error.isEmpty(), qPrintable(c.error));
    QCOMPARE(c.records.size(), 2);
    QCOMPARE(c.records[1].pulse, dev.records[1].pulse);
}

void ContecBleDownloaderTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}
