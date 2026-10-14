/* O2Ring Protocol Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "o2ringprotocoltests.h"

#include <QtEndian>

#include "SleepLib/loader_plugins/o2ring_protocol.h"
#include "tests/o2ringtestdata.h"

using namespace O2Ring;
using O2RingTestData::makeFile;
using O2RingTestData::response;

void O2RingProtocolTests::testCrcVector()
{
    QCOMPARE(crc8(QByteArray::fromHex("aa14eb00000000")), quint8(0xC6));
    QCOMPARE(buildRequest(CmdInfo), QByteArray::fromHex("aa14eb00000000c6"));
}

void O2RingProtocolTests::testRequestWithPayloadAndSeq()
{
    const QByteArray r = buildRequest(CmdData, QByteArrayLiteral("abc"), 0x0102);
    QCOMPARE(r.mid(3, 2), QByteArray::fromHex("0201"));
    QCOMPARE(r.mid(5, 2), QByteArray::fromHex("0300"));
    QCOMPARE(r.mid(7, 3), QByteArrayLiteral("abc"));
    QCOMPARE(quint8(r.back()), crc8(r.left(r.size() - 1)));
}

void O2RingProtocolTests::testFileNamePayload()
{
    QCOMPARE(fileNamePayload(QStringLiteral("20261012230005")), QByteArray("20261012230005\0", 15));
}

void O2RingProtocolTests::testTimePayload()
{
    QCOMPARE(timePayload(QDateTime(QDate(2026, 10, 13), QTime(23, 40, 0))), QByteArrayLiteral("{\"SetTIME\":\"2026-10-13,23:40:00\"}"));
}

void O2RingProtocolTests::testReaderByteByByte()
{
    FrameReader reader;
    QList<Frame> out;
    for (char b : response(QByteArrayLiteral("{\"SN\":\"1\"}"))) out += reader.feed(QByteArray(1, b));
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.first().payload, QByteArrayLiteral("{\"SN\":\"1\"}"));
    QCOMPARE(out.first().status, quint8(0));
}

void O2RingProtocolTests::testReaderTwoFramesInOneChunk()
{
    const QList<Frame> out = FrameReader().feed(response("a") + response("bb", 0, 7));
    QCOMPARE(out.size(), 2);
    QCOMPARE(out[0].payload, QByteArray("a"));
    QCOMPARE(out[1].payload, QByteArray("bb"));
    QCOMPARE(out[1].seq, quint16(7));
}

void O2RingProtocolTests::testReaderSkipsJunk()
{
    const QList<Frame> out = FrameReader().feed(QByteArray::fromHex("0102") + response("x"));
    QCOMPARE(out.size(), 1);
    QCOMPARE(out[0].payload, QByteArray("x"));
}

void O2RingProtocolTests::testReaderDropsBadCrc()
{
    QByteArray bad = response("x");
    bad[bad.size() - 1] = char(bad.back() ^ 0xFF);
    FrameReader reader;
    QVERIFY(reader.feed(bad).isEmpty());
    const QList<Frame> out = reader.feed(response("y"));
    QCOMPARE(out.size(), 1);
    QCOMPARE(out[0].payload, QByteArray("y"));
}

void O2RingProtocolTests::testReaderIgnoresRequests()
{
    QVERIFY(FrameReader().feed(buildRequest(CmdInfo)).isEmpty());
}

void O2RingProtocolTests::testReaderLenCap()
{
    const QByteArray junk = QByteArray::fromHex("5500ff0000ffff");   // len 65535: a false start
    const QList<Frame> out = FrameReader().feed(junk + response("ok"));
    QCOMPARE(out.size(), 1);
    QCOMPARE(out[0].payload, QByteArray("ok"));
}

void O2RingProtocolTests::testReaderReset()
{
    FrameReader reader;
    reader.feed(response("abc").left(5));
    reader.reset();
    const QList<Frame> out = reader.feed(response("z"));
    QCOMPARE(out.size(), 1);
    QCOMPARE(out[0].payload, QByteArray("z"));
}

void O2RingProtocolTests::testParseInfo()
{
    Info info;
    const QByteArray json("{\"SN\":\"2682C06554\",\"Model\":\"1652\",\"SoftwareVer\":\"1.13.1\",\"CurTIME\":\"2026-10-13,07:01:02\","
                          "\"CurBAT\":\"65%\",\"FileList\":\"20261012230005,20261013230010,\"}");
    QVERIFY(parseInfo(json + QByteArray(2, '\0'), &info));   // trailing NULs, as the ring sends
    QCOMPARE(info.serial, QStringLiteral("2682C06554"));
    QCOMPARE(info.model, QStringLiteral("1652"));
    QCOMPARE(info.firmware, QStringLiteral("1.13.1"));
    QCOMPARE(info.time, QStringLiteral("2026-10-13,07:01:02"));
    QCOMPARE(info.battery, QStringLiteral("65%"));
    QCOMPARE(info.files, QStringList({ QStringLiteral("20261012230005"), QStringLiteral("20261013230010") }));
    QVERIFY(parseInfo(QByteArrayLiteral("{\"SN\":\"1234\"}"), &info));
    QVERIFY(info.files.isEmpty());
    QVERIFY(!parseInfo(QByteArrayLiteral("not json"), &info));
}

void O2RingProtocolTests::testValidateFile()
{
    QString error;
    QVERIFY2(validateFile(QStringLiteral("20261012230005"), makeFile(), &error), qPrintable(error));
    QVERIFY(!validateFile(QStringLiteral("20261012230005"), makeFile(QDateTime(QDate(2026, 10, 12), QTime(23, 0, 5)), 900, 9), &error));
    QVERIFY(!validateFile(QStringLiteral("20261012230005"), makeFile().chopped(5), &error));
    QVERIFY(!validateFile(QStringLiteral("20261012230006"), makeFile(), &error));
    QVERIFY(!validateFile(QStringLiteral("20261012230005"), QByteArray(20, '\0'), &error));
    // some 2 s firmware writes the size as if the records were 4 s apart
    QByteArray quirk = makeFile(QDateTime(QDate(2026, 10, 12), QTime(23, 0, 5)), 1800, 3, 2);
    qToLittleEndian<quint32>(quint32((quirk.size() - 40) / 2 + 40), quirk.data() + 9);
    QVERIFY2(validateFile(QStringLiteral("20261012230005"), quirk, &error), qPrintable(error));
}
