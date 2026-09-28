/* Loader Robustness Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "loaderrobustnesstests.h"

#include "SleepLib/loader_plugins/edfparser.h"
#include "SleepLib/loader_plugins/prisma_loader.h"

namespace {

QByteArray field(const QString& value, int width)
{
    return value.toLatin1().leftJustified(width, ' ', true);
}

struct Signal {
    QString label;
    QString samples;            // samples per record, as written in the header
    QString reserved = QString();
    QString digMin = QStringLiteral("-32768");
    QString digMax = QStringLiteral("32767");
};

// A minimal EDF: 256-byte main header, 256 bytes per signal, then \a data.
QByteArray makeEdf(const QList<Signal>& sigs, const QString& records, const QByteArray& data)
{
    const int ns = sigs.size();
    QByteArray out;
    out += field(QStringLiteral("0"), 8);
    out += field(QStringLiteral("patient"), 80);
    out += field(QStringLiteral("recording"), 80);
    out += field(QStringLiteral("01.09.2623.00.00"), 16);
    out += field(QString::number(256 + 256 * ns), 8);
    out += field(QString(), 44);
    out += field(records, 8);
    out += field(QStringLiteral("1"), 8);
    out += field(QString::number(ns), 4);
    for (const auto& s : sigs) out += field(s.label, 16);
    for (int i = 0; i < ns; ++i) out += field(QString(), 80);
    for (int i = 0; i < ns; ++i) out += field(QStringLiteral("L/min"), 8);
    for (int i = 0; i < ns; ++i) out += field(QStringLiteral("-100"), 8);
    for (int i = 0; i < ns; ++i) out += field(QStringLiteral("100"), 8);
    for (const auto& s : sigs) out += field(s.digMin, 8);
    for (const auto& s : sigs) out += field(s.digMax, 8);
    for (int i = 0; i < ns; ++i) out += field(QString(), 80);
    for (const auto& s : sigs) out += field(s.samples, 8);
    for (const auto& s : sigs) out += field(s.reserved, 32);
    out += data;
    return out;
}

QByteArray int16s(const QList<qint16>& values)
{
    QByteArray b;
    for (qint16 v : values) {
        b += char(v & 0xff);
        b += char((v >> 8) & 0xff);
    }
    return b;
}

} // namespace

void LoaderRobustnessTests::testEdfValidFileParses()
{
    EDFInfo edf;
    QVERIFY(edf.Open(makeEdf({{QStringLiteral("Flow"), QStringLiteral("4")}}, QStringLiteral("2"),
                             int16s({1, 2, 3, 4, 5, 6, 7, 8}))));
    QVERIFY(edf.Parse());
    EDFSignal* sig = edf.lookupLabel(QStringLiteral("Flow"));
    QVERIFY(sig != nullptr);
    QVERIFY(sig->dataArray != nullptr);
    QCOMPARE(sig->dataArray[0], qint16(1));
    QCOMPARE(sig->dataArray[7], qint16(8));
}

void LoaderRobustnessTests::testEdfNegativeSampleCountRejected()
{
    EDFInfo edf;
    QVERIFY(edf.Open(makeEdf({{QStringLiteral("Flow"), QStringLiteral("-1")}}, QStringLiteral("2"),
                             int16s({1, 2, 3, 4}))));
    QVERIFY(!edf.Parse());
}

void LoaderRobustnessTests::testEdfOverflowingSizesRejected()
{
    // 65536 samples x 65537 records x 2 bytes wraps a 32-bit size to a small
    // number; the data is far too short, so the file must be rejected.
    EDFInfo edf;
    QVERIFY(edf.Open(makeEdf({{QStringLiteral("Flow"), QStringLiteral("65536")}}, QStringLiteral("65537"),
                             int16s({1, 2, 3, 4}))));
    QVERIFY(!edf.Parse());
}

void LoaderRobustnessTests::testEdfTruncatedFileRejected()
{
    EDFInfo edf;
    QVERIFY(edf.Open(makeEdf({{QStringLiteral("Flow"), QStringLiteral("4")}}, QStringLiteral("3"),
                             int16s({1, 2, 3, 4, 5, 6}))));        // needs 12 samples
    QVERIFY(!edf.Parse());
}

void LoaderRobustnessTests::testEdfAnnotationWithoutSeparator()
{
    // An annotation record that starts like an onset but never has a separator.
    QByteArray record = QByteArrayLiteral("+123456789012345");
    QCOMPARE(record.size(), 16);
    EDFInfo edf;
    QVERIFY(edf.Open(makeEdf({{QStringLiteral("EDF Annotations"), QStringLiteral("8")}}, QStringLiteral("1"),
                             record)));
    QVERIFY(edf.Parse());               // the file is well-formed; the record is just unusable
    QVERIFY(!edf.annotations.isEmpty());
    QVERIFY(edf.annotations.first().isEmpty());
}

void LoaderRobustnessTests::testWmedfWithoutStorageFlagsRejected()
{
    // No signal says whether it is stored as 8 or 16 bits: there is no record
    // size, and dividing by it crashed.
    WMEDFInfo wmedf;
    QVERIFY(wmedf.Open(makeEdf({{QStringLiteral("RespFlow"), QStringLiteral("4")}}, QStringLiteral("1"),
                               int16s({1, 2, 3, 4}))));
    QVERIFY(!wmedf.Parse());
}

void LoaderRobustnessTests::testWmedfEightBitSignalParses()
{
    WMEDFInfo wmedf;
    Signal s { QStringLiteral("ObstructLevel"), QStringLiteral("4"), QStringLiteral("#1"),
               QStringLiteral("0"), QStringLiteral("255") };
    QVERIFY(wmedf.Open(makeEdf({s}, QStringLiteral("1"), QByteArray("\x01\x02\x03\x04", 4))));
    QVERIFY(wmedf.Parse());
    EDFSignal* sig = wmedf.lookupLabel(QStringLiteral("ObstructLevel"));
    QVERIFY(sig != nullptr && sig->dataArray != nullptr);
    QCOMPARE(sig->dataArray[3], qint16(4));
}
