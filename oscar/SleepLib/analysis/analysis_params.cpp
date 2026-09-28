/* Sleep Analysis Parameters
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysis_params.h"

#include <QCryptographicHash>
#include <QStringList>

namespace analysis {

namespace {

// Canonical "key=value;" text of a parameter set, prefixed with the algorithm version
// and the stage, so that equal parameters always hash equally.
class Canon
{
  public:
    explicit Canon(const char *stage) { m_parts << QString("v=%1").arg(kAnalysisAlgoVersion) << stage; }
    Canon &add(const char *key, double value) {
        m_parts << QString("%1=%2").arg(key, QString::number(value, 'g', 12));
        return *this;
    }
    QString hash() const {
        const QByteArray text = m_parts.join(';').toUtf8();
        return QString::fromLatin1(QCryptographicHash::hash(text, QCryptographicHash::Sha1).toHex().left(16));
    }
  private:
    QStringList m_parts;
};

} // namespace

QString AnalysisParams::oxiHash() const
{
    return Canon("oxi")
        .add("desatMinDrop", oxi.desatMinDrop)
        .add("desatMinSec", oxi.desatMinSec)
        .add("desatMaxFallSec", oxi.desatMaxFallSec)
        .add("desatMaxSec", oxi.desatMaxSec)
        .add("pulseRise", oxi.pulseRise)
        .add("bradyBpm", oxi.bradyBpm)
        .add("tachyBpm", oxi.tachyBpm)
        .add("bradyTachyMinSec", oxi.bradyTachyMinSec)
        .add("zoneLowPct", oxi.zoneLowPct)
        .add("zoneCriticalPct", oxi.zoneCriticalPct)
        .add("zoneWindowSec", oxi.zoneWindowSec)
        .add("zoneStepSec", oxi.zoneStepSec)
        .add("zoneMinSec", oxi.zoneMinSec)
        .add("zoneMergeGapSec", oxi.zoneMergeGapSec)
        .add("zoneLowSec", oxi.zoneLowSec)
        .add("zoneCriticalSec", oxi.zoneCriticalSec)
        .add("zoneMinDesats", oxi.zoneMinDesats)
        .hash();
}

QString AnalysisParams::flowHash() const
{
    return Canon("flow")
        .add("apneaReduction", flow.apneaReduction)
        .add("hypopneaReduction", flow.hypopneaReduction)
        .add("minEventSec", flow.minEventSec)
        .add("maxEventSec", flow.maxEventSec)
        .add("baselineWindowSec", flow.baselineWindowSec)
        .add("baselinePercentile", flow.baselinePercentile)
        .add("flThreshold", flow.flThreshold)
        .add("classifyApneas", flow.classifyApneas ? 1 : 0)
        .hash();
}

QString AnalysisParams::dayHash() const
{
    return Canon("day")
        .add("rule", int(day.rule))
        .add("flowOnlyReduction", day.flowOnlyReduction)
        .add("linkWindowSec", day.linkWindowSec)
        .add("limitOxiToCpap", day.limitOxiToCpap ? 1 : 0)
        .add("pulseRiseAsArousal", day.pulseRiseAsArousal ? 1 : 0)
        .hash();
}

} // namespace analysis
