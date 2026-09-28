/* Sleep Analysis Service Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_SERVICE_H
#define ANALYSIS_SERVICE_H

#include <QDate>
#include <QList>
#include <QMap>
#include <QObject>
#include <functional>

#include "analysis_params.h"
#include "day_analysis.h"
#include "database/analysis_daily_repository.h"

class Day;

namespace analysis {

/*!
 * \class AnalysisService
 * \brief Keeps the open profile's sleep analysis current (spec §4.4).
 *
 * Holds the parameters in force (from Profile::analysis, handed on to the loaders),
 * a cache of the profile's analysis_daily rows for the Overview and Statistics, and
 * brings days up to date:
 * - "pending" days, whose sessions' stage 1 is current but whose day scoring is
 *   missing or outdated (new imports, time corrections, a session switched on or
 *   off, a purge): cheap, no waveforms;
 * - "outdated" days, which include those whose sessions' stage 1 is missing or
 *   outdated (data imported before the analysis existed, changed flow or oximetry
 *   parameters): these need the waveforms, so they wait for the day to be opened or
 *   for a recalculation the user asks for.
 * Lives on the main thread; owned by the main window.
 */
class AnalysisService : public QObject
{
    Q_OBJECT
  public:
    explicit AnalysisService(QObject *parent = nullptr);

    //! Takes the parameters from the open profile (and hands them to the loaders) and
    //! forgets the cached rows. Call when a profile opens and when its settings change.
    void reloadSettings();
    //! Forgets the profile: when it closes.
    void reset();
    AnalysisParams params() const { return m_params; }

    //! Days whose day scoring is missing or outdated while their stage 1 is current.
    QList<QDate> pendingDays();
    //! Every day whose analysis is missing or outdated, stage 1 included.
    QList<QDate> outdatedDays();
    //! outdatedDays().size(), kept until something may have changed it (for notices).
    int outdatedCount();

    //! Brings a day up to date (stage 1 of its sessions where needed, then the day);
    //! removes the stored row of a day left without anything to analyse. Returns
    //! whether anything was written.
    bool updateDay(const QDate &date);
    //! updateDay() for each date; \a progress(done, total) returning false cancels.
    //! Emits daysChanged() once for the days written. Returns how many were done.
    int updateDays(const QList<QDate> &dates, const std::function<bool(int, int)> &progress = {});

    //! The day brought up to date and scored, for display.
    DayResult dayResult(Day *day, QString *oxiSource = nullptr);

    //! The stored row of a day, or one with id 0.
    AnalysisDailyData row(const QDate &date);
    //! The stored rows from \a from to \a to, in date order.
    QList<AnalysisDailyData> rows(const QDate &from, const QDate &to);
    //! Rereads the rows from the database (after something else changed them).
    void reloadCache();

  signals:
    //! The stored analysis of these days changed.
    void daysChanged(const QList<QDate> &dates);

  private:
    qint64 profileId() const;
    void ensureCache();
    void refreshRow(const QDate &date);
    //! \a pendingOnly: skip the days whose stage 1 is outdated.
    QList<QDate> daysToUpdate(bool pendingOnly);

    AnalysisParams m_params;
    qint64 m_cacheProfile = 0;
    bool m_cacheLoaded = false;
    QMap<QDate, AnalysisDailyData> m_rows;
    int m_outdatedCount = -1;   //!< -1: not known
};

} // namespace analysis

#endif // ANALYSIS_SERVICE_H
