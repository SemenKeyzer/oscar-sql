/* Sleep Analysis Event Matcher Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_EVENT_MATCHER_H
#define ANALYSIS_EVENT_MATCHER_H

#include <QPair>
#include <QVector>

namespace analysis {

//! The respiratory event types compared between the device and the analysis.
enum class RespEvent {
    ObstructiveApnea = 0,
    CentralApnea,
    Apnea,                  //!< unclassified: the device's UA and A, the analysis' aA
    ObstructiveHypopnea,
    CentralHypopnea,
    Hypopnea,
    Rera,
};
constexpr int kRespEventTypes = 7;

enum class EventGroup { Apnea = 0, Hypopnea, Rera };
constexpr int kEventGroups = 3;

EventGroup groupOf(RespEvent type);

//! One event to match: its interval and type.
struct MatchEvent {
    qint64 start = 0;
    qint64 end = 0;
    RespEvent type = RespEvent::Apnea;
};

//! How the device's events and the analysis' events pair up (spec §3.4.6).
struct MatchResult {
    QVector<QPair<int, int>> matched;   //!< (device index, analysis index)
    QVector<int> deviceOnly;
    QVector<int> analysisOnly;
    int typeMismatch = 0;               //!< matched pairs of different groups
    //! Matched pairs by type: [device type * kRespEventTypes + analysis type].
    QVector<int> typeMatrix = QVector<int>(kRespEventTypes * kRespEventTypes, 0);

    //! matched / (device + analysis - matched); 1 when both have no events.
    double agreement() const;
};

//! Pairs events whose intervals, each widened by \a toleranceMs on both sides, overlap.
//! Greedy by the largest overlap: first within the same group (apnea, hypopnea, RERA),
//! then across groups (a "type mismatch"). Each event is used at most once.
MatchResult matchEvents(const QVector<MatchEvent> &device, const QVector<MatchEvent> &analysis,
                        qint64 toleranceMs = 5000);

} // namespace analysis

#endif // ANALYSIS_EVENT_MATCHER_H
