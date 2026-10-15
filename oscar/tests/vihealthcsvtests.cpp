/* ViHealth CSV oximetry import Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "vihealthcsvtests.h"
#include "SleepLib/loader_plugins/vihealth_csv_loader.h"

#include <QFile>
#include <QTemporaryDir>

namespace {

//! A CSV as ViHealth writes it: \a header, then one line every 4 s from \a start.
QString csv(const QString &header, const QDateTime &start, const QList<QPair<QString, QString>> &values)
{
    QString text = header + QStringLiteral("\n");
    QDateTime t = start;
    for (const auto &v : values) {
        text += QLocale::c().toString(t, QStringLiteral("HH:mm:ss MMM d yyyy"))
              + QStringLiteral(",%1,%2,3,0,0\n").arg(v.first, v.second);
        t = t.addSecs(4);
    }
    return text;
}

QString writeFile(QTemporaryDir &dir, const QString &name, const QString &text)
{
    const QString path = dir.path() + QStringLiteral("/") + name;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(text.toUtf8());
    return path;
}

const QString kRussian = QStringLiteral("Время,Уровень кислорода,Частота пульса,Движение,Напоминание O₂,Напоминание PR");
const QString kEnglish = QStringLiteral("Time,Oxygen Level,Pulse Rate,Motion,O2 Reminder,PR Reminder");

} // namespace

// The Russian ViHealth export: one session from the first line's time, a sample every 4 s.
void ViHealthCsvTests::testRussianExport()
{
    QTemporaryDir dir;
    const QDateTime start(QDate(2026, 10, 13), QTime(23, 37, 38));
    QList<QPair<QString, QString>> values;
    for (int i = 0; i < 20; ++i) values << qMakePair(QString::number(94 + i % 3), QString::number(60 + i % 5));
    values[5] = qMakePair(QStringLiteral("--"), QStringLiteral("--"));   // no reading
    const QString path = writeFile(dir, QStringLiteral("O2Ring _20261013233738.csv"), csv(kRussian, start, values));

    ViHealthCsvLoader loader;
    QCOMPARE(loader.Open(path), 1);
    QCOMPARE(loader.oxisessions.size(), 1);
    QCOMPARE(loader.startTime(), start);
    QCOMPARE(loader.importResolution(), qint64(4000));
    const QVector<OxiRecord> *rec = loader.oxisessions.value(start);
    QVERIFY(rec != nullptr);
    QCOMPARE(rec->size(), 20);
    QCOMPARE(int(rec->at(0).spo2), 94);
    QCOMPARE(int(rec->at(0).pulse), 60);
    QCOMPARE(int(rec->at(5).spo2), 0);
    QCOMPARE(int(rec->at(5).pulse), 0);
    QCOMPARE(int(rec->at(19).spo2), 94 + 19 % 3);
    loader.trashRecords();
}

void ViHealthCsvTests::testEnglishExport()
{
    QTemporaryDir dir;
    const QDateTime start(QDate(2026, 1, 2), QTime(1, 2, 3));
    QList<QPair<QString, QString>> values;
    for (int i = 0; i < 12; ++i) values << qMakePair(QStringLiteral("97"), QStringLiteral("55"));
    const QString path = writeFile(dir, QStringLiteral("export.CSV"), csv(kEnglish, start, values));

    ViHealthCsvLoader loader;
    QCOMPARE(loader.Open(path), 1);
    QCOMPARE(loader.startTime(), start);
    QCOMPARE(loader.oxisessions.value(start)->size(), 12);
    loader.trashRecords();
}

// A short break is filled with empty samples; a long one starts a new session.
void ViHealthCsvTests::testGapSplitsSessions()
{
    QTemporaryDir dir;
    const QDateTime a(QDate(2026, 10, 13), QTime(23, 0, 0));
    QList<QPair<QString, QString>> values;
    for (int i = 0; i < 15; ++i) values << qMakePair(QStringLiteral("96"), QStringLiteral("70"));
    QString text = csv(kRussian, a, values);
    // 5 missing samples, then 15 more
    text += csv(QString(), a.addSecs(15 * 4 + 20), values).mid(1);
    // 30 min break: a new session
    const QDateTime b = a.addSecs(3600);
    text += csv(QString(), b, values).mid(1);
    const QString path = writeFile(dir, QStringLiteral("gaps.csv"), text);

    ViHealthCsvLoader loader;
    QCOMPARE(loader.Open(path), 1);
    QCOMPARE(loader.oxisessions.size(), 2);
    QCOMPARE(loader.oxisessions.value(a)->size(), 15 + 5 + 15);
    QCOMPARE(int(loader.oxisessions.value(a)->at(16).spo2), 0);
    QCOMPARE(loader.oxisessions.value(b)->size(), 15);
    QCOMPARE(loader.startTime(), a);
    loader.trashRecords();
}

// A CSV of another kind is left to other loaders.
void ViHealthCsvTests::testOtherCsvRefused()
{
    QTemporaryDir dir;
    ViHealthCsvLoader loader;
    QCOMPARE(loader.Open(writeFile(dir, QStringLiteral("dreem.csv"), QStringLiteral("Type;Start Time;Stop Time\nnight;2026-10-13;x\n"))), 0);
    QCOMPARE(loader.Open(writeFile(dir, QStringLiteral("empty.csv"), QString())), 0);
    QCOMPARE(loader.Open(dir.path() + QStringLiteral("/missing.csv")), 0);
    QCOMPARE(loader.Open(writeFile(dir, QStringLiteral("o2.spo"), QStringLiteral("x"))), 0);
    QVERIFY(loader.oxisessions.isEmpty());
}
