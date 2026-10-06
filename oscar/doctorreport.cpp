/* Doctor Report
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "doctorreport.h"

#include <QCoreApplication>
#include <QLocale>
#include <QPainter>
#include <QPen>
#include <cmath>
#include <QDateTime>
#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QTextDocument>
#include <QUrl>

#include "analysispanel.h"
#include "htmlpages.h"
#include "version.h"


namespace {

bool known(double value) { return !std::isnan(value); }

// A tile: a coloured stripe, the caption, the figure with its sign and a short note.
QString tile(const QString &caption, const QString &value, const QString &note, NightSummary::Level level)
{
    const QString color = NightSummaryView::levelColor(level).name();
    QString sign;
    if (level == NightSummary::Good) sign = QStringLiteral(" <font color='%1'><b>✓</b></font>").arg(color);
    if (level == NightSummary::Attention) sign = QStringLiteral(" <font color='%1'><b>!</b></font>").arg(color);
    return QStringLiteral("<table width='100%' cellspacing=0 cellpadding=3><tr><td width=4 bgcolor='%1'></td>"
                          "<td><font color='#606060'>%2</font><br><font size='+2'><b>%3</b></font>%4"
                          "<br><font size='-1' color='#606060'>%5</font></td></tr></table>")
        .arg(color, caption.toHtmlEscaped(), value.toHtmlEscaped(), sign, note.toHtmlEscaped());
}

NightSummary::Level below(double value, double target)
{
    if (!known(value)) return NightSummary::Unknown;
    return value < target ? NightSummary::Good : NightSummary::Attention;
}

} // namespace

NightSummary::Level DoctorReport::usageLevel() const
{
    if (!known(meanHours)) return NightSummary::Unknown;
    return meanHours >= complianceHours ? NightSummary::Good : NightSummary::Attention;
}

NightSummary::Level DoctorReport::ahiLevel() const { return below(deviceAhi, NightSummary::kAhiTarget); }

NightSummary::Level DoctorReport::leakLevel() const
{
    if (leakRedline <= 0) return NightSummary::Unknown;
    return below(leak, leakRedline);
}

NightSummary::Level DoctorReport::odiLevel() const { return below(odi3, NightSummary::kOdiTarget); }
NightSummary::Level DoctorReport::below90Level() const { return below(below90, NightSummary::kT90Target); }

namespace DoctorReportPage {

ChartLayout chartLayout(const QSize &size, int nights)
{
    // laid out for a chart 320 px high and scaled from there
    const double s = size.height() / 320.0;
    const int left = qRound(64 * s), right = qRound(8 * s), top = qRound(10 * s);
    const int bottom = qRound(30 * s), gap = qRound(16 * s);
    const int width = qMax(1, size.width() - left - right);
    const int plot = qMax(2, size.height() - top - bottom - gap);
    const int ahiHeight = plot * 55 / 100;

    ChartLayout l;
    l.ahi = QRect(left, top, width, ahiHeight);
    l.hours = QRect(left, top + ahiHeight + gap, width, plot - ahiHeight);
    l.left = left;
    l.column = nights > 0 ? double(width) / nights : 0;
    return l;
}

QColor hoursColor() { return QColor(0x3a, 0x78, 0xc3); }
QColor changeColor() { return QColor(0x90, 0x90, 0x90); }

QImage chart(const DoctorReport &r, const QSize &size)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::white);
    const int n = r.nightList.size();
    if (n == 0 || size.isEmpty()) return image;

    const ChartLayout l = chartLayout(size, n);
    const double s = size.height() / 320.0;

    double maxAhi = NightSummary::kAhiTarget * 2;
    double maxHours = qMax(r.complianceHours * 2, 8.0);
    for (const DoctorReport::Night &night : r.nightList) {
        if (known(night.ahi)) maxAhi = qMax(maxAhi, night.ahi * 1.1);
        if (known(night.hours)) maxHours = qMax(maxHours, night.hours * 1.1);
    }

    QPainter p(&image);
    QFont font = p.font();
    font.setPixelSize(qMax(8, qRound(13 * s)));
    p.setFont(font);

    auto height = [](const QRect &area, double value, double max) {
        return qMin(1.0, value / max) * (area.height() - 1);
    };
    auto bar = [&](const QRect &area, int i, double value, double max) {
        const double w = qMax(1.0, l.column * 0.7);
        const double h = height(area, value, max);
        return QRectF(l.left + i * l.column + (l.column - w) / 2, area.top() + area.height() - h, w, h);
    };

    // frames
    p.setPen(QColor(0xd0, 0xd0, 0xd0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(l.ahi.adjusted(0, 0, -1, -1));
    p.drawRect(l.hours.adjusted(0, 0, -1, -1));

    // bars
    p.setPen(Qt::NoPen);
    for (int i = 0; i < n; ++i) {
        const DoctorReport::Night &night = r.nightList[i];
        if (known(night.ahi) && night.ahi > 0) {
            p.setBrush(NightSummaryView::levelColor(night.ahi < NightSummary::kAhiTarget ? NightSummary::Good
                                                                                        : NightSummary::Attention));
            p.drawRect(bar(l.ahi, i, night.ahi, maxAhi));
        }
        if (known(night.hours) && night.hours > 0) {
            p.setBrush(night.hours >= r.complianceHours ? hoursColor()
                                                        : NightSummaryView::levelColor(NightSummary::Attention));
            p.drawRect(bar(l.hours, i, night.hours, maxHours));
        }
    }

    // targets
    QPen target(QColor(0x60, 0x60, 0x60));
    target.setStyle(Qt::DashLine);
    target.setWidthF(qMax(1.0, s));
    p.setPen(target);
    const double ahiY = l.ahi.top() + l.ahi.height() - height(l.ahi, NightSummary::kAhiTarget, maxAhi);
    const double hoursY = l.hours.top() + l.hours.height() - height(l.hours, r.complianceHours, maxHours);
    p.drawLine(QPointF(l.ahi.left(), ahiY), QPointF(l.ahi.right(), ahiY));
    p.drawLine(QPointF(l.hours.left(), hoursY), QPointF(l.hours.right(), hoursY));

    // settings changes, on the left edge of the night they started
    QPen change(changeColor());
    change.setStyle(Qt::DashLine);
    change.setWidthF(qMax(1.0, 1.5 * s));
    p.setPen(change);
    for (const QDate &date : r.settingsChanges) {
        const qint64 i = r.from.daysTo(date);
        if (i <= 0 || i >= n) continue;
        const double x = l.left + i * l.column;
        p.drawLine(QPointF(x, l.ahi.top()), QPointF(x, l.hours.bottom()));
    }

    // scales and dates
    p.setPen(QColor(0x40, 0x40, 0x40));
    const QLocale locale;
    const int labelRight = l.left - qRound(6 * s);
    const QRect ahiLabels(0, l.ahi.top(), labelRight, l.ahi.height());
    const QRect hoursLabels(0, l.hours.top(), labelRight, l.hours.height());
    p.drawText(ahiLabels, Qt::AlignRight | Qt::AlignTop, locale.toString(maxAhi, 'f', 0));
    p.drawText(ahiLabels, Qt::AlignRight | Qt::AlignVCenter, r.ahiName);
    p.drawText(ahiLabels, Qt::AlignRight | Qt::AlignBottom, QStringLiteral("0"));
    p.drawText(hoursLabels, Qt::AlignRight | Qt::AlignTop, locale.toString(maxHours, 'f', 0));
    p.drawText(hoursLabels, Qt::AlignRight | Qt::AlignVCenter, DoctorReport::tr("h"));
    p.drawText(hoursLabels, Qt::AlignRight | Qt::AlignBottom, QStringLiteral("0"));
    const int every = qMax(1, int(std::ceil(70 * s / l.column)));
    for (int i = 0; i < n; i += every) {
        const double x = l.left + (i + 0.5) * l.column;
        p.drawText(QRectF(x - 40 * s, l.hours.bottom() + 4 * s, 80 * s, 22 * s), Qt::AlignHCenter | Qt::AlignTop,
                   r.nightList[i].date.toString(QStringLiteral("dd.MM")));
    }
    return image;
}

QString html(const DoctorReport &r, const QString &chartUrl, const QSizeF &chartSize)
{
    const QLocale locale;
    const QString none = SettingsComparison::kNoData;
    auto number = [&](double value, int decimals) { return known(value) ? locale.toString(value, 'f', decimals) : none; };
    auto withUnits = [&](double value, int decimals, const QString &units) {
        return known(value) ? QStringLiteral("%1 %2").arg(locale.toString(value, 'f', decimals), units).trimmed() : none;
    };
    auto date = [&](const QDate &d) { return locale.toString(d, QLocale::ShortFormat); };

    QString html = QStringLiteral("<p><font size='+3'><b>%1</b></font></p>").arg(DoctorReport::tr("CPAP Therapy Report").toHtmlEscaped());

    // who, on what, when
    html += QStringLiteral("<table cellspacing=0 cellpadding=1>");
    auto line = [&](const QString &label, const QString &text) {
        html += QStringLiteral("<tr><td><font color='#606060'>%1</font>&nbsp;&nbsp;</td><td>%2</td></tr>")
                    .arg(label.toHtmlEscaped(), text.toHtmlEscaped());
    };
    if (!r.patient.isEmpty()) {
        line(DoctorReport::tr("Patient:"), r.birthDate.isValid() ? DoctorReport::tr("%1, born %2").arg(r.patient, date(r.birthDate)) : r.patient);
    }
    if (!r.cpapDevices.isEmpty()) line(DoctorReport::tr("Device:"), r.cpapDevices.join(QStringLiteral(", ")));
    if (!r.oximeter.isEmpty()) line(DoctorReport::tr("Oximeter:"), r.oximeter);
    line(DoctorReport::tr("Period:"), DoctorReport::tr("%1 – %2 · nights with data %3 of %4 · with an oximeter %5")
                            .arg(date(r.from), date(r.to)).arg(r.nights).arg(r.days()).arg(r.oximetryNights));
    html += QStringLiteral("</table>");
    if (!r.currentSettings.isEmpty()) {
        html += QStringLiteral("<table width='100%' border=1 cellspacing=0 cellpadding=4><tr><td><b>%1</b> %2</td></tr></table>")
                    .arg(DoctorReport::tr("Current settings:").toHtmlEscaped(),
                         DoctorReport::tr("%1 — since %2").arg(r.currentSettings, date(r.settingsSince)).toHtmlEscaped());
    }

    // the six tiles
    QString usage = none;
    if (known(r.meanHours)) {
        const int minutes = qRound(r.meanHours * 60);
        usage = DoctorReport::tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
    }
    const QStringList tiles {
        tile(DoctorReport::tr("Usage"), usage,
             DoctorReport::tr("%1 of %2 nights ≥ %3 h").arg(r.compliantNights).arg(r.days()).arg(locale.toString(r.complianceHours)),
             r.usageLevel()),
        tile(r.ahiName, number(r.deviceAhi, 1),
             (r.correctedNights > 0
                  ? DoctorReport::tr("corrected by hand on %n night(s), by the device %1", nullptr, r.correctedNights)
                            .arg(number(r.deviceAhiUncorrected, 1)) + QStringLiteral(" · ")
                  : QString())
                 + DoctorReport::tr("OSCAR's analysis: %1 · flow limitation %2% (%3 min per night, %4% of breaths) · Glasgow Index %5 / %6 (adapted)")
                 .arg(number(r.analysisAhi, 1), number(r.flowLimitation, 0), number(r.flowLimitationMinutes, 0),
                      number(r.flowLimitedBreaths, 0), number(r.glasgow, 1), number(r.glasgowAdapted, 1)),
             r.ahiLevel()),
        tile(DoctorReport::tr("Leak"), withUnits(r.leak, 1, r.leakUnits),
             r.leakRedline > 0 ? DoctorReport::tr("red line %1").arg(locale.toString(r.leakRedline)) : DoctorReport::tr("no red line set"),
             r.leakLevel()),
        tile(DoctorReport::tr("Pressure %1%").arg(locale.toString(r.percentile)), withUnits(r.pressure, 1, r.pressureUnits),
             DoctorReport::tr("average over the nights"), NightSummary::Unknown),
        tile(DoctorReport::tr("ODI 3%"), known(r.odi3) ? DoctorReport::tr("%1 an hour").arg(number(r.odi3, 1)) : none,
             DoctorReport::tr("nights with an oximeter: %1").arg(r.oximetryNights), r.odiLevel()),
        tile(DoctorReport::tr("SpO2 below 90%"), known(r.below90) ? QStringLiteral("%1 %").arg(number(r.below90, 1)) : none,
             DoctorReport::tr("nights with an oximeter: %1").arg(r.oximetryNights), r.below90Level()),
    };
    html += QStringLiteral("<p><b>%1</b></p><table width='100%' cellspacing=4 cellpadding=0><tr>")
                .arg(DoctorReport::tr("Summary for the period").toHtmlEscaped());
    for (int i = 0; i < tiles.size(); ++i) {
        if (i == 3) html += QStringLiteral("</tr><tr>");
        html += QStringLiteral("<td width='33%'>%1</td>").arg(tiles[i]);
    }
    html += QStringLiteral("</tr></table>");
    if (r.analysisMissing > 0) {
        html += QStringLiteral("<p><font size='-1' color='#606060'>%1</font></p>")
                    .arg(DoctorReport::tr("OSCAR's analysis is missing or out of date for %1 of %2 nights, so its figures are left out.")
                             .arg(r.analysisMissing).arg(r.nights).toHtmlEscaped());
    }

    // night by night
    html += QStringLiteral("<p><b>%1</b></p><img src='%2' width=%3 height=%4><br><font size='-1' color='#606060'>%5</font>")
                .arg(DoctorReport::tr("Night by night").toHtmlEscaped(), chartUrl)
                .arg(qRound(chartSize.width())).arg(qRound(chartSize.height()))
                .arg(DoctorReport::tr("AHI per night (dashed: %1) · hours of use (dashed: %2 h) · grey lines: settings changed")
                         .arg(locale.toString(NightSummary::kAhiTarget), locale.toString(r.complianceHours)).toHtmlEscaped());

    // the settings over the period
    if (!r.comparison.isEmpty()) {
        SettingsComparison::Options options;
        options.showDevice = r.showDevice;
        options.ahiName = r.ahiName;
        options.percentile = r.percentile;
        options.rowColors = { QStringLiteral("#ffffff"), QStringLiteral("#f2f2f2") };
        options.settingsWidth = QStringLiteral("30%");   // otherwise the narrow page wraps it line by line
        // a size smaller: the Russian headers and settings labels would push the page to two
        html += QStringLiteral("<font size='-1'>%1</font>").arg(SettingsComparison::html(r.comparison, options));
    }

    html += QStringLiteral("<p><font size='-1' color='#606060'><i>%1</i><br>%2</font></p>")
                .arg(AnalysisPanel::disclaimer().toHtmlEscaped(),
                     DoctorReport::tr("Prepared by OSCAR %1 on %2")
                         .arg(getVersion().displayString(), locale.toString(QDateTime::currentDateTime(), QLocale::ShortFormat))
                         .toHtmlEscaped());
    return html;
}

bool writePdf(const DoctorReport &report, const QString &path, QString *error)
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Portrait);
    printer.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);

    QPainter painter;
    if (!painter.begin(&printer)) {
        if (error) *error = DoctorReport::tr("Could not write %1.").arg(path);
        return false;
    }
    paintPage(painter, printer, report, false);
    painter.end();

    const QFileInfo written(path);
    if (printer.printerState() == QPrinter::Error || !written.exists() || written.size() == 0) {
        if (error) *error = DoctorReport::tr("Could not write %1.").arg(path);
        return false;
    }
    return true;
}

int paintPage(QPainter &painter, QPrinter &printer, const DoctorReport &report, bool startOnNewPage)
{
    QFont font(QStringLiteral("Helvetica"));
    font.setPointSizeF(8.5);
    const QSizeF page = htmlPageSize(printer);
    const QSize chartPixels(2400, 720);
    const QString url = QStringLiteral("doctorreport-chart.png");
    const QSizeF chartSize(page.width(), page.width() * chartPixels.height() / chartPixels.width());
    return paintHtmlPages(painter, printer, html(report, url, chartSize), font,
                          { { url, chart(report, chartPixels) } }, startOnNewPage);
}

} // namespace DoctorReportPage
