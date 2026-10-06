/* Device Settings Comparison
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "settingscomparison.h"
#include "helptips.h"

#include <QCoreApplication>
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

QString settingsLabel(const QString &mode, const QString &pressure, const QString &relief)
{
    QStringList parts;
    for (const QString &part : { mode, pressure, relief }) {
        if (!part.trimmed().isEmpty()) parts << part.trimmed();
    }
    return parts.join(QStringLiteral(" · "));
}

int decimals(Column column)
{
    switch (column) {
    case Nights:
        return 0;
    case Usage:
    case DeviceAhi:
    case AnalysisAhi:
    case Glasgow:
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

namespace {

const QList<Column> kTableColumns { Nights, Usage, DeviceAhi, AnalysisAhi, Leak, Pressure, FlowLimitation, Glasgow, Odi3, Below90 };

QString columnHelpKey(Column column)
{
    switch (column) {
    case Nights: return QStringLiteral("nights_with_data");
    case Usage: return QStringLiteral("usage");
    case DeviceAhi: return QStringLiteral("ahi");
    case AnalysisAhi: return QStringLiteral("an_ahi");
    case Leak: return QStringLiteral("leak");
    case Pressure: return QStringLiteral("p95");
    case FlowLimitation: return QStringLiteral("fl_time");
    case Glasgow: return QStringLiteral("glasgow");
    case Odi3: return QStringLiteral("odi3");
    case Below90: return QStringLiteral("t90");
    case ColumnCount: break;
    }
    return QString();
}

QString columnTitle(Column column, const Options &options)
{
    const QLocale locale;
    switch (column) {
    case Nights: return QCoreApplication::translate("SettingsComparison", "Nights");
    case Usage: return QCoreApplication::translate("SettingsComparison", "Usage, h");
    case DeviceAhi: return QCoreApplication::translate("SettingsComparison", "Device %1").arg(options.ahiName);
    case AnalysisAhi: return QCoreApplication::translate("SettingsComparison", "Analysis AHI");
    case Leak: return QCoreApplication::translate("SettingsComparison", "Leak");
    case Pressure:
        return QCoreApplication::translate("SettingsComparison", "Pressure %1%").arg(locale.toString(options.percentile));
    case FlowLimitation: return QCoreApplication::translate("SettingsComparison", "Flow limitation, %");
    case Glasgow: return QCoreApplication::translate("SettingsComparison", "Glasgow Index");
    case Odi3: return QCoreApplication::translate("SettingsComparison", "ODI 3%");
    case Below90: return QCoreApplication::translate("SettingsComparison", "SpO2 < 90%");
    case ColumnCount: break;
    }
    return QString();
}

QString formatValue(double value, Column column)
{
    if (std::isnan(value) || std::isinf(value)) return kNoData;
    if (column == Nights) return QString::number(qRound(value));
    return QLocale().toString(value, 'f', decimals(column));
}

QString settingsText(const Group &g)
{
    return settingsLabel(g.mode, g.pressure, g.relief);
}

} // namespace

QString html(const QList<Row> &rows, const Options &options)
{
    const QLocale locale;
    QVector<QSet<int>> winners(ColumnCount);
    for (Column c : kTableColumns) winners[c] = best(rows, c);

    const int span = 1 + (options.showDevice ? 1 : 0) + kTableColumns.size();
    QString html = QStringLiteral("<table class=curved width='100%' cellpadding=2>");
    html += QStringLiteral("<tr bgcolor='%1'><th colspan=%2 align=center><font size='+2'>%3</font></th></tr>")
                .arg(options.headingColor).arg(span)
                .arg(QCoreApplication::translate("SettingsComparison", "Device Settings Compared").toHtmlEscaped());

    const QString width = options.settingsWidth.isEmpty() ? QString()
                                                          : QStringLiteral(" width='%1'").arg(options.settingsWidth);
    html += QStringLiteral("<tr><th align=left%1>%2</th>")
                .arg(width, QCoreApplication::translate("SettingsComparison", "Settings").toHtmlEscaped());
    if (options.showDevice) {
        html += QStringLiteral("<th align=left>%1</th>")
                    .arg(QCoreApplication::translate("SettingsComparison", "Device").toHtmlEscaped());
    }
    for (Column c : kTableColumns) {
        QString head = columnTitle(c, options).toHtmlEscaped();
        if (options.helpLinks) head = HelpTips::term(head, columnHelpKey(c));
        if (c == Pressure) {
            const QString tip = QCoreApplication::translate("SettingsComparison",
                                    "Average over the nights of each night's %1th percentile")
                                    .arg(locale.toString(options.percentile));
            head = QStringLiteral("<span title=\"%1\">%2</span>").arg(tip.toHtmlEscaped(), head);
        }
        html += QStringLiteral("<th align=right>%1</th>").arg(head);
    }
    html += QStringLiteral("</tr>");

    for (int i = 0; i < rows.size(); ++i) {
        const Row &r = rows[i];
        const bool few = !reliable(r);
        auto look = [few](const QString &text) {
            return few ? QStringLiteral("<font color='%1'>%2</font>").arg(kFewColor, text) : text;
        };
        const QString background = options.rowColors.isEmpty() ? QStringLiteral("#ffffff")
                                                               : options.rowColors[i % options.rowColors.size()];
        html += QStringLiteral("<tr bgcolor='%1'>").arg(background);
        html += QStringLiteral("<td><span title=\"%1\">%2</span></td>")
                    .arg(dateList(r.group.dates).toHtmlEscaped(), look(settingsText(r.group).toHtmlEscaped()));
        if (options.showDevice) html += QStringLiteral("<td>%1</td>").arg(look(r.group.deviceLabel.toHtmlEscaped()));
        for (Column c : kTableColumns) {
            QString text = formatValue(r.values[c], c);
            if (c == Nights && few) {
                text = QCoreApplication::translate("SettingsComparison", "%1 (few nights)").arg(r.nights());
            }
            // bold as well: Statistics shades every other row a light green of its own
            const bool won = winners[c].contains(i);
            QString shown = look(text.toHtmlEscaped());
            if (won) shown = QStringLiteral("<b>%1</b>").arg(shown);
            const QString mark = won ? QStringLiteral(" bgcolor='%1'").arg(kBestColor) : QString();
            html += QStringLiteral("<td align=right%1>%2</td>").arg(mark, shown);
        }
        html += QStringLiteral("</tr>");
    }

    const QString note = QCoreApplication::translate("SettingsComparison",
        "Green marks the best value among settings used for at least %1 nights. Grey rows have fewer "
        "nights, too few to judge. Pressure is the average over the nights.").arg(kMinNights);
    const QString noteHtml = options.helpLinks ? HelpTips::term(note.toHtmlEscaped(), QStringLiteral("best_value")) : note.toHtmlEscaped();
    html += QStringLiteral("<tr><td colspan=%1 align=center><i>%2</i></td></tr>").arg(QString::number(span), noteHtml);
    html += QStringLiteral("</table>");
    return html;
}

} // namespace SettingsComparison
