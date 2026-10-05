/* Doctor Report Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "doctorreporttests.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTranslator>
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

QString reportText(const char *source) { return QCoreApplication::translate("DoctorReport", source); }

// A full report: personal data, oximetry, analysis, current settings and a comparison.
DoctorReport fullReport()
{
    DoctorReport r = sampleReport();
    r.patient = QStringLiteral("Иван Петров");
    r.birthDate = QDate(1954, 1, 1);
    r.cpapDevices << QStringLiteral("Löwenstein Prisma 20A");
    r.oximeter = QStringLiteral("Contec CMS50FW");
    r.oximetryNights = 3;
    r.currentSettings = QStringLiteral("APAP · Min 7 Max 10 · SoftPAP: 1");
    r.settingsSince = QDate(2026, 8, 20);
    r.meanHours = 6.1;
    r.compliantNights = 3;
    r.deviceAhi = 4.3;
    r.analysisAhi = 5.2;
    r.flowLimitation = 11;
    r.leak = 3.1;
    r.leakUnits = QStringLiteral("L/min");
    r.leakRedline = 24;
    r.pressure = 9.4;
    r.pressureUnits = QStringLiteral("cmH2O");
    r.odi3 = 7.5;
    r.below90 = 0.4;
    SettingsComparison::Group g;
    g.mode = QStringLiteral("APAP");
    g.pressure = QStringLiteral("Min 7 Max 10");
    g.dates << QDate(2026, 9, 4) << QDate(2026, 9, 5);
    g.hours = 14;
    g.events = 42;
    r.comparison << SettingsComparison::row(g);
    return r;
}

const QSizeF kChart(500, 150);

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

void DoctorReportTests::testHtmlHeader()
{
    const DoctorReport r = fullReport();
    const QLocale locale;
    const QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QVERIFY(html.contains(reportText("CPAP Therapy Report")));
    QVERIFY(html.contains(reportText("%1, born %2").arg(r.patient, locale.toString(r.birthDate, QLocale::ShortFormat))));
    QVERIFY(html.contains(QStringLiteral("Löwenstein Prisma 20A")));
    QVERIFY(html.contains(QStringLiteral("Contec CMS50FW")));
    QVERIFY(html.contains(reportText("%1 — since %2").arg(r.currentSettings, locale.toString(r.settingsSince, QLocale::ShortFormat))
                              .toHtmlEscaped()));
    QVERIFY(html.contains(reportText("%1 – %2 · nights with data %3 of %4 · with an oximeter %5")
                              .arg(locale.toString(r.from, QLocale::ShortFormat), locale.toString(r.to, QLocale::ShortFormat))
                              .arg(4).arg(5).arg(3)));
    QVERIFY(html.contains(QStringLiteral("<img src='chart.png' width=500 height=150>")));
    QVERIFY(html.contains(QCoreApplication::translate("SettingsComparison", "Device Settings Compared")));
}

void DoctorReportTests::testHtmlWithoutPersonalData()
{
    DoctorReport r = fullReport();
    r.patient.clear();
    r.birthDate = QDate();
    const QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QVERIFY(!html.contains(reportText("Patient:")));
    QVERIFY(!html.contains(QLocale().toString(QDate(1954, 1, 1), QLocale::ShortFormat)));
}

void DoctorReportTests::testHtmlWithoutOximetry()
{
    DoctorReport r = fullReport();
    r.oximeter.clear();
    r.oximetryNights = 0;
    r.odi3 = DoctorReport::kNoValue;
    r.below90 = DoctorReport::kNoValue;
    const QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QVERIFY(!html.contains(reportText("Oximeter:")));
    QVERIFY(html.contains(QStringLiteral("<b>%1</b>").arg(SettingsComparison::kNoData)));
}

void DoctorReportTests::testHtmlWithoutAnalysis()
{
    DoctorReport r = fullReport();
    r.analysisAhi = DoctorReport::kNoValue;
    r.flowLimitation = DoctorReport::kNoValue;
    r.odi3 = DoctorReport::kNoValue;
    r.below90 = DoctorReport::kNoValue;
    const QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QVERIFY(html.contains(reportText("OSCAR's analysis: %1 · flow limitation %2%")
                              .arg(SettingsComparison::kNoData, SettingsComparison::kNoData).toHtmlEscaped()));
    QVERIFY(!html.contains(QStringLiteral("nan"), Qt::CaseInsensitive));
}

void DoctorReportTests::testHtmlRdi()
{
    DoctorReport r = fullReport();
    r.ahiName = QStringLiteral("RDI");
    const QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QVERIFY(html.contains(QStringLiteral("<font color='#606060'>RDI</font>")));                  // the tile
    QVERIFY(html.contains(QCoreApplication::translate("SettingsComparison", "Device %1").arg(QStringLiteral("RDI"))));
}

void DoctorReportTests::testHtmlSigns()
{
    DoctorReport r = fullReport();   // usage, AHI, leak and SpO2 fine; ODI 7.5 is not
    QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QCOMPARE(html.count(QStringLiteral("<b>✓</b>")), 4);
    QCOMPARE(html.count(QStringLiteral("<b>!</b>")), 1);

    r.odi3 = 2;
    html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QCOMPARE(html.count(QStringLiteral("<b>!</b>")), 0);
}

void DoctorReportTests::testHtmlEscapes()
{
    DoctorReport r = fullReport();
    r.currentSettings = QStringLiteral("Min <4 & Max 7");
    r.patient = QStringLiteral("A <b>B</b>");
    const QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QVERIFY(html.contains(QStringLiteral("Min &lt;4 &amp; Max 7")));
    QVERIFY(html.contains(QStringLiteral("A &lt;b&gt;B&lt;/b&gt;")));
    QVERIFY(!html.contains(QStringLiteral("Min <4")));
}

void DoctorReportTests::testWritePdf()
{
    // 12 nights and 11 settings rows: still one page
    DoctorReport r = fullReport();
    r.to = r.from.addDays(11);
    r.nightList.clear();
    for (int i = 0; i < 12; ++i) {
        DoctorReport::Night n;
        n.date = r.from.addDays(i);
        n.hours = 6 + (i % 3);
        n.ahi = 2 + i % 5;
        r.nightList << n;
    }
    r.comparison.clear();
    for (int i = 0; i < 11; ++i) {
        SettingsComparison::Group g;
        g.mode = QStringLiteral("APAP (dyn)");
        g.pressure = QStringLiteral("Min %1 Max %2 (cmH2O)").arg(4 + i).arg(10 + i);
        g.relief = QStringLiteral("SoftPAP: 1 - Slight");
        g.dates << r.from.addDays(i);
        g.hours = 7;
        g.events = 20;
        r.comparison << SettingsComparison::row(g);
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("report.pdf"));
    QString error;
    QVERIFY2(DoctorReportPage::writePdf(r, path, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray pdf = file.readAll();
    QVERIFY(pdf.startsWith("%PDF"));
    const int pages = int(QString::fromLatin1(pdf).count(QRegularExpression(QStringLiteral("/Type\\s*/Page[^s]"))));
    QCOMPARE(pages, 1);
}

