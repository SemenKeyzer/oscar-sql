/* Device Settings Comparison
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "settingscomparison.h"

#include <QHash>
#include <QLocale>
#include <algorithm>
#include <cmath>

namespace SettingsComparison {

namespace {

QDate lastDate(const Group &g) { return g.dates.isEmpty() ? QDate() : g.dates.last(); }

// Values compare as they are shown, so 7.00 and 7.004 win together.
qint64 shown(double value, Column column) { return qRound64(value * std::pow(10.0, decimals(column))); }

} // namespace

QList<Group> group(const QList<Period> &periods)
{
    QList<Group> groups;
    QHash<QString, int> index;
    for (const Period &p : periods) {
        const QString key = QStringList { p.mode, p.pressure, p.relief, p.deviceKey }.join(QChar(0x1f));
        auto it = index.find(key);
        if (it == index.end()) {
            Group g;
            g.mode = p.mode;
            g.pressure = p.pressure;
            g.relief = p.relief;
            g.deviceKey = p.deviceKey;
            g.deviceLabel = p.deviceLabel;
            it = index.insert(key, groups.size());
            groups << g;
        }
        Group &g = groups[it.value()];
        g.dates << p.dates;
        g.hours += p.hours;
        g.events += p.events;
    }
    for (Group &g : groups) {
        std::sort(g.dates.begin(), g.dates.end());
        g.dates.erase(std::unique(g.dates.begin(), g.dates.end()), g.dates.end());
    }
    std::stable_sort(groups.begin(), groups.end(),
                     [](const Group &a, const Group &b) { return lastDate(a) > lastDate(b); });
    return groups;
}

Row row(const Group &group)
{
    Row r;
    r.group = group;
    const int nights = group.dates.size();
    r.values[Nights] = nights;
    if (nights > 0 && group.hours > 0) r.values[Usage] = group.hours / nights;
    if (group.hours > 0) r.values[DeviceAhi] = group.events / group.hours;
    return r;
}

bool reliable(const Row &row)
{
    return row.nights() >= kMinNights;
}

QSet<int> best(const QList<Row> &rows, Column column)
{
    if (column == Nights || column == Pressure || column == ColumnCount) return {};
    const bool higherIsBetter = (column == Usage);

    QList<int> candidates;
    for (int i = 0; i < rows.size(); ++i) {
        if (reliable(rows[i]) && !std::isnan(rows[i].values[column])) candidates << i;
    }
    if (candidates.size() < 2) return {};

    qint64 top = shown(rows[candidates.first()].values[column], column);
    for (int i : candidates) {
        const qint64 v = shown(rows[i].values[column], column);
        if (higherIsBetter ? v > top : v < top) top = v;
    }
    QSet<int> winners;
    for (int i : candidates) {
        if (shown(rows[i].values[column], column) == top) winners.insert(i);
    }
    return winners;
}

QString dateList(const QList<QDate> &dates)
{
    const QLocale locale;
    QStringList parts;
    int i = 0;
    while (i < dates.size()) {
        int j = i;
        while (j + 1 < dates.size() && dates[j].daysTo(dates[j + 1]) == 1) ++j;
        QString part = locale.toString(dates[i], QLocale::ShortFormat);
        if (j > i) part += QStringLiteral(" – ") + locale.toString(dates[j], QLocale::ShortFormat);
        parts << part;
        i = j + 1;
    }
    return parts.join(QStringLiteral("; "));
}

int decimals(Column column)
{
    switch (column) {
    case Nights:
        return 0;
    case Usage:
    case DeviceAhi:
    case AnalysisAhi:
    case Odi3:
        return 2;
    case Leak:
    case Pressure:
    case FlowLimitation:
    case Below90:
    case ColumnCount:
        break;
    }
    return 1;
}

} // namespace SettingsComparison
