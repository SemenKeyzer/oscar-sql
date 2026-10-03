/* Doctor Report Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "doctorreporttests.h"

#include <QApplication>
#include <cmath>

#include "doctorreport.h"

using namespace DoctorReportPage;

namespace {

// Five nights from 01.09.2026: the second without data; settings changed on the fourth.
DoctorReport sampleReport()
{
    DoctorReport r;
    r.from = QDate(2026, 9, 1);
    r.to = QDate(2026, 9, 5);
    const double hours[] = { 7.5, DoctorReport::kNoValue, 3, 6, 8 };
    const double ahi[] = { 3, DoctorReport::kNoValue, 8, 2, 4 };
    for (int i = 0; i < 5; ++i) {
        DoctorReport::Night n;
        n.date = r.from.addDays(i);
        n.hours = hours[i];
        n.ahi = ahi[i];
        r.nightList << n;
    }
    r.nights = 4;
    r.settingsChanges << QDate(2026, 9, 4);
    return r;
}

QColor pixel(const QImage &image, double x, int y) { return QColor(image.pixel(int(x), y)); }

} // namespace

void DoctorReportTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
}

void DoctorReportTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

void DoctorReportTests::testLevels()
{
    DoctorReport r;
    QCOMPARE(r.usageLevel(), NightSummary::Unknown);
    QCOMPARE(r.ahiLevel(), NightSummary::Unknown);
    QCOMPARE(r.leakLevel(), NightSummary::Unknown);
    QCOMPARE(r.odiLevel(), NightSummary::Unknown);
    QCOMPARE(r.below90Level(), NightSummary::Unknown);

    r.complianceHours = 4;
    r.meanHours = 6;
    QCOMPARE(r.usageLevel(), NightSummary::Good);
    r.meanHours = 3.9;
    QCOMPARE(r.usageLevel(), NightSummary::Attention);

    r.deviceAhi = 4.9;
    QCOMPARE(r.ahiLevel(), NightSummary::Good);
    r.deviceAhi = 5;
    QCOMPARE(r.ahiLevel(), NightSummary::Attention);

    r.leak = 10;
    QCOMPARE(r.leakLevel(), NightSummary::Unknown);     // no red line set
    r.leakRedline = 24;
    QCOMPARE(r.leakLevel(), NightSummary::Good);
    r.leak = 30;
    QCOMPARE(r.leakLevel(), NightSummary::Attention);

    r.odi3 = 4;
    QCOMPARE(r.odiLevel(), NightSummary::Good);
    r.odi3 = 6;
    QCOMPARE(r.odiLevel(), NightSummary::Attention);

    r.below90 = 4.9;
    QCOMPARE(r.below90Level(), NightSummary::Good);
    r.below90 = 5;
    QCOMPARE(r.below90Level(), NightSummary::Attention);
}

void DoctorReportTests::testChartLayout()
{
    const ChartLayout l = chartLayout(QSize(800, 320), 5);
    QCOMPARE(l.left, l.ahi.left());
    QCOMPARE(l.hours.left(), l.ahi.left());
    QCOMPARE(l.column, l.ahi.width() / 5.0);
    QVERIFY(l.hours.top() > l.ahi.bottom());
    QVERIFY(l.hours.bottom() < 320);
    QVERIFY(l.ahi.height() > l.hours.height());
}

void DoctorReportTests::testChart()
{
    const DoctorReport r = sampleReport();
    const QSize size(800, 320);
    const QImage image = chart(r, size);
    QCOMPARE(image.size(), size);

    const ChartLayout l = chartLayout(size, 5);
    auto centre = [&](int night) { return l.left + (night + 0.5) * l.column; };
    const int ahiBottom = l.ahi.top() + l.ahi.height() - 3;
    const int hoursBottom = l.hours.top() + l.hours.height() - 3;

    // AHI under the target is green, above it orange; the night without data stays empty
    QCOMPARE(pixel(image, centre(0), ahiBottom), NightSummaryView::levelColor(NightSummary::Good));
    QCOMPARE(pixel(image, centre(2), ahiBottom), NightSummaryView::levelColor(NightSummary::Attention));
    QCOMPARE(pixel(image, centre(1), ahiBottom), QColor(Qt::white));
    QCOMPARE(pixel(image, centre(1), hoursBottom), QColor(Qt::white));

    // hours at or above the compliance hours in blue, below in orange
    QCOMPARE(pixel(image, centre(0), hoursBottom), hoursColor());
    QCOMPARE(pixel(image, centre(2), hoursBottom), NightSummaryView::levelColor(NightSummary::Attention));

    // a grey line where the settings changed, on the left edge of the fourth night
    const double x = l.left + 3 * l.column;
    bool line = false;
    for (int dx = -1; dx <= 1 && !line; ++dx) {
        for (int y = l.ahi.top(); y <= l.hours.bottom() && !line; ++y) {
            line = pixel(image, x + dx, y) == changeColor();
        }
    }
    QVERIFY(line);

    // nothing to draw: a blank image
    const QImage empty = chart(DoctorReport(), size);
    QCOMPARE(empty.size(), size);
    QCOMPARE(QColor(empty.pixel(400, 160)), QColor(Qt::white));
}

void DoctorReportTests::testChartLongPeriod()
{
    // a year still gets a bar for every night
    DoctorReport r;
    r.from = QDate(2025, 10, 1);
    r.to = r.from.addDays(364);
    for (int i = 0; i < 365; ++i) {
        DoctorReport::Night n;
        n.date = r.from.addDays(i);
        n.hours = 7;
        n.ahi = 2;
        r.nightList << n;
    }
    const QSize size(2400, 720);
    const QImage image = chart(r, size);
    const ChartLayout l = chartLayout(size, 365);
    QVERIFY(l.column > 1);
    for (int night : { 0, 100, 364 }) {
        const double x = l.left + (night + 0.5) * l.column;
        QCOMPARE(pixel(image, x, l.ahi.top() + l.ahi.height() - 3), NightSummaryView::levelColor(NightSummary::Good));
    }
}
