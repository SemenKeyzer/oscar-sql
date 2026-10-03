/* Device Settings Comparison Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "settingscomparisontests.h"

#include <QCoreApplication>
#include <QLocale>
#include <cmath>

#include "settingscomparison.h"

using namespace SettingsComparison;

namespace {

QList<QDate> nights(const QDate &first, int count)
{
    QList<QDate> dates;
    for (int i = 0; i < count; ++i) dates << first.addDays(i);
    return dates;
}

Period period(const QString &pressure, const QDate &first, int count, double hoursPerNight, double eventsPerHour,
              const QString &relief = QStringLiteral("SoftPAP: 1"), const QString &device = QStringLiteral("prisma"))
{
    Period p;
    p.mode = QStringLiteral("APAP");
    p.pressure = pressure;
    p.relief = relief;
    p.deviceKey = device;
    p.deviceLabel = device;
    p.dates = nights(first, count);
    p.hours = hoursPerNight * count;
    p.events = eventsPerHour * p.hours;
    return p;
}

Row rowWith(int nightCount, Column column, double value)
{
    Group g;
    g.dates = nights(QDate(2026, 9, 1), nightCount);
    Row r;
    r.group = g;
    r.values[column] = value;
    return r;
}

} // namespace

void SettingsComparisonTests::testGroupMergesSameSettings()
{
    // 9-16 on 20.09, 6-9 on 24-28.09, 9-16 again on 21-23.09: two groups, 9-16 has 4 nights
    const QList<Period> periods {
        period(QStringLiteral("9-16"), QDate(2026, 9, 20), 1, 7, 5),
        period(QStringLiteral("6-9"), QDate(2026, 9, 24), 5, 8, 2),
        period(QStringLiteral("9-16"), QDate(2026, 9, 21), 3, 7, 3),
    };
    const QList<Group> groups = group(periods);
    QCOMPARE(groups.size(), 2);

    // the group used last comes first
    QCOMPARE(groups[0].pressure, QStringLiteral("6-9"));
    QCOMPARE(groups[0].dates.size(), 5);

    const Group &g = groups[1];
    QCOMPARE(g.pressure, QStringLiteral("9-16"));
    QCOMPARE(g.dates, nights(QDate(2026, 9, 20), 4));    // merged and in date order
    QCOMPARE(g.hours, 28.0);
    QCOMPARE(g.events, 7 * 5 + 21 * 3.0);
}

void SettingsComparisonTests::testGroupKeepsDifferentSettingsApart()
{
    const QList<Period> periods {
        period(QStringLiteral("9-16"), QDate(2026, 9, 1), 3, 7, 5),
        period(QStringLiteral("9-16"), QDate(2026, 9, 4), 3, 7, 5, QStringLiteral("SoftPAP: 2")),
        period(QStringLiteral("9-16"), QDate(2026, 9, 7), 3, 7, 5, QStringLiteral("SoftPAP: 1"),
               QStringLiteral("airsense")),
    };
    QCOMPARE(group(periods).size(), 3);
}

void SettingsComparisonTests::testRowFigures()
{
    const Group g = group({ period(QStringLiteral("9-16"), QDate(2026, 9, 21), 4, 7, 3.5) }).first();
    const Row r = row(g);
    QCOMPARE(r.nights(), 4);
    QCOMPARE(r.values[Nights], 4.0);
    QCOMPARE(r.values[Usage], 7.0);
    QCOMPARE(r.values[DeviceAhi], 3.5);
    QVERIFY(std::isnan(r.values[AnalysisAhi]));       // filled in by Statistics
    QVERIFY(reliable(r));
    QVERIFY(!reliable(row(group({ period(QStringLiteral("6-9"), QDate(2026, 9, 1), 2, 7, 1) }).first())));

    // no hours: no usage and no index, never a division by zero
    Group empty = g;
    empty.hours = 0;
    empty.events = 0;
    const Row none = row(empty);
    QVERIFY(std::isnan(none.values[Usage]));
    QVERIFY(std::isnan(none.values[DeviceAhi]));
}

void SettingsComparisonTests::testBest()
{
    // lower is better for AHI; the 1-night row would win but does not count
    QList<Row> rows { rowWith(4, DeviceAhi, 3.0), rowWith(5, DeviceAhi, 5.0), rowWith(1, DeviceAhi, 1.0) };
    QCOMPARE(best(rows, DeviceAhi), QSet<int>({ 0 }));

    // higher is better for usage; equal values win together
    rows = { rowWith(4, Usage, 7.0), rowWith(3, Usage, 7.0), rowWith(6, Usage, 6.5) };
    QCOMPARE(best(rows, Usage), QSet<int>({ 0, 1 }));

    // nothing to compare against: no winner
    rows = { rowWith(4, DeviceAhi, 3.0), rowWith(2, DeviceAhi, 1.0) };
    QVERIFY(best(rows, DeviceAhi).isEmpty());

    // rows without the figure are skipped
    rows = { rowWith(4, Odi3, 6.0), rowWith(4, Odi3, std::nan("")), rowWith(5, Odi3, 9.0) };
    QCOMPARE(best(rows, Odi3), QSet<int>({ 0 }));

    // pressure and nights are not better or worse
    rows = { rowWith(4, Pressure, 9.0), rowWith(5, Pressure, 12.0) };
    QVERIFY(best(rows, Pressure).isEmpty());
    QVERIFY(best(rows, Nights).isEmpty());
}

void SettingsComparisonTests::testDateList()
{
    const QLocale locale;
    const QDate a(2026, 9, 20), c(2026, 9, 22), d(2026, 9, 25);
    const QString expected = locale.toString(a, QLocale::ShortFormat) + QStringLiteral(" – ")
                           + locale.toString(c, QLocale::ShortFormat) + QStringLiteral("; ")
                           + locale.toString(d, QLocale::ShortFormat);
    QCOMPARE(dateList({ a, a.addDays(1), c, d }), expected);
    QCOMPARE(dateList({ d }), locale.toString(d, QLocale::ShortFormat));
    QCOMPARE(dateList({}), QString());
}

namespace {

Row tableRow(const QString &pressure, int nightCount, double ahi)
{
    Group g;
    g.mode = QStringLiteral("APAP");
    g.pressure = pressure;
    g.relief = QStringLiteral("SoftPAP: 1");
    g.deviceKey = g.deviceLabel = QStringLiteral("Prisma 20A");
    g.dates = nights(QDate(2026, 9, 1), nightCount);
    g.hours = 7.0 * nightCount;
    g.events = ahi * g.hours;
    return row(g);
}

QString number(double value, int decimals) { return QLocale().toString(value, 'f', decimals); }

} // namespace

void SettingsComparisonTests::testHtmlMarksBestAndFewNights()
{
    const QList<Row> rows { tableRow(QStringLiteral("9-16"), 4, 3.0), tableRow(QStringLiteral("6-9"), 5, 5.0),
                            tableRow(QStringLiteral("7-10"), 1, 1.0) };
    const QString html = SettingsComparison::html(rows, Options());

    // the best reliable AHI is green, the 1-night row's lower AHI is not
    // bold too: Statistics shades every other row a light green of its own
    QVERIFY2(html.contains(QStringLiteral("<td align=right bgcolor='%1'><b>%2</b></td>").arg(kBestColor, number(3.0, 2))),
             qPrintable(html));
    QVERIFY(!html.contains(QStringLiteral("bgcolor='%1'><font color='%2'>%3").arg(kBestColor, kFewColor, number(1.0, 2))));

    // the 1-night row is grey and says why
    QVERIFY(html.contains(QStringLiteral("<font color='%1'>APAP · 7-10 · SoftPAP: 1</font>").arg(kFewColor)));
    QVERIFY(html.contains(QCoreApplication::translate("SettingsComparison", "%1 (few nights)").arg(1)));

    // the dates of a row are in its tooltip
    QVERIFY(html.contains(QStringLiteral("title=\"%1\"").arg(dateList(rows[0].group.dates))));

    // tooltips in double quotes, which toHtmlEscaped() escapes: the English one has an apostrophe
    const QString tip = QCoreApplication::translate("SettingsComparison",
                            "Average over the nights of each night's %1th percentile").arg(95);
    QVERIFY(html.contains(QStringLiteral("title=\"%1\"").arg(tip.toHtmlEscaped())));
}

void SettingsComparisonTests::testHtmlShowsDashWithoutData()
{
    Row r = tableRow(QStringLiteral("9-16"), 4, 3.0);
    r.values[Usage] = std::nan("");
    const QString html = SettingsComparison::html({ r }, Options());
    QVERIFY(html.contains(QStringLiteral("<td align=right>%1</td>").arg(kNoData)));     // analysis not filled in
    QVERIFY(!html.contains(QStringLiteral("nan"), Qt::CaseInsensitive));
    QVERIFY(!html.contains(QStringLiteral("inf"), Qt::CaseInsensitive));
}

void SettingsComparisonTests::testHtmlDeviceColumn()
{
    const QList<Row> rows { tableRow(QStringLiteral("9-16"), 4, 3.0) };
    const QString device = QCoreApplication::translate("SettingsComparison", "Device");
    Options options;
    QVERIFY(!SettingsComparison::html(rows, options).contains(QStringLiteral("<th align=left>%1</th>").arg(device)));
    options.showDevice = true;
    const QString html = SettingsComparison::html(rows, options);
    QVERIFY(html.contains(QStringLiteral("<th align=left>%1</th>").arg(device)));
    QVERIFY(html.contains(QStringLiteral("<td>Prisma 20A</td>")));
}

void SettingsComparisonTests::testHtmlEscapesSettings()
{
    Row r = tableRow(QStringLiteral("Min <4 & Max 7"), 4, 3.0);
    const QString html = SettingsComparison::html({ r }, Options());
    QVERIFY(html.contains(QStringLiteral("Min &lt;4 &amp; Max 7")));
    QVERIFY(!html.contains(QStringLiteral("Min <4")));
}

void SettingsComparisonTests::testSettingsLabel()
{
    QCOMPARE(settingsLabel(QStringLiteral("APAP"), QStringLiteral("Min 7 Max 10"), QStringLiteral(" SoftPAP: 1 ")),
             QStringLiteral("APAP · Min 7 Max 10 · SoftPAP: 1"));
    QCOMPARE(settingsLabel(QStringLiteral("CPAP"), QString(), QStringLiteral("  ")), QStringLiteral("CPAP"));
}
