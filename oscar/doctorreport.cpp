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

namespace {

bool known(double value) { return !std::isnan(value); }

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

} // namespace DoctorReportPage
