/* PDF Report Options
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "pdfreportoptions.h"

#include <algorithm>
#include <cmath>

void PdfReportOptions::apply(Preset preset)
{
    summary = true;
    daily = overview = statistics = preset != Brief;
    switch (preset) {
    case Brief:
        break;
    case Detailed:
        nights = Last7Nights;
        overviewPreset = OverviewPresets::Therapy;
        statsSettings = statsOximetry = true;
        statsDevices = false;
        break;
    case Everything:
        nights = AllNights;
        overviewPreset = OverviewPresets::All;
        statsSettings = statsOximetry = statsDevices = true;
        break;
    }
}

QPair<QDate, QDate> PdfReportOptions::range(const QDate &lastNight) const
{
    switch (period) {
    case Last7: return { lastNight.addDays(-6), lastNight };
    case Last30: return { lastNight.addDays(-29), lastNight };
    case Last90: return { lastNight.addDays(-89), lastNight };
    case Custom: break;
    }
    return { from, to };
}

QList<QDate> PdfReportOptions::nightsToPrint(const QList<QDate> &cpapNights) const
{
    // the range of a Custom period needs no last night; the others take the latest given
    QDate latest;
    for (const QDate &d : cpapNights) latest = std::max(latest, d);
    const QPair<QDate, QDate> r = range(latest);

    QList<QDate> inside;
    for (const QDate &d : cpapNights) {
        if (d >= r.first && d <= r.second) inside << d;
    }
    std::sort(inside.begin(), inside.end());
    int keep = int(inside.size());
    if (nights == LastNight) keep = 1;
    else if (nights == Last3) keep = 3;
    else if (nights == Last7Nights) keep = 7;
    return inside.mid(std::max(0, int(inside.size()) - keep));
}

QVariantMap PdfReportOptions::toMap() const
{
    return {
        { QStringLiteral("period"), int(period) },
        { QStringLiteral("from"), from },
        { QStringLiteral("to"), to },
        { QStringLiteral("summary"), summary },
        { QStringLiteral("daily"), daily },
        { QStringLiteral("nights"), int(nights) },
        { QStringLiteral("overview"), overview },
        { QStringLiteral("overviewPreset"), OverviewPresets::key(overviewPreset) },
        { QStringLiteral("statistics"), statistics },
        { QStringLiteral("statsSettings"), statsSettings },
        { QStringLiteral("statsOximetry"), statsOximetry },
        { QStringLiteral("statsDevices"), statsDevices },
        { QStringLiteral("personalData"), personalData },
        { QStringLiteral("serialNumbers"), serialNumbers },
    };
}

QPair<QDate, QDate> PdfReportOptions::clamped(const QPair<QDate, QDate> &range, const QDate &first, const QDate &last)
{
    QPair<QDate, QDate> r = range;
    if (first.isValid() && r.first < first) r.first = first;
    if (last.isValid() && r.second > last) r.second = last;
    return r;
}

PdfReportOptions PdfReportOptions::fromMap(const QVariantMap &map)
{
    PdfReportOptions o;
    auto flag = [&map](const char *key, bool &value) {
        const QString k = QString::fromLatin1(key);
        if (map.contains(k)) value = map.value(k).toBool();
    };
    const int period = map.value(QStringLiteral("period"), int(o.period)).toInt();
    if (period >= Last7 && period <= Custom) o.period = PeriodKind(period);
    const int nights = map.value(QStringLiteral("nights"), int(o.nights)).toInt();
    if (nights >= LastNight && nights <= AllNights) o.nights = NightsKind(nights);
    o.from = map.value(QStringLiteral("from")).toDate();
    o.to = map.value(QStringLiteral("to")).toDate();
    if (map.contains(QStringLiteral("overviewPreset")))
        o.overviewPreset = OverviewPresets::fromKey(map.value(QStringLiteral("overviewPreset")).toString());
    flag("summary", o.summary);
    flag("daily", o.daily);
    flag("overview", o.overview);
    flag("statistics", o.statistics);
    flag("statsSettings", o.statsSettings);
    flag("statsOximetry", o.statsOximetry);
    flag("statsDevices", o.statsDevices);
    flag("personalData", o.personalData);
    flag("serialNumbers", o.serialNumbers);
    return o;
}

int estimatePages(const PdfReportOptions &o, int nights, int dailyGraphs, int overviewGraphs, int statisticsPages)
{
    int pages = o.summary ? 1 : 0;
    // a night's header takes about one graph's room on its first page, which holds six
    if (o.daily) pages += nights * (1 + std::max(0, int(std::ceil((dailyGraphs - 5) / 6.0))));
    if (o.overview) pages += std::max(1, int(std::ceil(overviewGraphs / 6.0)));
    if (o.statistics) pages += statisticsPages;
    return pages;
}
