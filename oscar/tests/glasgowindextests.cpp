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

FlowChunk synthSeries(const QString &name, double fs = 25, double peakScale = 1)
{
    FlowChunk c;
    c.start = 1000000;
    c.rateMs = 1000.0 / fs;
    for (const SeriesSpec &sp : kSpecs) {
        if (name == QLatin1String("mixed") || name == QLatin1String(sp.name)) appendSeries(c.samples, sp, fs, peakScale);
    }
    return c;
}

// Our breaths for a synthetic series, as segmentBreaths would mark them: inspiration from the
// first sample of the hump to the first expiratory sample.
QVector<Breath> synthBreaths(const QString &name, const FlowChunk &c)
{
    QVector<Breath> out;
    int offset = 0;
    for (const SeriesSpec &sp : kSpecs) {
        if (name != QLatin1String("mixed") && name != QLatin1String(sp.name)) continue;
        for (int b = 0; b < 60; ++b) {
            int peak = 0;
            for (int k = offset; k < offset + sp.insp; ++k) peak = c.samples[k] > c.samples[peak] ? k : peak;
            Breath br;
            br.start = c.start + qint64(offset * c.rateMs);
            br.inspEnd = br.start + qint64(sp.insp * c.rateMs);
            br.end = br.start + qint64((sp.insp + sp.exp + sp.rest) * c.rateMs);
            float lo = 0, hi = 0;
            for (int k = offset; k < offset + sp.insp + sp.exp + sp.rest; ++k) {
                hi = std::max(hi, c.samples[k]);
                lo = std::min(lo, c.samples[k]);
            }
            br.pif = hi;
            br.pef = lo;
            br.amplitude = hi - lo;
            out << br;
            offset += sp.insp + sp.exp + sp.rest;
        }
    }
    return out;
}

