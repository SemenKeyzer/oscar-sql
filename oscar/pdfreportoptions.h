/* PDF Report Options Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PDFREPORTOPTIONS_H
#define PDFREPORTOPTIONS_H

#include <QDate>
#include <QList>
#include <QPair>
#include <QVariantMap>

#include "overviewpresets.h"

//! What a combined PDF report holds: its period, sections and how much of each.
struct PdfReportOptions {
    enum PeriodKind { Last7, Last30, Last90, Custom };
    enum NightsKind { LastNight, Last3, Last7Nights, AllNights };
    enum Preset { Brief, Detailed, Everything };

    PeriodKind period = Last30;
    QDate from, to;                  //!< the Custom period
    bool summary = true;             //!< the one-page summary for the doctor
    bool daily = false;
    NightsKind nights = Last7Nights;
    bool overview = false;
    OverviewPresets::Preset overviewPreset = OverviewPresets::Therapy;
    bool statistics = false;
    bool statsSettings = true, statsOximetry = true, statsDevices = false;
    bool personalData = true, serialNumbers = false;

    //! Ticks the sections of \a preset; the personal-data choices stay as they are.
    void apply(Preset preset);
    //! The report's dates: the last 7, 30 or 90 nights up to \a lastNight, or from and to.
    QPair<QDate, QDate> range(const QDate &lastNight) const;
    //! The nights the Daily section prints: of \a cpapNights, those in range(), the chosen
    //! number from its end, oldest first.
    QList<QDate> nightsToPrint(const QList<QDate> &cpapNights) const;
    bool anySection() const { return summary || daily || overview || statistics; }

    QVariantMap toMap() const;
    //! The options saved by toMap(); anything missing or unknown keeps its default.
    static PdfReportOptions fromMap(const QVariantMap &map);
};

//! Roughly how many pages the report takes: \a nights printed, \a dailyGraphs visible on the
//! Daily tab, \a overviewGraphs in the chosen set, \a statisticsPages for the statistics.
int estimatePages(const PdfReportOptions &o, int nights, int dailyGraphs, int overviewGraphs, int statisticsPages);

#endif // PDFREPORTOPTIONS_H
