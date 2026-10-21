/* Contec RS01 Clock Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "rs01clocktests.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QThread>
#include "SleepLib/loader_plugins/rs01_clock.h"
#include "SleepLib/loader_plugins/rs01_hid.h"

namespace {

QByteArray bytes(std::initializer_list<int> list)
{
    QByteArray b;
    for (int v : list) b.append(char(v));
    return b;
}

QByteArray padded(const QByteArray &b)
{
    return b + QByteArray(64 - b.size(), '\0');
}

} // namespace

// The checksum the device uses (and checks): command and parameters, 7 bits.
void RS01ClockTests::testChecksum()
{
    QCOMPARE(rs01Checksum(bytes({ 0xF3, 0x00 })), quint8(0x73));   // its own reply to set clock
    QCOMPARE(rs01Checksum(bytes({ 0x83, 0x1A, 0x0A, 0x15, 0x16, 0x0F, 0x03, 0x00, 0x00 })), quint8(0x64));
    QCOMPARE(rs01Checksum(bytes({ 0x8B, 0x04, 0x00 })), quint8(0x0F));   // the notes' «8B 04 00 0F»
}

// «81 01»: the command and its checksum (no parameters).
void RS01ClockTests::testIdentifyCommand()
{
    QCOMPARE(rs01IdentifyCommand(), padded(bytes({ 0x81, 0x01 })));
}

void RS01ClockTests::testClockCommand()
{
    const QDateTime t(QDate(2026, 10, 21), QTime(22, 15, 3));
    QCOMPARE(rs01ClockCommand(t), padded(bytes({ 0x83, 0x1A, 0x0A, 0x15, 0x16, 0x0F, 0x03, 0x00, 0x00, 0x64 })));
}

// The year is one byte from 2000 and must stay 7-bit.
void RS01ClockTests::testClockYearRange()
{
    QVERIFY(rs01ClockCommand(QDateTime(QDate(1999, 12, 31), QTime(23, 59, 59))).isEmpty());
    QVERIFY(rs01ClockCommand(QDateTime(QDate(2128, 1, 1), QTime(0, 0))).isEmpty());
    QCOMPARE(rs01ClockCommand(QDateTime(QDate(2127, 1, 1), QTime(0, 0))).at(1), char(127));
}

// Replies as the device sent them, with or without the report ID in front.
void RS01ClockTests::testReplies()
{
    const QByteArray identify = bytes({ 0xF1 }) + QByteArray("RS01    ") + bytes({ 0x77 });
    QVERIFY(rs01IsIdentifyReply(padded(identify)));
    QVERIFY(rs01IsIdentifyReply(bytes({ 0x00 }) + padded(identify)));
    QVERIFY(!rs01IsIdentifyReply(padded(bytes({ 0xF1 }) + QByteArray("RS01    ") + bytes({ 0x76 }))));   // checksum
    QVERIFY(!rs01IsIdentifyReply(padded(bytes({ 0x82 }) + QByteArray("RS01    ") + bytes({ 0x78 }))));   // protocol B
    QVERIFY(!rs01IsIdentifyReply(padded(bytes({ 0xF1 }) + QByteArray("XX99    ") + bytes({ 0x11 }))));
    QVERIFY(!rs01IsIdentifyReply(QByteArray()));

    QVERIFY(rs01IsClockReply(padded(bytes({ 0xF3, 0x00, 0x73 }))));
    QVERIFY(rs01IsClockReply(bytes({ 0x00 }) + padded(bytes({ 0xF3, 0x00, 0x73 }))));
    QVERIFY(!rs01IsClockReply(padded(bytes({ 0xF3, 0x01, 0x74 }))));   // not "done"
    QVERIFY(!rs01IsClockReply(padded(bytes({ 0xF3, 0x00, 0x72 }))));
    QVERIFY(!rs01IsClockReply(bytes({ 0xF3 })));
}

// Each way setting the clock can fail has its own words; "not found" says how to connect it.
void RS01ClockTests::testErrorTexts()
{
    QSet<QString> texts;
    for (RS01Hid::Error e : { RS01Hid::Error::NotAvailable, RS01Hid::Error::NotFound, RS01Hid::Error::CannotOpen,
                              RS01Hid::Error::NotRS01, RS01Hid::Error::NoReply, RS01Hid::Error::BadReply }) {
        const QString t = RS01Hid::errorText(e);
        QVERIFY(!t.isEmpty());
        texts.insert(t);
    }
    QCOMPARE(texts.size(), 6);
    QVERIFY(RS01Hid::errorText(RS01Hid::Error::NotFound).contains(QStringLiteral("before")));
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QVERIFY(RS01Hid::available());
#else
    QVERIFY(!RS01Hid::available());
#endif
}

// Waiting for a reply: one deadline for the whole exchange, unrelated reports let pass, and when no
// accepted reply comes the last unrelated one is what the device said (not "no reply").
void RS01ClockTests::testAwaitReplyWithinDeadline()
{
    const QByteArray other = padded(bytes({ 0xF4, 0x00, 0x74 }));
    const QByteArray done = padded(bytes({ 0xF3, 0x00, 0x73 }));
    QList<int> asked;
    QList<QByteArray> replies { other, done };
    auto read = [&](int timeoutMs) {
        asked << timeoutMs;
        return replies.isEmpty() ? QByteArray() : replies.takeFirst();
    };
    QCOMPARE(rs01AwaitReply(read, rs01IsClockReply, 1000), done);
    QCOMPARE(asked.size(), 2);
    QVERIFY(asked[0] <= 1000 && asked[1] <= asked[0]);

    // the device keeps sending something else: stop at the deadline, give back what it said
    asked.clear();
    int calls = 0;
    auto chatty = [&](int timeoutMs) {
        asked << timeoutMs;
        ++calls;
        QThread::msleep(30);
        return other;
    };
    QElapsedTimer t;
    t.start();
    QCOMPARE(rs01AwaitReply(chatty, rs01IsClockReply, 200), other);
    QVERIFY2(t.elapsed() < 400, qPrintable(QString::number(t.elapsed())));
    QVERIFY(calls >= 2);

    // silence: empty
    QCOMPARE(rs01AwaitReply([](int) { return QByteArray(); }, rs01IsClockReply, 100), QByteArray());
}
