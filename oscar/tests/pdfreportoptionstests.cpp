/* PDF Report Options Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "pdfreportoptionstests.h"

#include "pdfreportoptions.h"

void PdfReportOptionsTests::testPresets()
{
    PdfReportOptions o;
    o.personalData = false;
    o.serialNumbers = true;

    o.apply(PdfReportOptions::Everything);
    QVERIFY(o.summary && o.daily && o.overview && o.statistics);
    QCOMPARE(o.nights, PdfReportOptions::AllNights);
    QCOMPARE(o.overviewPreset, OverviewPresets::All);
    QVERIFY(o.statsSettings && o.statsOximetry && o.statsDevices);

    o.apply(PdfReportOptions::Brief);
    QVERIFY(o.summary && !o.daily && !o.overview && !o.statistics);

    o.apply(PdfReportOptions::Detailed);
    QVERIFY(o.summary && o.daily && o.overview && o.statistics);
    QCOMPARE(o.nights, PdfReportOptions::Last7Nights);
    QCOMPARE(o.overviewPreset, OverviewPresets::Therapy);
    QVERIFY(o.statsSettings && o.statsOximetry && !o.statsDevices);

    // presets leave the personal-data choices alone
    QVERIFY(!o.personalData && o.serialNumbers);
}

void PdfReportOptionsTests::testRange()
{
    const QDate last(2026, 10, 2);
    PdfReportOptions o;
    o.period = PdfReportOptions::Last7;
    QCOMPARE(o.range(last), qMakePair(QDate(2026, 9, 26), last));
    o.period = PdfReportOptions::Last30;
    QCOMPARE(o.range(last), qMakePair(QDate(2026, 9, 3), last));
    o.period = PdfReportOptions::Last90;
    QCOMPARE(o.range(last), qMakePair(QDate(2026, 7, 5), last));
    o.period = PdfReportOptions::Custom;
    o.from = QDate(2026, 2, 1);
    o.to = QDate(2026, 2, 28);
    QCOMPARE(o.range(last), qMakePair(QDate(2026, 2, 1), QDate(2026, 2, 28)));
}

void PdfReportOptionsTests::testNightsToPrint()
{
    QList<QDate> nights;
    for (QDate d(2026, 9, 20); d <= QDate(2026, 10, 2); d = d.addDays(1)) nights << d;
    nights << QDate(2026, 8, 1);   // outside the period
    PdfReportOptions o;            // Last30 up to the last of these nights
    o.period = PdfReportOptions::Custom;
    o.from = QDate(2026, 9, 3);
    o.to = QDate(2026, 10, 2);

    o.nights = PdfReportOptions::LastNight;
    QCOMPARE(o.nightsToPrint(nights), QList<QDate>({ QDate(2026, 10, 2) }));
    o.nights = PdfReportOptions::Last3;
    QCOMPARE(o.nightsToPrint(nights), QList<QDate>({ QDate(2026, 9, 30), QDate(2026, 10, 1), QDate(2026, 10, 2) }));
    o.nights = PdfReportOptions::AllNights;
    QCOMPARE(o.nightsToPrint(nights).size(), 13);
    QCOMPARE(o.nightsToPrint(nights).first(), QDate(2026, 9, 20));
    QVERIFY(o.nightsToPrint({}).isEmpty());
}

void PdfReportOptionsTests::testEstimatePages()
{
    PdfReportOptions o;
    o.apply(PdfReportOptions::Detailed);
    QCOMPARE(estimatePages(o, 7, 11, 6, 2), 1 + 7 * 2 + 1 + 2);
    o.apply(PdfReportOptions::Brief);
    QCOMPARE(estimatePages(o, 7, 11, 6, 2), 1);
}

void PdfReportOptionsTests::testMapRoundTrip()
{
    PdfReportOptions o;
    o.apply(PdfReportOptions::Detailed);
    o.period = PdfReportOptions::Custom;
    o.from = QDate(2026, 9, 3);
    o.to = QDate(2026, 10, 2);
    o.personalData = false;
    o.overviewPreset = OverviewPresets::Oxygen;
    const PdfReportOptions back = PdfReportOptions::fromMap(o.toMap());
    QCOMPARE(back.period, PdfReportOptions::Custom);
    QCOMPARE(back.from, o.from);
    QCOMPARE(back.to, o.to);
    QVERIFY(back.daily && back.overview && back.statistics && !back.statsDevices);
    QCOMPARE(back.overviewPreset, OverviewPresets::Oxygen);
    QVERIFY(!back.personalData);

    const PdfReportOptions defaults = PdfReportOptions::fromMap({ { QStringLiteral("period"), 99 } });
    QCOMPARE(defaults.period, PdfReportOptions::Last30);
    QVERIFY(defaults.summary && !defaults.daily);
}
