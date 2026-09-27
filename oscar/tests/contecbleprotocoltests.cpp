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

// Secure-frame vectors from the vendor Java code: key, iv, plain command, F4 frame,
// and the payload of the same frame when read back as an 0x84 answer.
struct SecureVector { const char *key, *iv, *plain, *frame, *rx; };
const SecureVector kSecureVectors[] = {
    { "359d41baf78afe0de1bbe7ae28c0450c", "e43c084f4bbb2bf1839dee466d020100", "0b73392f3023583b61647f751337385f7731661613", "f4156177670035453333723935502f3b345a61040c205f36185e1d0001020c", "0b73392f3023583b61647f751337385f7731661613" },
    { "f450279849599b56dd53b3351a572b40", "f27f72d37f347b5d9979162cfa020100", "236052490a4a0b606604282d196801", "f40f7605010b051e3c6931107e072e365400795700010223", "236052490a4a0b606604282d196801" },
    { "1192ed6a754300c523675af9b6c4a514", "cacaa1b6a736738853ee067b87020100", "1c794c691851302f112e3737533a", "f40e6a6f0020581312486a601f593f275b54120001022c", "1c794c691851302f112e3737533a" },
    { "79092bb2831b0279bb5070f331dc1178", "6481868c3667cbd3ed4091091e020100", "6a643a612350192c167b6e3d3e4635", "f40f120c013f6f39372d4c34152f536e236f3b1600010258", "6a643a612350192c167b6e3d3e4635" },
    { "a917160239c103a2db67f4c18eef29b3", "2fa68ba1aa5db9501dfbfb1177020100", "2f256f13", "f40408196a3a2b0001026b", "2f256f13" },
};
const char *const kF3Vector = "f310187e031a091b112a3253051a091b112a32530522";
// Produced by the reference Python port, which was checked 300/300 against the vendor ARM code.
struct KdfVector { const char *seed; const char *salt; const char *out; };
const KdfVector kKdfVectors[] = {
    { "000102030405060708090a0b0c0d0e0f", "50F     ", "007a97ac9fc5bff73417aad07f2eb7ca" },
    { "ffffffffffffffffffffffffffffffff", "SpO202", "7cd6d1d0f1dad5ea6211d6f5a2bafeca" },
    { "1a091b112a325305061a091b112a3253", "ABCDEFGH", "6bd6f7c7dafd82a3d761cb57b6d8e672" },
    { "80017f33009910ee0506070809a0b0c0", "", "c0a11c44c3e1aaae91c682c7cfdb688d" },
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

void ContecBleProtocolTests::testKdfMatchesReference()
{
    for (const KdfVector &v : kKdfVectors) {
        QCOMPARE(kdf(hex(v.seed), QByteArray(v.salt)), hex(v.out));
    }
}

void ContecBleProtocolTests::testSecureFramesMatchVendorCode()
{
    for (const SecureVector &v : kSecureVectors) {
        Keys keys;
        keys.keyTx = keys.keyRx = hex(v.key);
        keys.ivTx = keys.ivRx = hex(v.iv);
        const QByteArray f4 = buildF4(hex(v.plain), keys);
        QCOMPARE(f4, hex(v.frame));
        QByteArray as84 = f4;
        as84[0] = char(0x84);
        QCOMPARE(frameLength(as84), as84.size());   // F4 from a device is a 3-byte ack; 84 carries the length
        QCOMPARE(open84(as84, keys), hex(v.rx));
    }
}

void ContecBleProtocolTests::testF3FromVendorVector()
{
    const QByteArray a = hex(kF3Vector);
    const QByteArray seed = unpack7(a.mid(2, 3), a.mid(5, 16));
    QCOMPARE(buildF3(seed), a);
    QCOMPARE(seedFrom83(a), seed);
}

void ContecBleProtocolTests::testAppSeedLayout()
{
    const QByteArray s = appSeed(QDateTime(QDate(2026, 9, 27), QTime(18, 5, 7, 300)));
    QByteArray expected;
    for (int v : {26, 9, 27, 18 | 0x80, 5 | 0x80, 7, 300 & 0x7F, 300 >> 7,
                  26 | 0x80, 9 | 0x80, 27 | 0x80, 18 | 0x80, 5 | 0x80, 7 | 0x80, (300 & 0x7F) | 0x80, (300 >> 7) | 0x80})
        expected.append(char(v));
    QCOMPARE(s, expected);
    const QByteArray f3 = buildF3(s);
    QCOMPARE(f3.size(), 22);
    QCOMPARE(f3.left(2), hex("f310"));
    for (int i = 2; i < f3.size(); ++i) QVERIFY(quint8(f3[i]) < 0x80);
    QCOMPARE(seedFrom83(f3), s);
}

// Java's CTR increments the whole 128-bit IV: the second block of a stream that starts at
// ...00ff must equal a stream that starts at ...0100.
void ContecBleProtocolTests::testAesCtrCarriesAcrossTheWholeIv()
{
    const QByteArray key = hex("000102030405060708090a0b0c0d0e0f");
    const QByteArray zeros(32, '\0');
    const QByteArray a = aesCtr(zeros, key, hex("000000000000000000000000000000ff"));
    const QByteArray b = aesCtr(QByteArray(16, '\0'), key, hex("00000000000000000000000000000100"));
    QCOMPARE(a.size(), 32);
    QCOMPARE(a.mid(16), b);
    QCOMPARE(aesCtr(a, key, hex("000000000000000000000000000000ff")), zeros);   // CTR is its own inverse
}
