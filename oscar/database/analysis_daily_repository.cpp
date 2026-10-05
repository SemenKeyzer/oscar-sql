/* Analysis Daily Repository
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysis_daily_repository.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariantMap>

#include "database_manager.h"

namespace {

QString histToJson(const QVector<int> &hist)
{
    QJsonArray a;
    for (int v : hist) a.append(v);
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

QVector<int> histFromJson(const QString &json)
{
    QVector<int> out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json.toUtf8()).array()) out.append(v.toInt());
    return out;
}

// Column -> value; groups that do not apply are NULL.
QVariantMap toColumns(const AnalysisDailyData &d)
{
    QVariantMap c;
    c["profile_id"] = d.profileId;
    c["date"] = d.date.toString(Qt::ISODate);
    c["algo_version"] = d.algoVersion;
    c["params_hash"] = d.paramsHash;
    c["inputs_hash"] = d.inputsHash;
    c["computed_at"] = (d.computedAt.isValid() ? d.computedAt : QDateTime::currentDateTime()).toString(Qt::ISODate);

    auto put = [&c](bool present, const char *name, const QVariant &value) { c[name] = present ? value : QVariant(); };
    const bool f = d.hasFlow;
    put(f, "flow_s", d.flowSeconds);
    put(f, "flow_rate_hz", d.flowRateHz);
    put(f, "unscoreable_s", d.unscoreableSeconds);
    put(f, "n_oa", d.nObstructiveApnea);
    put(f, "n_ca", d.nCentralApnea);
    put(f, "n_a", d.nApnea);
    put(f, "n_oh", d.nObstructiveHypopnea);
    put(f, "n_ch", d.nCentralHypopnea);
    put(f, "n_h", d.nHypopnea);
    put(f, "n_rera", d.nRera);
    put(f, "n_unconfirmable", d.nUnconfirmable);
    put(f, "n_h_aasm3", d.nHypopneaAasm3);
    put(f, "n_h_cms4", d.nHypopneaCms4);
    put(f, "n_h_flow", d.nHypopneaFlow);
    put(f, "fl_time_s", d.flSeconds);
    put(f, "fl_sum", d.flSum);
    put(f, "n_fl_breaths", d.flBreaths);
    put(f && d.hasFlRuns, "fl_limited_breaths", d.flLimitedBreaths);
    put(f && d.hasFlRuns, "fl_longest_s", d.flLongestSeconds);
    put(f && !d.glasgow.isEmpty(), "gi_breaths", d.glasgow.breaths);
    put(f && !d.glasgow.isEmpty(), "gi_counts", d.glasgow.toText());
    put(f && !d.glasgowAdapted.isEmpty(), "gia_breaths", d.glasgowAdapted.breaths);
    put(f && !d.glasgowAdapted.isEmpty(), "gia_counts", d.glasgowAdapted.toText());
    put(f, "pb_time_s", d.pbSeconds);
    put(f, "hypopnea_rule", d.hypopneaRule);

    const bool k = d.hasComparison;
    put(k, "dev_apnea", d.devApnea);
    put(k, "dev_hypopnea", d.devHypopnea);
    put(k, "dev_rera", d.devRera);
    put(k, "cmp_matched", d.cmpMatched);
    put(k, "cmp_device_only", d.cmpDeviceOnly);
    put(k, "cmp_analysis_only", d.cmpAnalysisOnly);
    put(k, "cmp_type_mismatch", d.cmpTypeMismatch);

    const bool o = d.hasOximetry;
    put(o, "oxi_s", d.oxiSeconds);
    put(o, "oxi_scope", d.oxiScope);
    put(o, "oxi_source", d.oxiSource);
    c["has_cpap"] = d.hasCpap ? 1 : 0;
    put(o, "n_desat3", d.nDesat3);
    put(o, "n_desat4", d.nDesat4);
    put(o, "spo2_hist", histToJson(d.spo2Hist));
    put(o, "spo2_sum", d.spo2Sum);
    put(o, "spo2_median", d.spo2Median);
    put(o, "spo2_nadir", d.spo2Nadir);
    put(o, "desat_area", d.desatArea);
    put(o && d.hasCpap, "linked_desat_area", d.linkedDesatArea);
    put(o && d.hasCpap, "n_unexplained_desat", d.nUnexplainedDesat);
    put(o, "n_cyclic", d.nCyclic);
    put(o, "cyclic_s", d.cyclicSeconds);
    put(o, "n_zones", d.nZones);
    put(o, "zone_s", d.zoneSeconds);
    put(o, "zone_severe_s", d.zoneSevereSeconds);

    const bool p = d.hasPulse;
    put(p, "pulse_s", d.pulseSeconds);
    put(p, "pulse_sum", d.pulseSum);
    put(p, "pulse_sq_sum", d.pulseSqSum);
    put(p, "pulse_min", d.pulseMin);
    put(p, "pulse_max", d.pulseMax);
    put(p, "pulse_hist", histToJson(d.pulseHist));
    put(p, "n_pulse_rise", d.nPulseRise);
    put(p, "dhr_sum", d.dhrSum);
    put(p, "n_dhr", d.nDhr);
    put(p, "brady_s", d.bradySeconds);
    put(p, "tachy_s", d.tachySeconds);

    put(d.hasOffsetHint, "oxi_offset_hint_ms", d.oxiOffsetHintMs);
    c["extra_json"] = d.extraJson.isEmpty() ? QVariant() : QVariant(d.extraJson);
    return c;
}

AnalysisDailyData fromRecord(const QSqlRecord &r)
{
    AnalysisDailyData d;
    auto i = [&r](const char *n) { return r.value(n).toInt(); };
    auto f = [&r](const char *n) { return r.value(n).toDouble(); };
    d.id = r.value("id").toLongLong();
    d.profileId = r.value("profile_id").toLongLong();
    d.date = QDate::fromString(r.value("date").toString(), Qt::ISODate);
    d.algoVersion = i("algo_version");
    d.paramsHash = r.value("params_hash").toString();
    d.inputsHash = r.value("inputs_hash").toString();
    d.computedAt = QDateTime::fromString(r.value("computed_at").toString(), Qt::ISODate);

    d.hasFlow = !r.isNull("flow_s");
    d.flowSeconds = i("flow_s");
    d.flowRateHz = f("flow_rate_hz");
    d.unscoreableSeconds = i("unscoreable_s");
    d.nObstructiveApnea = i("n_oa");
    d.nCentralApnea = i("n_ca");
    d.nApnea = i("n_a");
    d.nObstructiveHypopnea = i("n_oh");
    d.nCentralHypopnea = i("n_ch");
    d.nHypopnea = i("n_h");
    d.nRera = i("n_rera");
    d.nUnconfirmable = i("n_unconfirmable");
    d.nHypopneaAasm3 = i("n_h_aasm3");
    d.nHypopneaCms4 = i("n_h_cms4");
    d.nHypopneaFlow = i("n_h_flow");
    d.flSeconds = i("fl_time_s");
    d.flSum = f("fl_sum");
    d.flBreaths = i("n_fl_breaths");
    d.hasFlRuns = !r.isNull("fl_limited_breaths");
    d.flLimitedBreaths = i("fl_limited_breaths");
    d.flLongestSeconds = i("fl_longest_s");
    d.glasgow = analysis::GlasgowCounts::fromText(i("gi_breaths"), r.value("gi_counts").toString());
    d.glasgowAdapted = analysis::GlasgowCounts::fromText(i("gia_breaths"), r.value("gia_counts").toString());
    d.pbSeconds = i("pb_time_s");
    d.hypopneaRule = i("hypopnea_rule");

    d.hasComparison = !r.isNull("cmp_matched");
    d.devApnea = i("dev_apnea");
    d.devHypopnea = i("dev_hypopnea");
    d.devRera = i("dev_rera");
    d.cmpMatched = i("cmp_matched");
    d.cmpDeviceOnly = i("cmp_device_only");
    d.cmpAnalysisOnly = i("cmp_analysis_only");
    d.cmpTypeMismatch = i("cmp_type_mismatch");

    d.hasOximetry = !r.isNull("oxi_s");
    d.oxiSeconds = i("oxi_s");
    d.oxiScope = r.value("oxi_scope").toString();
    d.oxiSource = r.value("oxi_source").toString();
    d.hasCpap = i("has_cpap") != 0;
    d.nDesat3 = i("n_desat3");
    d.nDesat4 = i("n_desat4");
    d.spo2Hist = histFromJson(r.value("spo2_hist").toString());
    d.spo2Sum = f("spo2_sum");
    d.spo2Median = f("spo2_median");
    d.spo2Nadir = f("spo2_nadir");
    d.desatArea = f("desat_area");
    d.linkedDesatArea = f("linked_desat_area");
    d.nUnexplainedDesat = i("n_unexplained_desat");
    d.nCyclic = i("n_cyclic");
    d.cyclicSeconds = i("cyclic_s");
    d.nZones = i("n_zones");
    d.zoneSeconds = i("zone_s");
    d.zoneSevereSeconds = i("zone_severe_s");

    d.hasPulse = !r.isNull("pulse_s");
    d.pulseSeconds = i("pulse_s");
    d.pulseSum = f("pulse_sum");
    d.pulseSqSum = f("pulse_sq_sum");
    d.pulseMin = f("pulse_min");
    d.pulseMax = f("pulse_max");
    d.pulseHist = histFromJson(r.value("pulse_hist").toString());
    d.nPulseRise = i("n_pulse_rise");
    d.dhrSum = f("dhr_sum");
    d.nDhr = i("n_dhr");
    d.bradySeconds = i("brady_s");
    d.tachySeconds = i("tachy_s");

    d.hasOffsetHint = !r.isNull("oxi_offset_hint_ms");
    d.oxiOffsetHintMs = r.value("oxi_offset_hint_ms").toLongLong();
    d.extraJson = r.value("extra_json").toString();
    return d;
}

bool exec(QSqlQuery &q, const char *where)
{
    if (q.exec()) return true;
    qCritical() << "AnalysisDailyRepository:" << where << "failed:" << q.lastError().text();
    DatabaseManager::instance().checkQueryError(QString("AnalysisDailyRepository::") + where, q);
    return false;
}

} // namespace

bool AnalysisDailyRepository::upsert(const AnalysisDailyData &data)
{
    const QVariantMap cols = toColumns(data);
    const QStringList names = cols.keys();
    QStringList marks;
    for (int k = 0; k < names.size(); ++k) marks << "?";
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QString("INSERT OR REPLACE INTO analysis_daily (%1) VALUES (%2)").arg(names.join(", "), marks.join(", ")));
    for (const QString &n : names) q.addBindValue(cols.value(n));
    return exec(q, "upsert");
}

AnalysisDailyData AnalysisDailyRepository::find(qint64 profileId, const QDate &date)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare("SELECT * FROM analysis_daily WHERE profile_id = ? AND date = ?");
    q.addBindValue(profileId);
    q.addBindValue(date.toString(Qt::ISODate));
    if (!exec(q, "find") || !q.next()) return AnalysisDailyData();
    return fromRecord(q.record());
}

QList<AnalysisDailyData> AnalysisDailyRepository::findRange(qint64 profileId, const QDate &from, const QDate &to)
{
    QList<AnalysisDailyData> out;
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare("SELECT * FROM analysis_daily WHERE profile_id = ? AND date >= ? AND date <= ? ORDER BY date");
    q.addBindValue(profileId);
    q.addBindValue(from.toString(Qt::ISODate));
    q.addBindValue(to.toString(Qt::ISODate));
    if (!exec(q, "findRange")) return out;
    while (q.next()) out.append(fromRecord(q.record()));
    return out;
}

bool AnalysisDailyRepository::remove(qint64 profileId, const QDate &date)
{
    return removeRange(profileId, date, date);
}

bool AnalysisDailyRepository::removeRange(qint64 profileId, const QDate &from, const QDate &to)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare("DELETE FROM analysis_daily WHERE profile_id = ? AND date >= ? AND date <= ?");
    q.addBindValue(profileId);
    q.addBindValue(from.toString(Qt::ISODate));
    q.addBindValue(to.toString(Qt::ISODate));
    return exec(q, "removeRange");
}
