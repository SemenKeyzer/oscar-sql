/* Sleep Analysis Event Matcher
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "event_matcher.h"

#include <algorithm>

namespace analysis {

namespace {

struct Candidate {
    qint64 overlap;
    int device;
    int analysis;
};

// Takes the candidate pairs in order of decreasing overlap while both events are free.
void takeGreedy(QVector<Candidate> &pairs, QVector<bool> &deviceUsed, QVector<bool> &analysisUsed,
                QVector<QPair<int, int>> &out)
{
    std::sort(pairs.begin(), pairs.end(), [](const Candidate &a, const Candidate &b) {
        if (a.overlap != b.overlap) return a.overlap > b.overlap;
        if (a.device != b.device) return a.device < b.device;
        return a.analysis < b.analysis;
    });
    for (const Candidate &c : pairs) {
        if (deviceUsed[c.device] || analysisUsed[c.analysis]) continue;
        deviceUsed[c.device] = analysisUsed[c.analysis] = true;
        out.append({ c.device, c.analysis });
    }
}

} // namespace

EventGroup groupOf(RespEvent type)
{
    switch (type) {
    case RespEvent::ObstructiveApnea:
    case RespEvent::CentralApnea:
    case RespEvent::Apnea:
        return EventGroup::Apnea;
    case RespEvent::ObstructiveHypopnea:
    case RespEvent::CentralHypopnea:
    case RespEvent::Hypopnea:
        return EventGroup::Hypopnea;
    case RespEvent::Rera:
        break;
    }
    return EventGroup::Rera;
}

double MatchResult::agreement() const
{
    const int device = matched.size() + deviceOnly.size();
    const int analysis = matched.size() + analysisOnly.size();
    const int all = device + analysis - matched.size();
    return all > 0 ? double(matched.size()) / all : 1.0;
}

MatchResult matchEvents(const QVector<MatchEvent> &device, const QVector<MatchEvent> &analysis,
                        qint64 toleranceMs)
{
    MatchResult r;
    QVector<Candidate> same, cross;
    for (int i = 0; i < device.size(); ++i) {
        const qint64 ds = device[i].start - toleranceMs, de = device[i].end + toleranceMs;
        for (int j = 0; j < analysis.size(); ++j) {
            const qint64 overlap = qMin(de, analysis[j].end + toleranceMs) - qMax(ds, analysis[j].start - toleranceMs);
            if (overlap <= 0) continue;
            (groupOf(device[i].type) == groupOf(analysis[j].type) ? same : cross).append({ overlap, i, j });
        }
    }

    QVector<bool> deviceUsed(device.size(), false), analysisUsed(analysis.size(), false);
    takeGreedy(same, deviceUsed, analysisUsed, r.matched);
    const int sameGroup = r.matched.size();
    takeGreedy(cross, deviceUsed, analysisUsed, r.matched);
    r.typeMismatch = r.matched.size() - sameGroup;

    for (const auto &m : r.matched) {
        ++r.typeMatrix[int(device[m.first].type) * kRespEventTypes + int(analysis[m.second].type)];
    }
    for (int i = 0; i < device.size(); ++i) if (!deviceUsed[i]) r.deviceOnly.append(i);
    for (int j = 0; j < analysis.size(); ++j) if (!analysisUsed[j]) r.analysisOnly.append(j);
    return r;
}

} // namespace analysis
