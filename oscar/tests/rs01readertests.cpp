/* Contec RS01 Reader Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "rs01readertests.h"

#include <QTemporaryDir>
#include "SleepLib/loader_plugins/rs01_reader.h"
#include "tests/rs01_synth.h"

using namespace rs01synth;

namespace {

QByteArray steady(int records, int flow = 2008, int pr = 60, int spo2 = 96)
{
    return segment(1, records, [=](int i) { return record(i, flow, pr, spo2); });
}

} // namespace

// The format doc's example header reads as written: every rate and scale from the header.
void RS01ReaderTests::testHeaderParsesExample()
{
    const RS01Header h = rs01ParseHeader(header());
    QVERIFY2(h.valid, qPrintable(h.error));
    QCOMPARE(h.start, QDateTime(QDate(2026, 10, 17), QTime(1, 50, 53)));
    QCOMPARE(h.segHeaderBytes, 512);
    QCOMPARE(h.recordsPerSeg, 600);
    QCOMPARE(h.recordSeconds, 1);
    QCOMPARE(h.channels.size(), 4);
    const RS01Signal &flow = h.channels[h.indexOf(QStringLiteral("Flow"))];
    QCOMPARE(flow.samplesPerRecord, 50);
    QCOMPARE(flow.bytesPerSample, 2);
    QCOMPARE(flow.unit, QStringLiteral("Pa"));
    QCOMPARE(flow.digMax, 4095.0);
    QCOMPARE(flow.physMin, -511.0);
    QCOMPARE(h.channels[h.indexOf(QStringLiteral("Pleth"))].samplesPerRecord, 100);
    QCOMPARE(h.recordBytes(), 206);
}

// Nothing hard-coded: the vendor's own sample file had the pleth at 60 Hz.
void RS01ReaderTests::testHeaderSixtyHzPleth()
{
    const RS01Header h = rs01ParseHeader(header(QStringLiteral("20261017015053"), 60));
    QVERIFY(h.valid);
    QCOMPARE(h.channels[h.indexOf(QStringLiteral("Pleth"))].samplesPerRecord, 60);
    QCOMPARE(h.recordBytes(), 166);
}

void RS01ReaderTests::testHeaderInvalid()
{
    QVERIFY(!rs01ParseHeader(QByteArray(100, ' ')).valid);
    QByteArray noTime = header();
    noTime.replace(22, 14, QByteArray(14, ' '));
    const RS01Header h = rs01ParseHeader(noTime);
    QVERIFY(!h.valid);
    QVERIFY(!h.error.isEmpty());
}

// Two seconds: flow 50 Hz, pulse and SpO2 1 Hz, pleth 100 Hz, in physical units.
void RS01ReaderTests::testSegmentRecords()
{
    QTemporaryDir root;
    const QString dir = recording(root.path(), QStringLiteral("10170150"), header(), { { 1, steady(2, 2008, 61, 95) } });
    const RS01Recording r = rs01ReadRecording(dir);
    QVERIFY2(r.valid, qPrintable(r.error));
    QCOMPARE(r.seconds, 2);
    QCOMPARE(r.runs.value(QStringLiteral("Flow")).size(), 1);
    QCOMPARE(r.runs.value(QStringLiteral("Flow")).first().samples.size(), 100);
    QCOMPARE(r.runs.value(QStringLiteral("Flow")).first().intervalMs, 20.0);
    QCOMPARE(r.runs.value(QStringLiteral("PR")).first().samples, QVector<float>({ 61, 61 }));
    QCOMPARE(r.runs.value(QStringLiteral("SpO2")).first().samples, QVector<float>({ 95, 95 }));
    QCOMPARE(r.runs.value(QStringLiteral("Pleth")).first().samples.size(), 200);
    QCOMPARE(r.runs.value(QStringLiteral("SpO2")).first().startMs, QDateTime(QDate(2026, 10, 17), QTime(1, 50, 53)).toMSecsSinceEpoch());
}

// A second without the FF FF marker is missing: the signals break there, nothing is invented.
void RS01ReaderTests::testMissingMarkerIsGap()
{
    QTemporaryDir root;
    const QByteArray seg = segment(1, 5, [](int i) { return record(i, 2008, 60, 96, 60, 100, i != 3); });
    const RS01Recording r = rs01ReadRecording(recording(root.path(), QStringLiteral("10170150"), header(), { { 1, seg } }));
    QVERIFY(r.valid);
    const QVector<RS01Run> spo2 = r.runs.value(QStringLiteral("SpO2"));
    QCOMPARE(spo2.size(), 2);
    QCOMPARE(spo2[0].samples.size(), 2);
    QCOMPARE(spo2[1].startMs - spo2[0].startMs, qint64(3000));
    QCOMPARE(r.missingSeconds, 1);
}

void RS01ReaderTests::testIndexOutOfRangeIsGap()
{
    QTemporaryDir root;
    const QByteArray seg = segment(1, 3, [](int i) { return record(i == 2 ? 999 : i, 2008, 60, 96); });
    const RS01Recording r = rs01ReadRecording(recording(root.path(), QStringLiteral("10170150"), header(), { { 1, seg } }));
    QCOMPARE(r.runs.value(QStringLiteral("SpO2")).size(), 2);
}

// The index restarts in every segment: the second segment goes on 600 s later.
void RS01ReaderTests::testSegmentsContinueInTime()
{
    QTemporaryDir root;
    const QByteArray s1 = segment(1, 600, [](int i) { return record(i, 2008, 60, 96); });
    const QByteArray s2 = segment(2, 10, [](int i) { return record(i, 2008, 60, 96); });
    const RS01Recording r = rs01ReadRecording(recording(root.path(), QStringLiteral("10170150"), header(), { { 1, s1 }, { 2, s2 } }));
    QCOMPARE(r.seconds, 610);
    const QVector<RS01Run> spo2 = r.runs.value(QStringLiteral("SpO2"));
    QCOMPARE(spo2.size(), 1);   // no gap between the segments
    QCOMPARE(spo2.first().samples.size(), 610);
}

// A missing, empty or cut segment (the device's FAT damage): the others are kept, each at the
// time its file name gives, with a gap where the missing one was.
void RS01ReaderTests::testMissingSegmentIsGap()
{
    QTemporaryDir root;
    const QByteArray s1 = segment(1, 600, [](int i) { return record(i, 2008, 60, 96); });
    const QByteArray s3 = segment(3, 10, [](int i) { return record(i, 2008, 60, 96); });
    const QString dir = recording(root.path(), QStringLiteral("10170150"), header(), { { 1, s1 }, { 2, QByteArray() }, { 3, s3 } });
    const RS01Recording r = rs01ReadRecording(dir);
    QCOMPARE(r.seconds, 1210);
    const QVector<RS01Run> spo2 = r.runs.value(QStringLiteral("SpO2"));
    QCOMPARE(spo2.size(), 2);
    QCOMPARE(spo2[1].startMs - spo2[0].startMs, qint64(1200000));

    // a stray file ("001 copy.SEG") is not a segment and changes nothing
    write(QDir(dir).filePath(QStringLiteral("001 copy.SEG")), s3);
    QCOMPARE(rs01ReadRecording(dir).seconds, 1210);
}

// A segment whose number bytes were wiped (00 00 00): its file name says which it is.
void RS01ReaderTests::testWipedSegmentNumberUsesFileName()
{
    QTemporaryDir root;
    QByteArray s2 = segment(2, 10, [](int i) { return record(i, 2008, 60, 96); });
    s2.replace(0, 3, QByteArray(3, '\0'));
    const RS01Recording r = rs01ReadRecording(recording(root.path(), QStringLiteral("10170150"), header(),
                                                        { { 1, steady(600) }, { 2, s2 } }));
    QCOMPARE(r.seconds, 610);
}

// Copied on Windows or FAT: names in lower case, any folder name; other files around are ignored.
void RS01ReaderTests::testCopiesAnyNameAnyCase()
{
    QTemporaryDir root;
    const QString dir = QDir(root.path()).filePath(QStringLiteral("night copy"));
    QDir().mkpath(dir);
    write(QDir(dir).filePath(QStringLiteral("10170150.hd")), header());
    write(QDir(dir).filePath(QStringLiteral("001.seg")), steady(30));
    write(QDir(dir).filePath(QStringLiteral("0.indexHead")), QByteArray("spotlight"));
    const RS01Recording r = rs01ReadRecording(dir);
    QVERIFY2(r.valid, qPrintable(r.error));
    QCOMPARE(r.seconds, 30);
    QCOMPARE(r.name, QStringLiteral("10170150"));
    QCOMPARE(rs01FindRecordings(root.path()), QStringList({ dir }));   // a folder holding recordings
    QCOMPARE(rs01FindRecordings(dir), QStringList({ dir }));           // the recording itself
}

// The zero is the recording's own median (≈2008, not 2048); inspiration (lower values) is positive.
void RS01ReaderTests::testFlowZeroAndSign()
{
    QTemporaryDir root;
    const QByteArray seg = segment(1, 9, [](int i) { return record(i, i == 5 ? 1908 : 2008, 60, 96); });
    const RS01Recording r = rs01ReadRecording(recording(root.path(), QStringLiteral("10170150"), header(), { { 1, seg } }));
    const QVector<float> flow = r.runs.value(QStringLiteral("Flow")).first().samples;
    QCOMPARE(flow.first(), 0.0f);
    const float inspiration = flow[4 * 50];
    QVERIFY2(inspiration > 0, qPrintable(QString::number(inspiration)));
    QVERIFY(qAbs(inspiration - 100.0f * 1022.0f / 4095.0f) < 0.01f);   // 100 counts at 1022 Pa / 4095 counts
}

// SpO2 or pulse 0: the finger is out, no sample (a gap), not a reading of 0.
void RS01ReaderTests::testNoSignalZeros()
{
    QTemporaryDir root;
    const QByteArray seg = segment(1, 4, [](int i) { return record(i, 2008, i == 2 ? 0 : 60, i == 2 ? 0 : 96); });
    const RS01Recording r = rs01ReadRecording(recording(root.path(), QStringLiteral("10170150"), header(), { { 1, seg } }));
    const QVector<RS01Run> spo2 = r.runs.value(QStringLiteral("SpO2"));
    QCOMPARE(spo2.size(), 2);
    for (const RS01Run &run : spo2) QVERIFY(!run.samples.contains(0.0f));
    QCOMPARE(r.runs.value(QStringLiteral("Flow")).size(), 1);   // the flow went on
}

// A segment cut inside a record: read up to the last whole record.
void RS01ReaderTests::testTruncatedSegment()
{
    QTemporaryDir root;
    QByteArray seg = steady(5);
    seg.chop(50);
    const RS01Recording r = rs01ReadRecording(recording(root.path(), QStringLiteral("10170150"), header(), { { 1, seg } }));
    QVERIFY(r.valid);
    QCOMPARE(r.seconds, 4);
}

// The recordings on the device (or a copy): MMDDhhmm folders with their header.
void RS01ReaderTests::testFindRecordings()
{
    QTemporaryDir root;
    recording(root.path(), QStringLiteral("10170150"), header(), { { 1, steady(2) } });
    recording(root.path(), QStringLiteral("10172127"), header(QStringLiteral("20261017212710")), { { 1, steady(2) } });
    QDir(root.path()).mkpath(QStringLiteral("System Volume Information"));
    QCOMPARE(rs01FindRecordings(root.path()).size(), 2);
    QVERIFY(rs01FindRecordings(root.path()).first().endsWith(QStringLiteral("10170150")));
}

namespace {

// Mean of a signal's samples between two clock times of the recording's day.
double meanBetween(const RS01Recording &r, const QString &label, const QTime &from, const QTime &to)
{
    const qint64 a = QDateTime(r.header.start.date(), from).toMSecsSinceEpoch();
    const qint64 b = QDateTime(r.header.start.date(), to).toMSecsSinceEpoch();
    double sum = 0;
    int n = 0;
    for (const RS01Run &run : r.runs.value(label)) {
        for (int k = 0; k < run.samples.size(); ++k) {
            const qint64 t = run.startMs + qint64(k * run.intervalMs);
            if (t >= a && t < b) { sum += run.samples[k]; ++n; }
        }
    }
    return n ? sum / n : qQNaN();
}

} // namespace

// The owner's own recordings, only where RS01_ROOT names their folder: never in the repository.
// The night, the polarity test and the device's FAT-damaged recordings of 2024.
void RS01ReaderTests::testRealRecordingsIfGiven()
{
    const QString root = qEnvironmentVariable("RS01_ROOT");
    if (root.isEmpty()) QSKIP("RS01_ROOT not set");

    const RS01Recording night = rs01ReadRecording(QDir(root).filePath(QStringLiteral("10170150")));
    QVERIFY2(night.valid, qPrintable(night.error));
    QCOMPARE(night.seconds, 27937);
    QCOMPARE(night.header.start.addSecs(night.seconds).time(), QTime(9, 36, 30));
    const double spo2 = meanBetween(night, QStringLiteral("SpO2"), QTime(1, 50), QTime(9, 37));
    const double pr = meanBetween(night, QStringLiteral("PR"), QTime(1, 50), QTime(9, 37));
    float nadir = 101;
    qint64 nadirAt = 0;
    for (const RS01Run &run : night.runs.value(QStringLiteral("SpO2"))) {
        for (int k = 0; k < run.samples.size(); ++k) {
            if (run.samples[k] < nadir) { nadir = run.samples[k]; nadirAt = run.startMs + qint64(k * run.intervalMs); }
        }
    }
    qInfo() << "night: SpO2 mean" << spo2 << "nadir" << nadir << "at" << QDateTime::fromMSecsSinceEpoch(nadirAt).time()
            << "PR mean" << pr << "PR 06:42-06:45" << meanBetween(night, QStringLiteral("PR"), QTime(6, 42), QTime(6, 45));
    QVERIFY(qAbs(spo2 - 91.3) < 0.5);
    QCOMPARE(nadir, 73.0f);
    QVERIFY(qAbs(pr - 65) < 2);

    // inhales are up in OSCAR: the long exhales of 21:27 negative, the long inhales of 21:28:22 positive
    const RS01Recording polarity = rs01ReadRecording(QDir(root).filePath(QStringLiteral("10172127")));
    QVERIFY2(polarity.valid, qPrintable(polarity.error));
    const double exhales = meanBetween(polarity, QStringLiteral("Flow"), QTime(21, 27, 15), QTime(21, 27, 52));
    const double rest = meanBetween(polarity, QStringLiteral("Flow"), QTime(21, 28, 2), QTime(21, 28, 18));
    const double inhales = meanBetween(polarity, QStringLiteral("Flow"), QTime(21, 28, 22), QTime(21, 28, 45));
    qInfo() << "polarity: exhales" << exhales << "rest" << rest << "inhales" << inhales << "Pa";
    QVERIFY(exhales < -5);
    QVERIFY(inhales > 5);
    QVERIFY(qAbs(rest) < 3);

    // the damaged ones: no crash, the readable parts kept
    for (const QString &dir : rs01FindRecordings(QDir(root).filePath(QStringLiteral("_damaged_2024")))) {
        const RS01Recording d = rs01ReadRecording(dir);
        qInfo() << "damaged" << QFileInfo(dir).fileName() << "valid" << d.valid << d.error << "start" << d.header.start
                << "seconds" << d.seconds << "missing" << d.missingSeconds << "SpO2 runs" << d.runs.value(QStringLiteral("SpO2")).size();
    }
    const RS01Recording cut = rs01ReadRecording(QDir(root).filePath(QStringLiteral("_damaged_2024/11140200")));
    QVERIFY2(cut.valid, qPrintable(cut.error));
    QVERIFY(cut.seconds > 9 * 600);   // segment 9 is whole: the reading went past the missing 5 and empty 7, 8
}
