/* Device Settings Comparison Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SETTINGSCOMPARISON_H
#define SETTINGSCOMPARISON_H

#include <QDate>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <limits>

//! Compares how the nights went on each set of device settings (Statistics, "Settings" mode).
namespace SettingsComparison {

//! Nights a row needs before its figures count as a result.
constexpr int kMinNights = 3;

//! One stretch of nights on unchanged settings, as Statistics::updateRXChanges() finds them.
struct Period {
    QString mode;
    QString pressure;
    QString relief;
    QString deviceKey;      //!< equal for nights on what counts as the same device
    QString deviceLabel;
    QList<QDate> dates;
    double hours = 0;
    double ahiHours = -1;   //!< hours the AHI counts over (less manually excluded stretches); <0: the same as hours
    double events = 0;      //!< the device's AHI (or RDI) events
};

//! All periods with the same settings on the same device, wherever they fall in the history.
struct Group {
    QString mode;
    QString pressure;
    QString relief;
    QString deviceKey;
    QString deviceLabel;
    QList<QDate> dates;     //!< ascending, no duplicates
    double hours = 0;
    double ahiHours = -1;   //!< hours the AHI counts over; <0: the same as hours
    double events = 0;
};

//! Merges \a periods with equal settings and device; the group used last comes first.
QList<Group> group(const QList<Period> &periods);

enum Column { Nights, Usage, DeviceAhi, AnalysisAhi, Leak, Pressure, FlowLimitation, Glasgow, Odi3, Below90, ColumnCount };

//! A table row: its group and one value per column, NaN where there is no data.
struct Row {
    Group group;
    QVector<double> values = QVector<double>(ColumnCount, std::numeric_limits<double>::quiet_NaN());
    int nights() const { return group.dates.size(); }
};

//! A row with the nights, usage and device AHI of \a group filled in.
Row row(const Group &group);
//! Whether \a row has enough nights to be compared.
bool reliable(const Row &row);
//! Rows holding the best value of \a column among the reliable ones; empty with fewer than two.
QSet<int> best(const QList<Row> &rows, Column column);
//! \a dates (ascending) as text, nights in a row shown as one range.
QString dateList(const QList<QDate> &dates);
//! "mode · pressure · relief": each part trimmed, empty ones left out.
QString settingsLabel(const QString &mode, const QString &pressure, const QString &relief);
//! Decimals \a column is shown with.
int decimals(Column column);

//! Background of the best value in a column.
const QString kBestColor = QStringLiteral("#a8e0a8");
//! Text colour of rows with too few nights.
const QString kFewColor = QStringLiteral("#909090");
//! Shown where a row has no data for a column.
const QString kNoData = QStringLiteral("–");

struct Options {
    bool showDevice = false;            //!< more than one device in the history
    QString ahiName = QStringLiteral("AHI");
    double percentile = 95;             //!< of the pressure column
    QString headingColor = QStringLiteral("#ffffff");
    QStringList rowColors;              //!< background per row, cycled; white when empty
    QString settingsWidth;              //!< width of the settings column, e.g. "34%"; empty: as wide as it needs
    bool helpLinks = false;             //!< column heads and the note link their explanations (not in the PDF)
};

//! The whole comparison table: title, column heads, one row per group and the note.
QString html(const QList<Row> &rows, const Options &options);

} // namespace SettingsComparison

#endif // SETTINGSCOMPARISON_H
