/* Sleep Analysis Device Event Conventions Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_DEVICE_EVENT_CONVENTIONS_H
#define ANALYSIS_DEVICE_EVENT_CONVENTIONS_H

#include <QString>

#include "oxi_analyzer.h"

namespace analysis {

//! What a device event's time stamp marks.
enum class EventTimeConvention {
    End,     //!< OSCAR's convention: the event ends at its time stamp
    Onset,   //!< the event starts at its time stamp
};

//! How one loader stores the time and length of respiratory events.
struct DeviceEventConvention {
    EventTimeConvention time = EventTimeConvention::End;
    double defaultDurationSec = 10;   //!< for events without a usable duration
};

//! The convention of the loader named \a loaderName (Machine::loaderName()).
DeviceEventConvention deviceEventConvention(const QString &loaderName);

//! The interval of a device event stored at \a time with \a durationSec (EventList data).
//! A duration of 0 or less, or of more than 5 minutes, is not a duration: the default
//! is used instead.
Span deviceEventSpan(qint64 time, double durationSec, const DeviceEventConvention &convention);

} // namespace analysis

#endif // ANALYSIS_DEVICE_EVENT_CONVENTIONS_H
