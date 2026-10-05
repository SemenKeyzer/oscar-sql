/* Glasgow Index Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "glasgowindextests.h"

#include <cmath>

#include "SleepLib/analysis/glasgow_index.h"

using namespace analysis;

namespace {

// Synthetic breathing, built exactly as reference/glasgow/golden.js builds it for FlowLimits.js:
// an inspiration of I samples shaped s(x)*P, an expiration of E samples -0.8*P*sin(pi x), R zeros.
struct SeriesSpec { const char *name; const char *shape; double peak; int insp, exp, rest; };
const SeriesSpec kSpecs[] = {
    { "normal", "parabola", 30, 40, 60, 25 },
    { "flat", "flat", 30, 40, 60, 25 },
    { "double", "double", 30, 40, 60, 25 },
    { "spike", "spike", 30, 40, 60, 25 },
    { "nopause", "parabola", 30, 40, 60, 0 },
    { "fast", "parabola", 30, 25, 30, 5 },
    { "varamp", "parabola", 0, 40, 60, 25 },
    { "weak", "flat", 15, 40, 60, 25 },
    { "skew", "skew", 30, 40, 60, 25 },
};

double shape(const QString &s, double x)
{
    if (s == QLatin1String("parabola")) return 4 * x * (1 - x);
    if (s == QLatin1String("flat")) return std::min(1.0, 1.6 * 4 * x * (1 - x));
    if (s == QLatin1String("double")) return std::sin(M_PI * x) * (1 - 0.4 * std::exp(-std::pow((x - 0.5) / 0.1, 2)));
    if (s == QLatin1String("spike")) return 1 - std::fabs(2 * x - 1);
    return std::sin(M_PI * std::pow(x, 0.6));   // skew
}

double round2(double v) { return std::floor(v * 100 + 0.5) / 100; }

int scaled(int n25, double fs) { return int(std::lround(n25 * fs / 25.0)); }

void appendSeries(QVector<float> &out, const SeriesSpec &sp, double fs, double peakScale = 1)
{
    const int I = scaled(sp.insp, fs), E = scaled(sp.exp, fs), R = scaled(sp.rest, fs);
    for (int b = 0; b < 60; ++b) {
        const double P = (QLatin1String(sp.name) == QLatin1String("varamp") ? (b % 2 ? 36 : 24) : sp.peak) * peakScale;
        for (int k = 0; k < I; ++k) out << float(round2(shape(QLatin1String(sp.shape), double(k) / I) * P));
        for (int k = 0; k < E; ++k) out << float(round2(-0.8 * P * std::sin(M_PI * k / E)));
        for (int k = 0; k < R; ++k) out << 0.0f;
    }
}

FlowChunk synthSeries(const QString &name, double fs = 25)
{
    FlowChunk c;
    c.start = 1000000;
    c.rateMs = 1000.0 / fs;
    for (const SeriesSpec &sp : kSpecs) {
        if (name == QLatin1String("mixed") || name == QLatin1String(sp.name)) appendSeries(c.samples, sp, fs);
    }
    return c;
}

// FlowLimits.js's answers for these series (reference/glasgow/golden.json):
// breaths, then skew, spike, flatTop, topHeavy, multiPeak, noPause, inspirRate, multiBreath, ampVar, overall
struct Golden { const char *name; int breaths; double v[10]; };
const Golden kGolden[] = {
    { "normal", 60, { 0, 0, 0, 0, 0, 0, 0, 0.02, 0, 0.02 } },
    { "flat", 60, { 0, 0, 1, 1, 0, 0, 0, 0.02, 0, 1.02 } },
    { "double", 60, { 0, 0, 0, 0, 1, 0, 0, 0.02, 0, 1.02 } },
    { "spike", 60, { 0, 1, 0, 0, 0, 0, 0, 0.02, 0, 1.02 } },
    { "nopause", 60, { 0, 0, 0, 0, 0, 0.98, 0, 0.02, 0, 1 } },
    { "fast", 60, { 0, 0, 0, 0, 0, 0.97, 0.9, 0.02, 0, 1.89 } },
    { "varamp", 60, { 0, 0, 0, 0, 0, 0, 0, 0.02, 0.9, 0.92 } },
    { "weak", 60, { 0, 0, 1, 1, 0, 0, 0, 0.02, 0, 1.02 } },
    { "skew", 59, { 1, 0, 0, 0, 0, 0, 0, 0, 0, 1 } },
    { "mixed", 540, { 0.11, 0.11, 0.22, 0.22, 0.11, 0.22, 0.11, 0, 0.13, 1.01 } },
};

} // namespace

void GlasgowIndexTests::testMatchesFlowLimitsJs_data()
{
    QTest::addColumn<int>("index");
    for (int i = 0; i < int(sizeof(kGolden) / sizeof(kGolden[0])); ++i) QTest::newRow(kGolden[i].name) << i;
}

// FlowLimits.js rounds each fraction to 2 decimals and sums the rounded ones.
void GlasgowIndexTests::testMatchesFlowLimitsJs()
{
    QFETCH(int, index);
    const Golden &g = kGolden[index];
    const GlasgowCounts c = glasgowOriginal({ synthSeries(QLatin1String(g.name)) }).counts;
    QCOMPARE(c.breaths, g.breaths);
    double overall = 0;
    for (int k = 0; k < GiComponentCount; ++k) {
        const double f = round2(c.fraction(GlasgowComponent(k)));
        QVERIFY2(std::fabs(f - g.v[k]) < 1e-9, qPrintable(QStringLiteral("component %1: %2 vs %3").arg(k).arg(f).arg(g.v[k])));
        if (k != GiTopHeavy) overall += f;
    }
    QCOMPARE(round2(overall), g.v[9]);
}

void GlasgowIndexTests::testEachComponent()
{
    auto frac = [](const char *name, GlasgowComponent k) {
        return glasgowOriginal({ synthSeries(QLatin1String(name)) }).counts.fraction(k);
    };
    QVERIFY(frac("flat", GiFlatTop) >= 0.9);
    QVERIFY(frac("double", GiMultiPeak) >= 0.9);
    QVERIFY(frac("spike", GiSpike) >= 0.9);
    QVERIFY(frac("nopause", GiNoPause) >= 0.9);
    QVERIFY(frac("fast", GiInspirRate) >= 0.8);
    QVERIFY(frac("varamp", GiAmpVar) >= 0.8);
    QVERIFY(frac("skew", GiSkew) >= 0.9);
    for (GlasgowComponent k : { GiSkew, GiSpike, GiFlatTop, GiMultiPeak, GiNoPause, GiInspirRate, GiAmpVar }) {
        QVERIFY(frac("normal", k) < 0.1);
    }
}

void GlasgowIndexTests::testSampleRateIndependent()
{
    for (const char *name : { "normal", "flat", "double" }) {
        const double at25 = glasgowOriginal({ synthSeries(QLatin1String(name), 25) }).counts.index();
        for (double fs : { 20.0, 50.0 }) {
            const double other = glasgowOriginal({ synthSeries(QLatin1String(name), fs) }).counts.index();
            QVERIFY2(std::fabs(other - at25) <= 0.02, qPrintable(QStringLiteral("%1 at %2 Hz: %3 vs %4").arg(name).arg(fs).arg(other).arg(at25)));
        }
    }
}

void GlasgowIndexTests::testShortChunks()
{
    FlowChunk tiny;
    tiny.rateMs = 40;
    tiny.samples = QVector<float>(10, 20.0f);
    const GlasgowResult r = glasgowOriginal({ tiny, FlowChunk() });
    QCOMPARE(r.counts.breaths, 0);
    QVERIFY(std::isnan(r.counts.index()));
    QVERIFY(glasgowOriginal({}).counts.isEmpty());
}

void GlasgowIndexTests::testCountsText()
{
    GlasgowCounts a;
    a.breaths = 100;
    for (int k = 0; k < GiComponentCount; ++k) a.flagged[k] = k * 3;
    QVERIFY(GlasgowCounts::fromText(a.breaths, a.toText()) == a);
    QVERIFY(GlasgowCounts().toText().isEmpty());
    GlasgowCounts b = a;
    b += a;
    QCOMPARE(b.breaths, 200);
    QCOMPARE(b.flagged[GiAmpVar], 2 * a.flagged[GiAmpVar]);
}
