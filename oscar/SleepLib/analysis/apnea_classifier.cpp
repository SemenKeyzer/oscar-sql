/* Sleep Analysis Apnea Classifier
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "apnea_classifier.h"

#include <cmath>

namespace analysis {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Power and amplitude of a Hann-windowed segment at frequency f (direct DFT).
struct Tone { double power; double amplitude; };

Tone toneAt(const QVector<double> &w, const QVector<double> &hann, double fs, double f)
{
    double re = 0, im = 0;
    const int n = w.size();
    for (int i = 0; i < n; ++i) {
        const double a = 2 * kPi * f * i / fs;
        re += w[i] * hann[i] * std::cos(a);
        im -= w[i] * hann[i] * std::sin(a);
    }
    const double mag = std::sqrt(re * re + im * im);
    // Hann coherent gain 0.5: a sine of amplitude A gives |X| = A * n / 4
    return Tone { mag * mag, 4 * mag / n };
}

} // namespace

float cardiogenicOscillationShare(const QVector<float> &flow, double fs, float baseline, float pulseBpm)
{
    if (fs < 10 || baseline <= 0) return -1;
    const int win = int(std::lround(4 * fs));
    const int step = int(std::lround(fs));
    const int edge = int(std::lround(2 * fs));
    if (flow.size() - 2 * edge < win) return -1;

    QVector<double> hann(win);
    for (int i = 0; i < win; ++i) hann[i] = 0.5 - 0.5 * std::cos(2 * kPi * i / (win - 1));
    const double noiseTop = qMin(6.0, fs / 2 - 0.5);
    const bool pulseKnown = hasData(pulseBpm) && pulseBpm > 0;
    const double pulseHz = pulseKnown ? pulseBpm / 60.0 : 0;

    int windows = 0, withOscillation = 0;
    QVector<double> w(win);
    for (int start = edge; start + win <= flow.size() - edge; start += step) {
        double mean = 0;
        for (int i = 0; i < win; ++i) mean += flow[start + i];
        mean /= win;
        for (int i = 0; i < win; ++i) w[i] = flow[start + i] - mean;
        ++windows;

        Tone peak { 0, 0 };
        double peakF = 0;
        for (double f = 0.7; f <= 2.5 + 1e-9; f += 0.05) {
            const Tone t = toneAt(w, hann, fs, f);
            if (t.power > peak.power) { peak = t; peakF = f; }
        }
        double noise = 0;
        int nf = 0;
        for (double f = 3.0; f <= noiseTop + 1e-9; f += 0.25) { noise += toneAt(w, hann, fs, f).power; ++nf; }
        noise = nf ? noise / nf : 0;

        const bool strong = peak.power > 0 && peak.power >= 4 * noise;
        const bool bigEnough = 2 * peak.amplitude >= 0.02 * baseline;   // peak to peak
        const bool atPulse = !pulseKnown || std::fabs(peakF - pulseHz) <= 0.15;
        if (strong && bigEnough && atPulse) ++withOscillation;
    }
    return windows ? float(withOscillation) / windows : -1;
}

float apneaScore(const ApneaEvidence &ev, double flThreshold)
{
    float score = 0;

    // Oscillations of the heart beat carried by the flow: the airway is open.
    const float share = cardiogenicOscillationShare(ev.flow, ev.fs, ev.baseline, ev.pulseBpm);
    if (share >= 0.5f) score -= 1;

    // Flow limitation just before: the airway was narrowing.
    int limited = 0;
    for (const Breath &b : ev.before) {
        if (hasData(b.fl) && b.fl >= flThreshold) ++limited;
    }
    if (limited >= 2) score += 0.5f;

    // An abrupt, large recovery breath.
    if (ev.after && !ev.before.isEmpty()
        && ev.after->amplitude >= 1.2f * ev.baseline && ev.after->amplitude >= 2 * ev.before.last().amplitude) {
        score += 0.5f;
    }

    // Breaths fading away without flow limitation (decrescendo): central drive falling.
    if (ev.before.size() == 3) {
        const Breath &a = ev.before[0], &b = ev.before[1], &c = ev.before[2];
        const bool fading = b.amplitude <= 0.85f * a.amplitude && c.amplitude <= 0.85f * b.amplitude;
        bool unlimited = true;
        for (const Breath &x : ev.before) {
            if (hasData(x.fl) && x.fl >= 0.3f) unlimited = false;
        }
        if (fading && unlimited) score -= 0.5f;
    }

    if (ev.inPeriodicBreathing) score -= 0.5f;

    // The device's own forced-oscillation obstruction estimate (Prisma).
    if (hasData(ev.obstructLevel)) {
        if (ev.obstructLevel >= 50) score += 1;
        else if (ev.obstructLevel <= 20) score -= 1;
    }
    return score;
}

ApneaClass classForScore(float score)
{
    if (score >= 0.75f) return ApneaClass::Obstructive;
    if (score <= -0.75f) return ApneaClass::Central;
    return ApneaClass::Unclassified;
}

} // namespace analysis
