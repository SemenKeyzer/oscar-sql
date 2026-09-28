/* Sleep Analysis Device Event Conventions
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "device_event_conventions.h"

#include <cmath>

namespace analysis {

namespace {

constexpr double kMaxDurationSec = 300;

} // namespace

DeviceEventConvention deviceEventConvention(const QString &loaderName)
{
    DeviceEventConvention c;
    // ResMed's EVE files store each event as an EDF+ annotation, whose time is by
    // definition the onset; OSCAR draws it as the end, and on the flow graph the flags
    // do sit at the end of the flat stretch. Day scoring logs the median offset of
    // matched apneas (see day_analysis.cpp) so that real nights can settle it; until
    // then ResMed keeps OSCAR's convention, like every other loader.
    if (loaderName == QLatin1String("ResMed")) c.time = EventTimeConvention::End;
    return c;
}

Span deviceEventSpan(qint64 time, double durationSec, const DeviceEventConvention &convention)
{
    double d = durationSec;
    if (!(d > 0) || d > kMaxDurationSec) d = convention.defaultDurationSec;
    const qint64 ms = qint64(std::llround(d * 1000.0));
    if (convention.time == EventTimeConvention::Onset) return Span { time, time + ms, 0 };
    return Span { time - ms, time, 0 };
}

} // namespace analysis
