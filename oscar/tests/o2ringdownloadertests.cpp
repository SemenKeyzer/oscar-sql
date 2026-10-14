/* O2Ring Downloader Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "o2ringdownloadertests.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include "SleepLib/loader_plugins/o2ring_downloader.h"
#include "tests/fakeo2ring.h"
#include "tests/o2ringtestdata.h"

namespace {
const QString N1 = QStringLiteral("20261012230005");
const QString N2 = QStringLiteral("20261013230010");

QByteArray file(const QString &name, int points = 900)
{
    return O2RingTestData::makeFile(QDateTime::fromString(name, QStringLiteral("yyyyMMddHHmmss")), points);
}

struct Rig {
    FakeO2Ring ring;
    O2RingDownloader down;
    Rig()
    {
        ring.files.insert(N1, file(N1));
        ring.files.insert(N2, file(N2, 450));
        down.setResponseTimeout(50);
    }
    // waits for the ring's details
    bool connected()
    {
        QSignalSpy info(&down, &O2RingDownloader::infoReceived);
        down.start(&ring);
        return info.wait(2000);
    }
};
} // namespace

void O2RingDownloaderTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {   // timers and waiting need an event loop
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QCoreApplication(argc, argv);
    }
}

void O2RingDownloaderTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

void O2RingDownloaderTests::testDownloadTwoFiles()
{
    Rig rig;
    QVERIFY(rig.connected());
    QSignalSpy got(&rig.down, &O2RingDownloader::fileDownloaded);
    QSignalSpy done(&rig.down, &O2RingDownloader::finished);
    rig.down.download({ N1, N2 });
    QVERIFY(done.wait(5000));
    QCOMPARE(got.size(), 2);
    QCOMPARE(got[0][0].toString(), N1);
    QCOMPARE(got[0][1].toByteArray(), rig.ring.files.value(N1));
    QCOMPARE(got[1][1].toByteArray(), rig.ring.files.value(N2));
    int ends = 0;
    for (const auto &r : rig.ring.requests) ends += r.cmd == O2Ring::CmdEnd ? 1 : 0;
    QCOMPARE(ends, 2);
}

void O2RingDownloaderTests::testRetryAfterLostAnswer()
{
    Rig rig;
    rig.ring.lose = { { O2Ring::CmdData, 2 } };
    QVERIFY(rig.connected());
    QSignalSpy got(&rig.down, &O2RingDownloader::fileDownloaded);
    QSignalSpy done(&rig.down, &O2RingDownloader::finished);
    rig.down.download({ N1 });
    QVERIFY(done.wait(5000));
    QCOMPARE(got.size(), 1);
    QCOMPARE(got[0][1].toByteArray(), rig.ring.files.value(N1));
}

// The first answer to a piece arrives after the retry: it must not be taken for the next piece.
void O2RingDownloaderTests::testLateDuplicate()
{
    Rig rig;
    rig.ring.lateFile = N1;
    rig.ring.lateSeq = 1;
    rig.ring.lateMs = 80;   // after the 50 ms timeout, while the next pieces are asked for
    QVERIFY(rig.connected());
    QSignalSpy got(&rig.down, &O2RingDownloader::fileDownloaded);
    QSignalSpy failed(&rig.down, &O2RingDownloader::fileFailed);
    QSignalSpy done(&rig.down, &O2RingDownloader::finished);
    rig.down.download({ N1 });
    QVERIFY(done.wait(5000));
    QCOMPARE(got.size() + failed.size(), 1);
    if (!got.isEmpty()) QCOMPARE(got[0][1].toByteArray(), rig.ring.files.value(N1));   // never a wrong file
}

void O2RingDownloaderTests::testLinkLostMidFile()
{
    Rig rig;
    rig.ring.cutOn = N2;
    QVERIFY(rig.connected());
    QSignalSpy got(&rig.down, &O2RingDownloader::fileDownloaded);
    QSignalSpy failed(&rig.down, &O2RingDownloader::fileFailed);
    QSignalSpy lost(&rig.down, &O2RingDownloader::failed);
    rig.down.download({ N1, N2 });
    QVERIFY(lost.wait(5000) || !failed.isEmpty());
    QCOMPARE(got.size(), 1);   // the first file already arrived
    QCOMPARE(got[0][0].toString(), N1);
}

void O2RingDownloaderTests::testWrongSize()
{
    Rig rig;
    rig.ring.startSize = rig.ring.files.value(N1).size() + 100;   // more than the ring then sends
    QVERIFY(rig.connected());
    QSignalSpy got(&rig.down, &O2RingDownloader::fileDownloaded);
    QSignalSpy failed(&rig.down, &O2RingDownloader::fileFailed);
    QSignalSpy done(&rig.down, &O2RingDownloader::finished);
    rig.down.download({ N1 });
    QVERIFY(done.wait(5000));
    QCOMPARE(got.size(), 0);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(rig.ring.requests.last().cmd, quint8(O2Ring::CmdEnd));   // END also after an error
}

void O2RingDownloaderTests::testSetClock()
{
    Rig rig;
    QVERIFY(rig.connected());
    QSignalSpy set(&rig.down, &O2RingDownloader::clockSet);
    rig.down.setClock(QDateTime(QDate(2026, 10, 15), QTime(22, 5, 9)));
    QVERIFY(set.wait(2000));
    QCOMPARE(set[0][0].toBool(), true);
    QCOMPARE(rig.ring.clock, QStringLiteral("2026-10-15,22:05:09"));
}

void O2RingDownloaderTests::testInfoTimeout()
{
    Rig rig;
    rig.ring.lose = { { O2Ring::CmdInfo, 1 }, { O2Ring::CmdInfo, 2 }, { O2Ring::CmdInfo, 3 }, { O2Ring::CmdInfo, 4 } };
    QSignalSpy failed(&rig.down, &O2RingDownloader::failed);
    rig.down.start(&rig.ring);
    QVERIFY(failed.wait(3000));
}

// A ring that answers every piece with seq 0 (whether the real one echoes it is not known yet).
void O2RingDownloaderTests::testRingWithoutSeqEcho()
{
    Rig rig;
    rig.ring.echoSeq = false;
    QVERIFY(rig.connected());
    QSignalSpy got(&rig.down, &O2RingDownloader::fileDownloaded);
    QSignalSpy done(&rig.down, &O2RingDownloader::finished);
    rig.down.download({ N1 });
    QVERIFY(done.wait(5000));
    QCOMPARE(got.size(), 1);
    QCOMPARE(got[0][1].toByteArray(), rig.ring.files.value(N1));
}