GlasgowCounts adapted(const QString &name, double peakScale = 1, const QVector<Span> &blocked = {})
{
    const FlowChunk c = synthSeries(name, 25, peakScale);
    return glasgowAdapted({ c }, synthBreaths(name, c), blocked).counts;
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
    // Prisma devices record the flow at 10 Hz: a breath of 15 samples must not look skewed or spiky
    for (const char *name : { "normal", "flat", "double", "spike", "skew" }) {
        const double at25 = glasgowOriginal({ synthSeries(QLatin1String(name), 25) }).counts.index();
        for (double fs : { 10.0, 20.0, 50.0 }) {
            const double other = glasgowOriginal({ synthSeries(QLatin1String(name), fs) }).counts.index();
            QVERIFY2(std::fabs(other - at25) <= 0.1, qPrintable(QStringLiteral("%1 at %2 Hz: %3 vs %4").arg(name).arg(fs).arg(other).arg(at25)));
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

void GlasgowIndexTests::testAdaptedMatchesOriginalAt30()
{
    for (const char *name : { "normal", "flat", "double", "spike", "nopause", "fast", "varamp", "skew" }) {
        const GlasgowCounts o = glasgowOriginal({ synthSeries(QLatin1String(name)) }).counts;
        const GlasgowCounts a = adapted(QLatin1String(name));
        QVERIFY2(a.breaths >= 55, name);
        QVERIFY2(std::fabs(a.index() - o.index()) <= 0.05,
                 qPrintable(QStringLiteral("%1: adapted %2, original %3").arg(name).arg(a.index()).arg(o.index())));
        for (int k = 0; k < GiComponentCount; ++k) {
            QVERIFY2(std::fabs(a.fraction(GlasgowComponent(k)) - o.fraction(GlasgowComponent(k))) <= 0.1,
                     qPrintable(QStringLiteral("%1 component %2: %3 vs %4").arg(name).arg(k)
                                .arg(a.fraction(GlasgowComponent(k))).arg(o.fraction(GlasgowComponent(k)))));
        }
    }
}

// The original's thresholds are in L/min: weak breaths look flat, and a small wobble of strong
// breaths looks like unsettled breathing. Relative to the breath's size, neither is.
void GlasgowIndexTests::testAdaptedIgnoresWeakAmplitude()
{
    const double weak = 8.0 / 30;
    QVERIFY(glasgowOriginal({ synthSeries(QStringLiteral("normal"), 25, weak) }).counts.fraction(GiFlatTop) > 0.5);
    QVERIFY(adapted(QStringLiteral("normal"), weak).fraction(GiFlatTop) < 0.2);

    // varamp alternates 24/36 L/min; scaled so the peaks are 57.5/62.5 it becomes a wobble of ±2.5
    FlowChunk c;
    c.start = 1000000;
    c.rateMs = 40;
    const SeriesSpec strong { "strong", "parabola", 0, 40, 60, 25 };
    QVector<Breath> breaths;
    for (int b = 0; b < 60; ++b) {
        const double P = b % 2 ? 62.5 : 57.5;
        Breath br;
        br.start = c.start + qint64(c.samples.size() * c.rateMs);
        for (int k = 0; k < strong.insp; ++k) c.samples << float(round2(shape(QStringLiteral("parabola"), double(k) / strong.insp) * P));
        br.inspEnd = c.start + qint64(c.samples.size() * c.rateMs);
        for (int k = 0; k < strong.exp; ++k) c.samples << float(round2(-0.8 * P * std::sin(M_PI * k / strong.exp)));
        for (int k = 0; k < strong.rest; ++k) c.samples << 0.0f;
        br.end = c.start + qint64(c.samples.size() * c.rateMs);
        br.pif = float(P);
        br.pef = float(-0.8 * P);
        breaths << br;
    }
    QVERIFY(glasgowOriginal({ c }).counts.fraction(GiAmpVar) > 0.5);
    QVERIFY(glasgowAdapted({ c }, breaths, {}).counts.fraction(GiAmpVar) < 0.2);
}

void GlasgowIndexTests::testAdaptedSkipsBlocked()
{
    const FlowChunk c = synthSeries(QStringLiteral("normal"));
    const QVector<Breath> breaths = synthBreaths(QStringLiteral("normal"), c);
    const QVector<Span> blocked { Span { breaths.first().start, breaths[29].end - 1, 0 } };
    const GlasgowResult r = glasgowAdapted({ c }, breaths, blocked);
    QCOMPARE(r.counts.breaths, 30);
    QCOMPARE(r.breaths.size(), 60);
    QVERIFY(!r.breaths.first().counted);
}

void GlasgowIndexTests::testSeries()
{
    const FlowChunk c = synthSeries(QStringLiteral("mixed"));
    const GlasgowResult r = glasgowAdapted({ c }, synthBreaths(QStringLiteral("mixed"), c), {});
    const QVector<TimedValue> series = glasgowSeries(r.breaths);
    QCOMPARE(series.size(), r.counts.breaths);
    QCOMPARE(series.first().t, r.breaths.first().start);
    // 60 normal breaths of 5 s fill the window; the flat block follows them
    const float duringNormal = series[59].v;
    const float duringFlat = series[119].v;
    QVERIFY2(duringFlat > duringNormal + 0.5f, qPrintable(QStringLiteral("%1 vs %2").arg(duringFlat).arg(duringNormal)));
}

void GlasgowIndexTests::testAdaptedSampleRateIndependent()
{
    for (const char *name : { "normal", "flat", "double", "spike", "skew" }) {
        const FlowChunk c25 = synthSeries(QLatin1String(name), 25);
        const double at25 = glasgowAdapted({ c25 }, synthBreaths(QLatin1String(name), c25), {}).counts.index();
        const FlowChunk c10 = synthSeries(QLatin1String(name), 10);
        QVector<Breath> b10 = synthBreaths(QLatin1String(name), c25);   // same times, other samples
        for (Breath &b : b10) b.end = std::min(b.end, c10.start + qint64(c10.samples.size() * c10.rateMs));
        const double at10 = glasgowAdapted({ c10 }, b10, {}).counts.index();
        QVERIFY2(std::fabs(at10 - at25) <= 0.1, qPrintable(QStringLiteral("%1: %2 at 10 Hz vs %3").arg(name).arg(at10).arg(at25)));
    }
}

// Real inspirations often start with a slow low-flow onset. The original only looks at the
// part above its 5 L/min grey zone; the adapted variant must do the same, relative to the peak.
void GlasgowIndexTests::testAdaptedIgnoresSlowOnset()
{
    FlowChunk c;
    c.start = 1000000;
    c.rateMs = 40;
    QVector<Breath> breaths;
    for (int b = 0; b < 60; ++b) {
        Breath br;
        br.start = c.start + qint64(c.samples.size() * c.rateMs);
        for (int k = 0; k < 15; ++k) c.samples << float(round2(2.0 * k / 15));   // 0.6 s creeping up to 2 L/min
        for (int k = 0; k < 40; ++k) c.samples << float(round2(2 + 28 * 4.0 * k / 40 * (1 - k / 40.0)));
        br.inspEnd = c.start + qint64(c.samples.size() * c.rateMs);
        for (int k = 0; k < 60; ++k) c.samples << float(round2(-24 * std::sin(M_PI * k / 60)));
        for (int k = 0; k < 25; ++k) c.samples << 0.0f;
        br.end = c.start + qint64(c.samples.size() * c.rateMs);
        br.pif = 30;
        br.pef = -24;
        breaths << br;
    }
    const GlasgowCounts o = glasgowOriginal({ c }).counts;
    const GlasgowCounts a = glasgowAdapted({ c }, breaths, {}).counts;
    QVERIFY(o.fraction(GiSkew) < 0.1);
    QVERIFY2(a.fraction(GiSkew) < 0.1, qPrintable(QString::number(a.fraction(GiSkew))));
}
