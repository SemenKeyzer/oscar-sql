/* Manual scoring of respiratory events
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef MANUAL_SCORING_H
#define MANUAL_SCORING_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

#include "SleepLib/machine_common.h"

//! A doctor's corrections of the device's apneas and hypopneas, kept apart from the device data:
//! events added, removed or retyped, and stretches excluded from scoring (noise, awake).
namespace ManualScoring {

enum class Kind { Add, Remove, Retype, Exclude };

//! One correction. Remove and Retype name the device event by its channel and end time.
struct Edit {
    qint64 id = 0;
    SessionID session = 0;
    Kind kind = Kind::Add;
    ChannelID channel = 0;      //!< the added type, or the device event's own type
    ChannelID newChannel = 0;   //!< Retype: the new type
    qint64 startMs = 0;
    qint64 endMs = 0;           //!< an event counts at its end
    QString note;
    QDateTime createdAt;
};

//! A device event of a scored channel: its end time and duration.
struct DeviceEvent {
    ChannelID channel;
    qint64 endMs;
    double durationSec;
};

enum class Origin { Device, Added, Removed, Retyped };

//! An event as the corrected night has it.
struct EffectiveEvent {
    ChannelID channel;           //!< the type it counts as (Removed: its own type)
    ChannelID originalChannel;   //!< the device's type (Added: the same as channel)
    qint64 endMs;
    double durationSec;
    Origin origin;
    qint64 editId;               //!< the edit that made it so; 0 for an untouched device event
    bool excluded;               //!< ends inside an excluded stretch: not counted
};

struct Result {
    QHash<ChannelID, int> delta;   //!< counted minus device count, per scored channel
    qint64 excludedMs = 0;         //!< excluded time inside the sessions, overlaps counted once
    QList<qint64> notFound;        //!< Remove/Retype edits whose device event is not there
    QList<EffectiveEvent> events;  //!< every device and added event of the scored channels
};

constexpr qint64 kMatchToleranceMs = 1000;
constexpr double kShortEventSec = 10.0;

//! The channels that can be scored: OA, CA, A, H.
QList<ChannelID> scoredChannels();

//! The corrected night of one session or day. \a sessionSpans are the sessions' [start, end) in ms.
Result apply(const QList<DeviceEvent> &device, const QList<Edit> &edits, const QList<QPair<qint64, qint64>> &sessionSpans);

} // namespace ManualScoring

#endif // MANUAL_SCORING_H
