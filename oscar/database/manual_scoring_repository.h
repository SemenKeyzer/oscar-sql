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
//! (manual_scoring_summary), keyed by profile, device serial number and device session number,
//! so they outlive a rebuild of the device's data.
class ManualScoringRepository
{
  public:
    static QList<ManualScoring::Edit> editsForSession(const ManualScoring::SessionKey &key);
    //! Stores \a edit (its id is ignored); returns the new id, 0 on failure.
    static qint64 add(const ManualScoring::Edit &edit);
    static bool remove(qint64 id);
    static bool removeAllForSession(const ManualScoring::SessionKey &key);

    static bool storeSummary(const ManualScoring::SessionKey &key, const ManualScoring::Result &result);
    //! False when the session has no stored result.
    static bool loadSummary(const ManualScoring::SessionKey &key, QHash<ChannelID, int> &delta, qint64 &excludedMs, int &notFound);
    static bool removeSummary(const ManualScoring::SessionKey &key);
};

#endif // MANUAL_SCORING_REPOSITORY_H
