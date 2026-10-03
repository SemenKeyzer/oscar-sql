/* Doctor Report Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DOCTORREPORT_H
#define DOCTORREPORT_H

#include <QColor>
#include <QCoreApplication>
#include <QDate>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <limits>

#include "nightsummary.h"
#include "settingscomparison.h"

//! The one-page report for the doctor: how the therapy went over a span of dates.
struct DoctorReport {
    static constexpr double kNoValue = std::numeric_limits<double>::quiet_NaN();

    //! One date of the period; no hours and AHI when nothing was recorded that night.
    struct Night {
        QDate date;
        double hours = kNoValue;
        double ahi = kNoValue;
    };

    // header
    QString patient;                //!< empty when personal data is hidden
    QDate birthDate;                //!< invalid when hidden or unknown
    QStringList cpapDevices;
    QString oximeter;               //!< empty without oximetry in the period
    QDate from;
    QDate to;
    int nights = 0;                 //!< nights with CPAP data
    int oximetryNights = 0;
    QString currentSettings;        //!< empty when unknown
    QDate settingsSince;

    // totals
    double meanHours = kNoValue;
    int compliantNights = 0;
    double complianceHours = 4;
    QString ahiName = QStringLiteral("AHI");
    double deviceAhi = kNoValue;
    double analysisAhi = kNoValue;
    double flowLimitation = kNoValue;
    double leak = kNoValue;
    QString leakUnits;
    double leakRedline = 0;         //!< 0: none set
    double pressure = kNoValue;
    QString pressureUnits;
    double percentile = 95;
    double odi3 = kNoValue;
    double below90 = kNoValue;

    QVector<Night> nightList;       //!< every date of the period, oldest first
    QList<QDate> settingsChanges;   //!< nights a new settings period started on
    QList<SettingsComparison::Row> comparison;
    bool showDevice = false;

    //! Dates in the period, nights without data included.
    int days() const { return from.isValid() && to.isValid() ? int(from.daysTo(to)) + 1 : 0; }
    NightSummary::Level usageLevel() const;
    NightSummary::Level ahiLevel() const;
    NightSummary::Level leakLevel() const;
    NightSummary::Level odiLevel() const;
    NightSummary::Level below90Level() const;

    // DoctorReport::tr(), so lupdate finds the page's texts; last, as the macro ends in private:
    Q_DECLARE_TR_FUNCTIONS(DoctorReport)
};

//! Turns a DoctorReport into the chart, the page and the PDF.
namespace DoctorReportPage {

//! Where the chart puts things, for drawing and for tests.
struct ChartLayout {
    QRect ahi;              //!< the AHI bars' area
    QRect hours;            //!< the hours bars' area
    double column = 0;      //!< width of one night
    int left = 0;           //!< x of the first night
};

ChartLayout chartLayout(const QSize &size, int nights);
//! Colour of the hours bars at or above the compliance hours.
QColor hoursColor();
//! Colour of the lines where the settings changed.
QColor changeColor();
//! AHI (top) and hours of use (bottom) for every night, settings changes as grey lines.
QImage chart(const DoctorReport &report, const QSize &size);

} // namespace DoctorReportPage

#endif // DOCTORREPORT_H
