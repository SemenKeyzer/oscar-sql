/* Manual scoring storage
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef MANUAL_SCORING_REPOSITORY_H
#define MANUAL_SCORING_REPOSITORY_H

#include "SleepLib/manual_scoring.h"

//! The doctor's scoring edits (table manual_scoring) and their per-session result
//! (manual_scoring_summary), both keyed by the session's database row.
class ManualScoringRepository
{
  public:
    static QList<ManualScoring::Edit> editsForSession(qint64 sessionRow);
    static QList<ManualScoring::Edit> editsForSessions(const QList<qint64> &sessionRows);
    //! Stores \a edit (its id is ignored); returns the new id, 0 on failure.
    static qint64 add(const ManualScoring::Edit &edit);
    static bool remove(qint64 id);
    static bool removeAllForSessions(const QList<qint64> &sessionRows);

    static bool storeSummary(qint64 sessionRow, const ManualScoring::Result &result);
    //! False when the session has no stored result.
    static bool loadSummary(qint64 sessionRow, QHash<ChannelID, int> &delta, qint64 &excludedMs, int &notFound);
    static bool removeSummary(qint64 sessionRow);
};

#endif // MANUAL_SCORING_REPOSITORY_H
