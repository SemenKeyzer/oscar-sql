/* Glasgow Index
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * glasgowOriginal() is a port of FlowLimits.js of the Glasgow Index,
 * Copyright 2025 DaveSkvn, https://github.com/DaveSkvn/GlasgowIndex, distributed under the
 * GNU General Public License version 3 or (at your option) any later version.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "glasgow_index.h"

#include <QStringList>
#include <cmath>
#include <limits>

namespace analysis {

double GlasgowCounts::fraction(GlasgowComponent c) const
{
    if (breaths <= 0) return std::numeric_limits<double>::quiet_NaN();
    return double(flagged[c]) / breaths;
}

double GlasgowCounts::index() const
{
    if (breaths <= 0) return std::numeric_limits<double>::quiet_NaN();
    double sum = 0;
    for (int c = 0; c < GiComponentCount; ++c) {
        if (c != GiTopHeavy) sum += fraction(GlasgowComponent(c));
    }
    return sum;
}

GlasgowCounts &GlasgowCounts::operator+=(const GlasgowCounts &o)
{
    breaths += o.breaths;
    for (int c = 0; c < GiComponentCount; ++c) flagged[c] += o.flagged[c];
    return *this;
}

QString GlasgowCounts::toText() const
{
    if (breaths <= 0) return QString();
    QStringList parts;
    for (int n : flagged) parts << QString::number(n);
    return parts.join(QLatin1Char(','));
}

GlasgowCounts GlasgowCounts::fromText(int breaths, const QString &text)
{
    GlasgowCounts c;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (breaths <= 0 || parts.size() != GiComponentCount) return c;
    c.breaths = breaths;
    for (int i = 0; i < GiComponentCount; ++i) c.flagged[i] = parts[i].toInt();
    return c;
}

namespace {

// JavaScript's Math.round: halves go up.
double jsRound(double v) { return std::floor(v + 0.5); }

// One inspiration as FlowLimits.js describes it (indices into the joined samples).
struct Inspiration {
    int start = 0, end = 0, midPoint = 0;
    double maxValue = 0;
    double leftPercent = 50, top90Percent = 32, midVar = 0;
    bool multiPeak = false;
    bool noExhale = false;
    bool hasPreRest = false;   // JS leaves preRest undefined for an inspiration without a minimum
    double preRest = 0;
    bool hasAmpVar = false;    // ... and ampVar / inspirPerMin for the first 5 and the last
    double ampVar = 0;
    double inspirPerMin = 0;
};

} // namespace

// FlowLimits.js: findMins, findInspirations, calcCycleBasedIndicators, inspirationAmplitude,
// prepIndices. Constants in samples are the author's at 25 Hz, scaled by n().
GlasgowResult glasgowOriginal(const QVector<FlowChunk> &chunks)
{
    GlasgowResult result;
    QVector<double> y;
    QVector<qint64> t;
    double rateMs = 0;
    for (const FlowChunk &c : chunks) {   // formDataArray: the flow signals one after another
        if (c.samples.isEmpty() || c.rateMs <= 0) continue;
        if (rateMs <= 0) rateMs = c.rateMs;
        for (int i = 0; i < c.samples.size(); ++i) {
            y << c.samples[i];
            t << c.start + qint64(std::llround(i * c.rateMs));
        }
    }
    if (rateMs <= 0) return result;
    const double fs = 1000.0 / rateMs;
    auto n = [fs](int n25) { return std::max(1, int(std::lround(n25 * fs / 25.0))); };
    const int minWindow = n(25), minInspiration = n(8), shortInspiration = n(12);
    const int extrapolation = n(25), pause = n(10);
    const double greyLower = -10, greyUpper = 5, minPeakBump = 1;
    const int len = y.size();

    // findMins: the lowest sample within a second either side, below the grey zone
    QVector<char> isMin(len, 0);
    for (int ptr = minWindow; ptr < len - minWindow; ++ptr) {
        bool minDetected = true;
        for (int w = ptr - minWindow; w < ptr + minWindow - 1; ++w) {
            if (y[w] < y[ptr]) { minDetected = false; break; }
        }
        isMin[ptr] = minDetected && y[ptr] < greyLower;
    }
    for (int ptr = std::max(0, len - minWindow - 1); ptr < len - 1; ++ptr) isMin[ptr] = 0;

    // findInspirations
    QVector<Inspiration> insp;
    int ignoreUntil = 0;
    for (int i = 0; i < len - 1; ++i) {
        if (i < ignoreUntil || y[i] <= greyUpper || i == 0) continue;
        if (y[i - 1] > y[i] || y[i] < y[i + 1]) continue;
        int start = -1, end = -1;
        for (int d = i; d > 0; --d) {
            if (y[d] > y[i]) break;
            if (y[d] <= greyUpper) { start = d; break; }
        }
        if (start < 0) continue;
        for (int u = i; u < len - 1; ++u) {
            if (y[u] > y[i]) break;
            if (y[u] <= greyUpper) { end = u; break; }
        }
        if (end < 0 || end - start < minInspiration) continue;

        Inspiration in;
        in.start = start;
        in.end = end;
        in.maxValue = y[i];
        in.midPoint = start + int(jsRound((end - start) / 2.0));
        double leftVol = 0, rightVol = 0;
        int top90 = 0;
        const double threshold90 = in.maxValue * 0.9;
        bool firstPeakFound = false, lookingForNextPeak = false;
        double lastMax = 0, lowestPostFirstPeak = 0;
        for (int ptr = start; ptr < end; ++ptr) {
            if (ptr < in.midPoint) leftVol += y[ptr];
            else if (ptr > in.midPoint) rightVol += y[ptr];
            if (y[ptr] > threshold90) ++top90;
            if (!firstPeakFound) {
                if (y[ptr] > lastMax) lastMax = y[ptr];
                else if (y[ptr] < lastMax) firstPeakFound = true;
            } else if (!lookingForNextPeak && (lastMax - y[ptr]) > minPeakBump) {
                lookingForNextPeak = true;
                lowestPostFirstPeak = y[ptr];
            }
            if (lookingForNextPeak && !in.multiPeak) {   // a separate "if" in the original
                if (y[ptr] < lowestPostFirstPeak) lowestPostFirstPeak = y[ptr];
                else if (y[ptr] > lowestPostFirstPeak + minPeakBump) in.multiPeak = true;
            }
        }
        if (end - start > shortInspiration) {
            in.leftPercent = jsRound(10000 * leftVol / (leftVol + rightVol)) / 100;
            in.top90Percent = jsRound(10000.0 * top90 / (end - start)) / 100;
        }
        const int varStart = int(jsRound(in.midPoint - 0.25 * (end - start)));
        const int varEnd = int(jsRound(in.midPoint + 0.25 * (end - start)));
        const double half = 0.5 * (end - start);
        double midSum = 0;
        for (int ptr = varStart; ptr < varEnd; ++ptr) midSum += y[ptr];
        const double midMean = midSum / half;
        double midVar = 0;
        for (int ptr = varStart; ptr < varEnd; ++ptr) midVar += (midMean - y[ptr]) * (midMean - y[ptr]);
        in.midVar = jsRound(100 * midVar / half) / 100;
        insp << in;
        ignoreUntil = end;
    }

    // calcCycleBasedIndicators: link each inspiration to the expiration peak before it
    QVector<int> mins;
    for (int i = 0; i < len - 1; ++i) {
        if (isMin[i]) mins << i;
    }
    int next = 0;
    for (int i = 0; i < mins.size() - 1; ++i) {
        const int indexOfMin = mins[i];
        if (next >= insp.size()) break;
        int emergencyBreak = 10;
        do {
            if (emergencyBreak-- <= 0) break;
            Inspiration &in = insp[next];
            if (in.start < indexOfMin) {
                in.noExhale = true;   // two inspirations for one expiration
                ++next;
            } else if (in.start > mins[i + 1]) {
                break;
            } else if (in.start > indexOfMin) {
                in.noExhale = false;
                const double minValue = y[indexOfMin];
                const double oneSecondLater = indexOfMin + extrapolation < len ? y[indexOfMin + extrapolation] : 0;
                in.hasPreRest = true;
                if (oneSecondLater < 0) {
                    const int intersection = indexOfMin + int(jsRound(extrapolation * minValue / (minValue - oneSecondLater)));
                    in.preRest = in.start - intersection;
                } else {
                    in.preRest = -10;
                }
                ++next;
                break;
            }
        } while (next < insp.size() - 1);
    }

    // inspirationAmplitude
    const int ampWindow = 5;
    for (int i = ampWindow; i < insp.size() - 1; ++i) {
        double mean = 0;
        for (int k = 0; k < ampWindow; ++k) mean += insp[i - k].maxValue;
        mean /= ampWindow;
        double var = 0;
        for (int k = 0; k < ampWindow; ++k) var += (insp[i - k].maxValue - mean) * (insp[i - k].maxValue - mean);
        insp[i].hasAmpVar = true;
        insp[i].ampVar = jsRound(100 * var / ampWindow) / 100;
        const int samples = insp[i].start - insp[i - ampWindow].start;
        insp[i].inspirPerMin = jsRound(ampWindow * 60 * 1000 / (samples * rateMs));
    }

    // prepIndices
    for (const Inspiration &in : insp) {
        GlasgowBreath b;
        b.start = t[in.start];
        b.flags[GiSkew] = in.leftPercent < 45 || in.leftPercent > 55;
        b.flags[GiTopHeavy] = in.top90Percent > 40;
        b.flags[GiFlatTop] = in.midVar < 0.75;
        b.flags[GiSpike] = in.top90Percent < 20;
        b.flags[GiMultiPeak] = in.multiPeak;
        b.flags[GiNoPause] = in.hasPreRest && in.preRest < pause;
        b.flags[GiInspirRate] = in.hasAmpVar && in.inspirPerMin > 20;
        b.flags[GiMultiBreath] = in.noExhale;
        b.flags[GiAmpVar] = in.hasAmpVar && in.ampVar > 4;
        for (int k = 0; k < GiComponentCount; ++k) result.counts.flagged[k] += b.flags[k];
        result.breaths << b;
    }
    result.counts.breaths = insp.size();
    return result;
}

} // namespace analysis