void DoctorReportTests::testWritePdfFailsOnBadPath()
{
    QString error;
    QVERIFY(!DoctorReportPage::writePdf(fullReport(), QStringLiteral("/nonexistent-folder-oscar/report.pdf"), &error));
    QVERIFY(!error.isEmpty());
}

void DoctorReportTests::testHtmlAnalysisMissing()
{
    DoctorReport r = fullReport();
    const QString note = reportText("OSCAR's analysis is missing or out of date for %1 of %2 nights, so its figures are left out.");
    QVERIFY(!DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart).contains(note.arg(2).arg(4)));
    r.analysisMissing = 2;
    QVERIFY(DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart).contains(note.arg(2).arg(4)));
}

void DoctorReportTests::testWritePdfRussian()
{
    // The Russian page has longer headers and settings labels: still one page for a month
    // like the father's (18 nights of 30, 11 settings rows, the footer included).
    QTranslator russian;
    const QString qm = QFileInfo(QStringLiteral(__FILE__)).absolutePath() + QStringLiteral("/../translations/Russkiy.ru.qm");
    if (!russian.load(qm)) QSKIP("Russkiy.ru.qm not built");
    QCoreApplication::installTranslator(&russian);

    DoctorReport r = fullReport();
    r.from = QDate(2026, 9, 3);
    r.to = QDate(2026, 10, 2);
    r.nights = 18;
    r.analysisMissing = 0;
    r.nightList.clear();
    for (int i = 0; i < 30; ++i) {
        DoctorReport::Night n;
        n.date = r.from.addDays(i);
        if (i >= 12) {
            n.hours = 6 + (i % 4);
            n.ahi = 3 + i % 9;
        }
        r.nightList << n;
    }
    r.currentSettings = QStringLiteral("APAP (дин) · Мин 7.5 Макс 14.0 (см H2O) · SoftPAP: 1 - Слабый");
    r.comparison.clear();
    for (int i = 0; i < 11; ++i) {
        SettingsComparison::Group g;
        g.mode = QStringLiteral("APAP (дин)");
        g.pressure = QStringLiteral("Мин %1.0 Макс %2.0 (см H2O)").arg(4 + i).arg(10 + i);
        g.relief = i % 3 ? QStringLiteral("SoftPAP: 1 - Слабый") : QStringLiteral("SoftPAP: 2 - Стандартный");
        for (int d = 0; d <= i % 4; ++d) g.dates << r.from.addDays(12 + i + d);
        g.hours = 8.0 * g.dates.size();
        g.events = 6.0 * g.hours;
        SettingsComparison::Row row = SettingsComparison::row(g);
        for (SettingsComparison::Column c : { SettingsComparison::AnalysisAhi, SettingsComparison::Leak, SettingsComparison::Pressure,
                                              SettingsComparison::FlowLimitation, SettingsComparison::Odi3, SettingsComparison::Below90 })
            row.values[c] = 10.5 + i;
        r.comparison << row;
    }

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("report.pdf"));
    QString error;
    const bool written = DoctorReportPage::writePdf(r, path, &error);
    QCoreApplication::removeTranslator(&russian);
    QVERIFY2(written, qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const int pages = int(QString::fromLatin1(file.readAll()).count(QRegularExpression(QStringLiteral("/Type\\s*/Page[^s]"))));
    QCOMPARE(pages, 1);
}
