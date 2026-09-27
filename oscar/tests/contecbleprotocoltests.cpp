/* Contec BLE Protocol Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contecbleprotocoltests.h"
#include "SleepLib/loader_plugins/contec_ble_protocol.h"

#include <vector>

using namespace ContecBle;

namespace {

QByteArray hex(const char *s) { return QByteArray::fromHex(QByteArray(s)); }
QString spaced(const QByteArray &b) { return QString::fromLatin1(b.toHex(' ')); }
QVector<int> ints(const std::vector<int> &v) { return QVector<int>(v.begin(), v.end()); }

// Produced by running the vendor Java parsers on random packets (ContecBTdiscover/tests/data/vectors.txt).
struct EdVector { int version; const char *raw; std::vector<int> difference; std::vector<int> original; };
const EdVector kEdVectors[] = {
    { 11, "67204e312f0e591274251915672450633b54277f45655158100b3f650355",
      { 37, 36, 35, 36, 41, 47, 255, 253, 257, 262, 262, 268, 271, 274, 271, 276, 280, 278, 255, 255, 255, 251, 256, 250, 255, 250, 251 },
      { 116, 165, 153, 149, 103, 36, 80, 227, 59, 84, 167, 255, 69, 229, 81, 216, 16, 11, 191, 101, 3 } },
    { 11, "043b3d3a5322323c78113168527d625972324c247c4e092e632e237d7317",
      { 17, 20, 21, 15, 15, 10, 12, 255, 250, 244, 246, 251, 250, 255, 257, 260, 262, 266, 262, 260, 264, 255, 251, 247, 241, 241, 240 },
      { 120, 145, 49, 104, 82, 253, 98, 89, 242, 50, 76, 164, 252, 78, 9, 46, 227, 174, 163, 253, 115 } },
    { 14, "2f7537232a66286538100c325f711574672511780b260354462024337b7e",
      { 198, 198, 194, 191, 193, 198, 255, 255, 256, 255, 260, 255, 259, 265, 255, 257, 262, 263, 264, 255, 255, 255, 252, 250, 256, 256, 259 },
      { 56, 144, 140, 50, 95, 241, 149, 116, 103, 37, 145, 120, 139, 38, 131, 84, 198, 32, 36, 179, 251 } },
    { 14, "73732c5d0101670242266a474a36586e473d0e274b0972715054305a734b",
      { 112, 106, 104, 108, 255, 259, 257, 260, 266, 271, 271, 277, 271, 275, 255, 252, 247, 247, 241, 243, 255, 259, 256, 256, 255, 255, 257 },
      { 194, 38, 106, 71, 74, 54, 88, 238, 199, 189, 14, 39, 203, 137, 114, 241, 80, 84, 48, 90, 115 } },
    { 14, "6a1221007b51326a49372178697254104e583e1a2e195e7f6d7c2b40610b",
      { 97, 95, 96, 255, 255, 249, 248, 255, 257, 252, 256, 255, 255, 251, 245, 250, 250, 253, 247, 246, 244, 246, 240, 241, 240, 235, 229 },
      { 201, 55, 33, 120, 233, 114, 212, 16, 206, 88, 62, 154, 174, 25, 94, 255, 109, 252, 43, 192, 225 } },
};

} // namespace

void ContecBleProtocolTests::testCommandBytes()
{
    QCOMPARE(spaced(cmdId()), QStringLiteral("81 01"));
    QCOMPARE(spaced(cmdInfo()), QStringLiteral("82 02"));
    QCOMPARE(spaced(cmdPrepare()), QStringLiteral("8f 04 00 13"));
    QCOMPARE(spaced(cmdStorage()), QStringLiteral("9f 1f"));
    QCOMPARE(spaced(cmdFormats()), QStringLiteral("8e 06 14"));
    QCOMPARE(spaced(cmdCountRecords()), QStringLiteral("90 06 16"));
    QCOMPARE(spaced(cmdNextHeader()), QStringLiteral("9c 01 1d"));
    QCOMPARE(spaced(cmdEraseAllRecords()), QStringLiteral("9d 7f 7f 7f 7f 00 00 19"));
}

// FE 06 reports format bits 1/2/4, but 9D uses the codes 01/03/04 (02 aborts a channel).
void ContecBleProtocolTests::testChannelCommands()
{
    const QByteArray diff = cmdChannel(FmtDifference, 1, 5, 6, 300);
    QCOMPARE(diff, frame({0x9D, 0x01, 0x01, 0x05, 0x06, 0x2C, 0x02}));
    const QByteArray orig = cmdChannel(FmtOriginal, 2, 5, 6);
    QCOMPARE(orig.size(), 9);
    QCOMPARE(quint8(orig[1]), quint8(0x03));
    QCOMPARE(quint8(cmdChannel(FmtCode, 3, 5, 6)[1]), quint8(0x04));
    QCOMPARE(cmdChannelAbort(1, 5, 6), frame({0x9D, 0x02, 0x01, 0x05, 0x06, 0x00, 0x00}));
    QCOMPARE(formatCode(FmtDifference), quint8(0x01));
    QCOMPARE(formatCode(FmtOriginal), quint8(0x03));
    QCOMPARE(formatCode(FmtCode), quint8(0x04));
}

void ContecBleProtocolTests::testSetTimeCommand()
{
    const QDateTime t(QDate(2026, 9, 28), QTime(7, 30, 15, 300));
    QCOMPARE(cmdSetTime(t), frame({0x83, 26, 9, 28, 7, 30, 15, 300 & 0x7F, 300 >> 7}));
}

void ContecBleProtocolTests::testPickFormat()
{
    QCOMPARE(pickFormat(0x07), int(FmtOriginal));
    QCOMPARE(pickFormat(0x05), int(FmtCode));
    QCOMPARE(pickFormat(0x01), int(FmtDifference));
}

void ContecBleProtocolTests::testDestructiveGuard()
{
    QVERIFY(isDestructive(cmdEraseAllRecords()));
    QVERIFY(isDestructive(frame({0xA1, 0x00})));
    QVERIFY(isDestructive(frame({0x91, 0x7F})));
    QVERIFY(!isDestructive(frame({0x91, 0x7E})));
    QVERIFY(!isDestructive(cmdChannel(FmtOriginal, 1, 5, 6)));
    QVERIFY(!isDestructive(cmdChannelAbort(1, 5, 6)));
    QVERIFY(!isDestructive(cmdStorage()));
}

void ContecBleProtocolTests::testModelLookup()
{
    QCOMPARE(modelForName(QStringLiteral("SpO202")).model, QStringLiteral("CMS50FW"));
    QCOMPARE(modelForName(QStringLiteral("SpO202")).variant, 'A');
    QCOMPARE(modelForName(QStringLiteral("SpO210xyz")).model, QStringLiteral("CMS50K1"));
    QCOMPARE(modelForName(QStringLiteral("SpO210xyz")).variant, 'K');
    QVERIFY(!modelForName(QStringLiteral("SpO203")).isValid());
    QVERIFY(!modelForName(QString()).isValid());
}

void ContecBleProtocolTests::testFrameLengths()
{
    QCOMPARE(frameLength(hex("f200000206000007")), 0);           // F2 needs 9 bytes before it knows
    QCOMPARE(frameLength(hex("f200000206000d0700")), 9);         // no strings: fixed 9 bytes
    QCOMPARE(frameLength(hex("e006")), 15);
    QCOMPARE(frameLength(hex("e000")), 7);
    QCOMPARE(frameLength(hex("ed03")), 30);
    QCOMPARE(frameLength(hex("ed01")), 24);
    QCOMPARE(frameLength(hex("ed7f")), 7);
    QCOMPARE(frameLength(hex("8415")), 21 + 4 + 6);
    QCOMPARE(frameLength(hex("ec")), 21);
    QCOMPARE(frameLength(hex("11")), 1);
    QCOMPARE(frameLength(hex("ed")), 0);
}

void ContecBleProtocolTests::testSplitterHandlesSplitAndMergedNotifications()
{
    const QByteArray f1 = frame({0xF1, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'});
    const QByteArray waveBody = hex("eb00054003");
    const QByteArray wave = waveBody + char(checksum(waveBody));
    const QByteArray stream = hex("1122") + f1 + wave + wave;

    FrameSplitter sp;
    QList<QByteArray> out = sp.feed(stream.left(5));
    out += sp.feed(stream.mid(5, 12));
    out += sp.feed(stream.mid(17));
    QCOMPARE(out.size(), 3);
    QCOMPARE(out[0], f1);
    QCOMPARE(out[1], wave);
    QCOMPARE(out[2], wave);
    QCOMPARE(sp.skippedBytes(), 2);
    QCOMPARE(parseF1(f1), QStringLiteral("ABCDEFGH"));
}

// Answers captured from a CMS50FW (firmware 2.0.0) on 27.09.2026.
void ContecBleProtocolTests::testParseDeviceAnswers()
{
    const DeviceInfo info = parseF2(hex("f200000206000d0700"));
    QCOMPARE(info.firmware, QStringLiteral("2.0.0"));
    QCOMPARE(info.protocolVersion, 13);

    const StorageStatus st = parseEF(hex("ef00014000000030"));
    QVERIFY(st.hasData);
    QVERIFY(st.hasRecords());
    QCOMPARE(st.mask2, 0);

    QCOMPARE(parseE0Count(hex("e0060600060000521f006c4c1b7026")), 6);
    QCOMPARE(parseFE06Formats(hex("fe060700000000")), 7);
    QCOMPARE(parseF1(frame({0xF1, '5', '0', 'F', ' ', ' ', ' ', ' ', ' '})), QStringLiteral("50F     "));
}

void ContecBleProtocolTests::testRecordHeaderAndMidnight()
{
    const int n = 21900;   // 6 h 05 min at 1 Hz
    QByteArray body;
    for (int v : {0xEC, 0x40, 5, 6, 26, 9, 25, 23, 55, 0, n & 0x7F, (n >> 7) & 0x7F, (n >> 14) & 0x7F, 0, 0, 0, 0, 0, 0, 0})
        body.append(char(v));
    const RecordHeader h = parseEC(body + char(checksum(body)));
    QVERIFY(h.last);
    QVERIFY(!h.hasPI);
    QCOMPARE(h.l, 5);
    QCOMPARE(h.m, 6);
    QCOMPARE(h.samples, n);
    QCOMPARE(h.start(), QDateTime(QDate(2026, 9, 25), QTime(23, 55, 0)));
    QCOMPARE(h.start().addSecs(h.samples - 1).date(), QDate(2026, 9, 26));   // runs past midnight

    QByteArray bad = body;
    bad[7] = char(30);   // hour 30 cannot be a valid start
    QVERIFY(!parseEC(bad + char(checksum(bad))).start().isValid());
}

void ContecBleProtocolTests::testEdVectorsMatchVendorCode()
{
    for (const EdVector &v : kEdVectors) {
        const QByteArray raw = hex(v.raw);
        QCOMPARE(parseEdOriginal(raw), ints(v.original));
        QCOMPARE(parseEdDifference(raw, v.version), ints(v.difference));
    }
}

// Expected values produced by the reference Python CodeDecoder; packet A ends with a lone
// escape byte that packet B completes (base 0x58 = 88).
void ContecBleProtocolTests::testCodeDecoderAcrossPackets()
{
    CodeDecoder d;
    const QVector<int> a = d.feed(hex("ed040100000300407670123f0021100f4405112330010203405060707534"));
    const QVector<int> b = d.feed(hex("ed040101003100007801122f76740013310f22100121123344044011004d"));
    QCOMPARE(a, QVector<int>({95, 94, 93, 96, 96, 94, 95, 95, 96, 96, 92, 92, 96, 91, 95, 95, 94, 93, 93, 96,
                              96, 95, 96, 94, 96, 93, 92, 96, 91, 96, 90, 96, 89, 96}));
    QCOMPARE(b, QVector<int>({88, 87, 87, 86, 86, 100, 100, 99, 97, 97, 99, 100, 98, 98, 99, 100, 100, 99, 98, 99,
                              99, 98, 97, 97, 96, 96, 100, 96, 96, 100, 99, 99, 100, 100}));
}
