/* Sleep Analysis Service
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysis_service.h"

#include <QDebug>

#include "session_analysis.h"
#include "SleepLib/day.h"
#include "SleepLib/profiles.h"
#include "SleepLib/session.h"

namespace analysis {

AnalysisService::AnalysisService(QObject *parent)
    : QObject(parent)
{
}

qint64 AnalysisService::profileId() const
{
    return p_profile ? p_profile->getDatabaseId() : 0;
}

void AnalysisService::reloadSettings()
{
    m_params = p_profile && p_profile->analysis ? p_profile->analysis->params() : AnalysisParams();
    setActiveParams(m_params);
    m_cacheLoaded = false;
    m_rows.clear();
}

void AnalysisService::reset()
{
    m_cacheLoaded = false;
    m_cacheProfile = 0;
    m_rows.clear();
}

void AnalysisService::ensureCache()
{
    const qint64 id = profileId();
    if (m_cacheLoaded && m_cacheProfile == id) return;
    m_rows.clear();
    m_cacheProfile = id;
    m_cacheLoaded = true;
    if (id <= 0) return;
    for (const AnalysisDailyData &d : AnalysisDailyRepository().findRange(id, QDate(1900, 1, 1), QDate(9999, 12, 31))) {
        m_rows.insert(d.date, d);
    }
}

void AnalysisService::reloadCache()
{
    m_cacheLoaded = false;
    ensureCache();
}

void AnalysisService::refreshRow(const QDate &date)
{
    const AnalysisDailyData d = AnalysisDailyRepository().find(profileId(), date);
    if (d.id) m_rows.insert(date, d);
    else m_rows.remove(date);
}

AnalysisDailyData AnalysisService::row(const QDate &date)
{
    ensureCache();
    return m_rows.value(date);
}

QList<AnalysisDailyData> AnalysisService::rows(const QDate &from, const QDate &to)
{
    ensureCache();
    QList<AnalysisDailyData> out;
    for (auto it = m_rows.lowerBound(from); it != m_rows.end() && it.key() <= to; ++it) out.append(it.value());
    return out;
}

QList<QDate> AnalysisService::daysToUpdate(bool pendingOnly)
{
    QList<QDate> out;
    if (!p_profile || !m_params.enabled) return out;
    ensureCache();
    for (auto it = p_profile->daylist.begin(); it != p_profile->daylist.end(); ++it) {
        Day *day = p_profile->GetDay(it.key());
        const QList<Session *> sessions = analysableSessions(day);
        if (sessions.isEmpty()) {
            if (m_rows.contains(it.key())) out.append(it.key());   // a row left behind
            continue;
        }
        if (pendingOnly) {
            bool stageOneCurrent = true;
            for (Session *s : sessions) stageOneCurrent = stageOneCurrent && !stageOneNeeded(s, m_params).any();
            if (!stageOneCurrent) continue;
        }
        if (dayOutdated(day, m_params, m_rows.value(it.key()))) out.append(it.key());
    }
    // rows of days that are gone altogether
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        if (!p_profile->daylist.contains(it.key())) out.append(it.key());
    }
    std::sort(out.begin(), out.end());
    return out;
}

QList<QDate> AnalysisService::pendingDays()
{
    return daysToUpdate(true);
}

QList<QDate> AnalysisService::outdatedDays()
{
    return daysToUpdate(false);
}

bool AnalysisService::updateDay(const QDate &date)
{
    if (!p_profile || !m_params.enabled) return false;
    ensureCache();
    Day *day = p_profile->GetDay(date);
    if (analysableSessions(day).isEmpty()) {
        if (!m_rows.contains(date)) return false;
        AnalysisDailyRepository().remove(profileId(), date);
        m_rows.remove(date);
        return true;
    }
    const DayAnalysis a = analyzeDay(day, m_params, true);
    if (a.scored) refreshRow(date);
    return a.scored;
}

int AnalysisService::updateDays(const QList<QDate> &dates, const std::function<bool(int, int)> &progress)
{
    QList<QDate> changed;
    int done = 0;
    for (const QDate &date : dates) {
        if (progress && !progress(done, dates.size())) break;
        if (updateDay(date)) changed.append(date);
        ++done;
    }
    if (progress) progress(done, dates.size());
    if (!changed.isEmpty()) emit daysChanged(changed);
    return done;
}

DayResult AnalysisService::dayResult(Day *day, QString *oxiSource)
{
    if (!day || !m_params.enabled) return DayResult();
    ensureCache();
    const DayAnalysis a = analyzeDay(day, m_params, true);
    if (a.scored) {
        refreshRow(day->date());
        emit daysChanged({ day->date() });
        if (oxiSource) *oxiSource = a.oxiSource;
        return a.result;
    }
    return scoreStoredDay(day, m_params, oxiSource);
}

} // namespace analysis
