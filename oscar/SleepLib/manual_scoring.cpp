/* Manual scoring of respiratory events
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "SleepLib/manual_scoring.h"

#include <algorithm>

#include "SleepLib/schema.h"

namespace ManualScoring {

QList<ChannelID> scoredChannels()
{
    // built on each call: the channel ids are set by schema::init()
    return { CPAP_Obstructive, CPAP_ClearAirway, CPAP_Apnea, CPAP_Hypopnea };
}

namespace {

//! The union of \a spans clipped to \a sessions, sorted and without overlaps.
QList<QPair<qint64, qint64>> clippedUnion(const QList<QPair<qint64, qint64>> &spans, const QList<QPair<qint64, qint64>> &sessions)
{
    QList<QPair<qint64, qint64>> parts;
    for (const auto &s : spans) {
        for (const auto &sess : sessions) {
            const qint64 a = qMax(s.first, sess.first), b = qMin(s.second, sess.second);
            if (b > a) parts.append({ a, b });
        }
    }
    std::sort(parts.begin(), parts.end());
    QList<QPair<qint64, qint64>> merged;
    for (const auto &p : parts) {
        if (!merged.isEmpty() && p.first <= merged.last().second) merged.last().second = qMax(merged.last().second, p.second);
        else merged.append(p);
    }
    return merged;
}

} // namespace

Result apply(const QList<DeviceEvent> &device, const QList<Edit> &edits, const QList<QPair<qint64, qint64>> &sessionSpans)
{
    const QList<ChannelID> scored = scoredChannels();
    Result r;
    QHash<ChannelID, int> deviceCount;
    for (const DeviceEvent &d : device) {
        if (!scored.contains(d.channel)) continue;
        r.events.append({ d.channel, d.channel, d.endMs, d.durationSec, Origin::Device, 0, false });
        ++deviceCount[d.channel];
    }
    const int deviceEnd = r.events.size();   // the device events come first

    QList<Edit> ordered = edits;
    std::sort(ordered.begin(), ordered.end(), [](const Edit &a, const Edit &b) { return a.id < b.id; });
    QList<QPair<qint64, qint64>> excludes;
    for (const Edit &e : ordered) {
        switch (e.kind) {
        case Kind::Add:
            r.events.append({ e.channel, e.channel, e.endMs, (e.endMs - e.startMs) / 1000.0, Origin::Added, e.id, false });
            break;
        case Kind::Exclude:
            excludes.append({ e.startMs, e.endMs });
            break;
        case Kind::Remove:
        case Kind::Retype: {
            // the nearest device event of that type; a later edit of the same event wins
            int best = -1;
            qint64 bestGap = kMatchToleranceMs + 1;
            for (int i = 0; i < deviceEnd; ++i) {
                const EffectiveEvent &ev = r.events.at(i);
                const qint64 gap = qAbs(ev.endMs - e.endMs);
                if (ev.originalChannel == e.channel && gap < bestGap) {
                    best = i;
                    bestGap = gap;
                }
            }
            if (best < 0) {
                r.notFound.append(e.id);
                break;
            }
            EffectiveEvent &ev = r.events[best];
            ev.editId = e.id;
            if (e.kind == Kind::Remove) {
                ev.origin = Origin::Removed;
                ev.channel = ev.originalChannel;
            } else {
                ev.origin = Origin::Retyped;
                ev.channel = e.newChannel;
            }
            break;
        }
        }
    }

    const QList<QPair<qint64, qint64>> excluded = clippedUnion(excludes, sessionSpans);
    for (const auto &span : excluded) r.excludedMs += span.second - span.first;
    QHash<ChannelID, int> counted;
    for (EffectiveEvent &ev : r.events) {
        for (const auto &span : excluded) {
            if (ev.endMs >= span.first && ev.endMs <= span.second) ev.excluded = true;
        }
        if (ev.origin != Origin::Removed && !ev.excluded) ++counted[ev.channel];
    }
    for (ChannelID c : scored) {
        const int d = counted.value(c) - deviceCount.value(c);
        if (d != 0) r.delta.insert(c, d);
    }
    return r;
}

} // namespace ManualScoring
