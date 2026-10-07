/* Sleep Analysis Preferences Page
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysisprefs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

#include "analysispanel.h"
#include "SleepLib/profiles.h"

using analysis::AnalysisParams;
using analysis::HypopneaRule;

AnalysisPreferencesPage::AnalysisPreferencesPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(tr("OSCAR can analyse the flow and oximetry of each night itself, next to what "
                                "the device reports: apneas and hypopneas, flow limitation, desaturations, "
                                "time with low SpO2 and pulse rises."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_enabled = new QCheckBox(tr("Analyse nights with OSCAR's own analysis (experimental)"), this);
    m_enabled->setObjectName(QStringLiteral("analysisEnabled"));
    layout->addWidget(m_enabled);
    // how many nights the settings on the page would recalculate; filled in once a counter is set
    m_recalcNote = new QLabel(this);
    m_recalcNote->setObjectName(QStringLiteral("recalculationNote"));
    m_recalcNote->setWordWrap(true);
    m_recalcNote->hide();
    layout->addWidget(m_recalcNote);
    m_recalcTimer = new QTimer(this);
    m_recalcTimer->setSingleShot(true);
    m_recalcTimer->setInterval(300);   // not on every click of a spin box arrow
    connect(m_recalcTimer, &QTimer::timeout, this, &AnalysisPreferencesPage::updateRecalculationNote);

    // hypopnea rule: a selector with what each rule means under it
    auto *ruleBox = new QGroupBox(tr("Hypopnea rule"), this);
    auto *ruleLayout = new QVBoxLayout(ruleBox);
    m_rule = new QComboBox(ruleBox);
    m_rule->setObjectName(QStringLiteral("hypopneaRule"));
    m_rule->addItem(tr("Auto (recommended)"), int(HypopneaRule::Auto));
    m_rule->addItem(tr("AASM 3 %"), int(HypopneaRule::Aasm3));
    m_rule->addItem(tr("CMS 4 %"), int(HypopneaRule::Cms4));
    m_rule->addItem(tr("Flow only"), int(HypopneaRule::FlowOnly));
    for (int i = 0; i < m_rule->count(); ++i) {
        m_rule->setItemData(i, ruleDescription(HypopneaRule(m_rule->itemData(i).toInt())), Qt::ToolTipRole);
    }
    m_ruleText = new QLabel(ruleBox);
    m_ruleText->setWordWrap(true);
    auto *ruleNote = new QLabel(tr("The Analysis panel always shows the hypopnea index under all three rules, "
                                   "so you can compare them on your own nights."), ruleBox);
    ruleNote->setWordWrap(true);
    ruleLayout->addWidget(m_rule);
    ruleLayout->addWidget(m_ruleText);
    ruleLayout->addWidget(ruleNote);
    layout->addWidget(ruleBox);
    connect(m_rule, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { showRuleDescription(); });

    auto *options = new QGroupBox(tr("Options"), this);
    auto *optionLayout = new QFormLayout(options);
    m_limitOxi = new QCheckBox(tr("Limit oximetry metrics to CPAP usage time"), options);
    m_limitOxi->setObjectName(QStringLiteral("limitOxi"));
    m_limitOxi->setToolTip(tr("Off: the whole oximeter recording counts. Desaturations are linked to breathing "
                              "events only where the CPAP ran, either way."));
    m_pulseArousal = new QCheckBox(tr("Count a pulse-rate rise as an arousal (not AASM)"), options);
    m_pulseArousal->setObjectName(QStringLiteral("pulseArousal"));
    m_pulseArousal->setToolTip(tr("A pulse rise at the end of a flow reduction also confirms it as a hypopnea."));
    m_classify = new QCheckBox(tr("Classify apneas as obstructive or central (experimental)"), options);
    m_classify->setObjectName(QStringLiteral("classifyApneas"));
    m_thresholds = new QLineEdit(options);
    m_thresholds->setObjectName(QStringLiteral("spo2Thresholds"));
    m_thresholds->setToolTip(tr("Up to six SpO2 values, separated by commas. Changing them needs no recalculation."));
    optionLayout->addRow(m_limitOxi);
    optionLayout->addRow(m_pulseArousal);
    optionLayout->addRow(m_classify);
    optionLayout->addRow(tr("SpO2 thresholds for \"time below\" (%)"), m_thresholds);
    layout->addWidget(options);

    // every other parameter, as a percentage where it is a fraction
    m_advanced = new QGroupBox(tr("Advanced"), this);
    m_advanced->setObjectName(QStringLiteral("advancedGroup"));
    m_advanced->setCheckable(true);
    m_advanced->setChecked(false);
    auto *advanced = new QFormLayout(m_advanced);
    auto *advancedContent = new QWidget(m_advanced);
    auto *form = new QFormLayout(advancedContent);
    form->setContentsMargins(0, 0, 0, 0);
    advanced->addRow(advancedContent);
    advancedContent->setVisible(false);
    connect(m_advanced, &QGroupBox::toggled, advancedContent, &QWidget::setVisible);

    const QString s = tr(" s"), pct = tr(" %"), bpm = tr(" bpm");
    auto add = [this, form](const char *name, const QString &label, QDoubleSpinBox *box, double *(*field)(AnalysisParams &), double scale) {
        box->setObjectName(QString::fromLatin1(name));   // the key of its explanation
        form->addRow(label, box);
        m_fields.append(Field { box, field, scale });
    };
    using P = AnalysisParams;
    add("apneaReduction", tr("Apnea: flow reduction at least"), number(50, 100, 1, 0, pct), [](P &p) { return &p.flow.apneaReduction; }, 100);
    add("hypopneaReduction", tr("Hypopnea candidate: flow reduction at least"), number(10, 90, 1, 0, pct), [](P &p) { return &p.flow.hypopneaReduction; }, 100);
    add("flowOnlyReduction", tr("Flow only hypopnea: flow reduction at least"), number(10, 90, 1, 0, pct), [](P &p) { return &p.day.flowOnlyReduction; }, 100);
    add("minEventSec", tr("Shortest event"), number(5, 60, 1, 0, s), [](P &p) { return &p.flow.minEventSec; }, 1);
    add("maxEventSec", tr("Longest event (longer is unscoreable)"), number(30, 300, 5, 0, s), [](P &p) { return &p.flow.maxEventSec; }, 1);
    add("baselineWindowSec", tr("Baseline window"), number(30, 600, 10, 0, s), [](P &p) { return &p.flow.baselineWindowSec; }, 1);
    add("baselinePercentile", tr("Baseline percentile"), number(50, 95, 1, 0, QString()), [](P &p) { return &p.flow.baselinePercentile; }, 1);
    add("flThreshold", tr("Flow limitation score of a limited breath"), number(0.1, 0.9, 0.05, 2, QString()), [](P &p) { return &p.flow.flThreshold; }, 1);
    add("linkWindowSec", tr("Desaturation linked to an event ending up to"), number(10, 90, 5, 0, s), [](P &p) { return &p.day.linkWindowSec; }, 1);
    add("desatMinDrop", tr("Desaturation: SpO2 drop at least"), number(2, 10, 1, 0, pct), [](P &p) { return &p.oxi.desatMinDrop; }, 1);
    add("desatMinSec", tr("Desaturation: at least"), number(5, 60, 1, 0, s), [](P &p) { return &p.oxi.desatMinSec; }, 1);
    add("desatMaxFallSec", tr("Desaturation: slowest fall"), number(30, 300, 10, 0, s), [](P &p) { return &p.oxi.desatMaxFallSec; }, 1);
    add("desatMaxSec", tr("Desaturation: longest"), number(60, 600, 10, 0, s), [](P &p) { return &p.oxi.desatMaxSec; }, 1);
    add("pulseRise", tr("Pulse rise at least"), number(3, 30, 1, 0, bpm), [](P &p) { return &p.oxi.pulseRise; }, 1);
    add("bradyBpm", tr("Low pulse below"), number(30, 60, 1, 0, bpm), [](P &p) { return &p.oxi.bradyBpm; }, 1);
    add("tachyBpm", tr("High pulse above"), number(90, 180, 1, 0, bpm), [](P &p) { return &p.oxi.tachyBpm; }, 1);
    add("bradyTachyMinSec", tr("Low or high pulse for at least"), number(10, 300, 5, 0, s), [](P &p) { return &p.oxi.bradyTachyMinSec; }, 1);
    add("zoneLowPct", tr("Problem zones: low SpO2 below"), number(80, 95, 1, 0, pct), [](P &p) { return &p.oxi.zoneLowPct; }, 1);
    add("zoneCriticalPct", tr("Problem zones: critical SpO2 below"), number(70, 92, 1, 0, pct), [](P &p) { return &p.oxi.zoneCriticalPct; }, 1);
    add("zoneWindowSec", tr("Problem zones: window"), number(60, 1200, 30, 0, s), [](P &p) { return &p.oxi.zoneWindowSec; }, 1);
    add("zoneStepSec", tr("Problem zones: window step"), number(10, 300, 10, 0, s), [](P &p) { return &p.oxi.zoneStepSec; }, 1);
    add("zoneLowSec", tr("Problem zones: seconds below the low SpO2"), number(10, 300, 10, 0, s), [](P &p) { return &p.oxi.zoneLowSec; }, 1);
    add("zoneCriticalSec", tr("Problem zones: seconds below the critical SpO2"), number(5, 300, 5, 0, s), [](P &p) { return &p.oxi.zoneCriticalSec; }, 1);
    add("zoneMinSec", tr("Problem zones: shortest zone"), number(30, 1200, 30, 0, s), [](P &p) { return &p.oxi.zoneMinSec; }, 1);
    add("zoneMergeGapSec", tr("Problem zones: merge zones closer than"), number(0, 1200, 30, 0, s), [](P &p) { return &p.oxi.zoneMergeGapSec; }, 1);
    m_zoneMinDesats = number(1, 20, 1, 0, QString());
    m_zoneMinDesats->setObjectName(QStringLiteral("zoneMinDesats"));
    form->addRow(tr("Problem zones: desaturations in a window"), m_zoneMinDesats);
    layout->addWidget(m_advanced);

    auto *reset = new QPushButton(tr("Reset to Defaults"), this);
    reset->setObjectName(QStringLiteral("resetAnalysisDefaults"));
    connect(reset, &QPushButton::clicked, this, [this]() { resetToDefaults(); });
    auto *bottom = new QHBoxLayout();
    bottom->addStretch(1);
    bottom->addWidget(reset);
    layout->addLayout(bottom);

    auto *disclaimer = new QLabel(QStringLiteral("<i>%1</i>").arg(AnalysisPanel::disclaimer().toHtmlEscaped()), this);
    disclaimer->setWordWrap(true);
    layout->addWidget(disclaimer);
    layout->addStretch(1);

    resetToDefaults();

    auto schedule = [this]() { m_recalcTimer->start(); };
    connect(m_enabled, &QCheckBox::toggled, this, schedule);
    connect(m_rule, &QComboBox::currentIndexChanged, this, schedule);
    for (QCheckBox *box : { m_limitOxi, m_pulseArousal, m_classify }) connect(box, &QCheckBox::toggled, this, schedule);
    for (const Field &f : m_fields) connect(f.box, &QDoubleSpinBox::valueChanged, this, schedule);
    connect(m_zoneMinDesats, &QDoubleSpinBox::valueChanged, this, schedule);
}

void AnalysisPreferencesPage::setRecalculationCounter(const std::function<int(const AnalysisParams &)> &counter, bool enabledNow)
{
    m_counter = counter;
    m_enabledNow = enabledNow;
    m_recalcTimer->start();
}

void AnalysisPreferencesPage::updateRecalculationNote()
{
    const AnalysisParams p = params();
    if (!m_counter || !p.enabled) {
        m_recalcNote->hide();
        return;
    }
    const int nights = m_counter(p);
    QString text;
    if (nights == 0) text = tr("No recalculation needed.");
    else if (!m_enabledNow) text = tr("%n night(s) will be analysed.", nullptr, nights);
    else text = tr("With these settings %n night(s) will need recalculation.", nullptr, nights);
    m_recalcNote->setText(text);
    m_recalcNote->setStyleSheet(nights == 0 ? QStringLiteral("color: gray") : QString());
    m_recalcNote->show();
}

QDoubleSpinBox *AnalysisPreferencesPage::number(double min, double max, double step, int decimals, const QString &suffix)
{
    auto *box = new QDoubleSpinBox(this);
    box->setRange(min, max);
    box->setSingleStep(step);
    box->setDecimals(decimals);
    box->setSuffix(suffix);
    return box;
}

QString AnalysisPreferencesPage::ruleDescription(HypopneaRule rule)
{
    switch (rule) {
    case HypopneaRule::Auto:
        return tr("AASM 3 % where the oximeter covers the event, Flow only elsewhere. Good when you wear an "
                  "oximeter only on some nights.");
    case HypopneaRule::Aasm3:
        return tr("Current sleep-lab standard (AASM 2012, rule 1A). A >= 30 % drop in flow for >= 10 s counts as "
                  "a hypopnea only if SpO2 then falls by >= 3 %. Arousals, which a sleep lab also accepts, are "
                  "invisible to OSCAR, so this can count slightly fewer hypopneas than a lab. Needs an oximeter; "
                  "without one, events are scored as Flow only.");
    case HypopneaRule::Cms4:
        return tr("Stricter rule used by US Medicare (AASM rule 1B). Same flow drop, but SpO2 must fall by >= 4 %. "
                  "Gives fewer hypopneas and a lower AHI than AASM 3 % for the same night; use it to compare with "
                  "studies or reports scored this way.");
    case HypopneaRule::FlowOnly:
        break;
    }
    return tr("No oximeter needed. A >= 50 % drop in flow for >= 10 s counts, similar to how CPAP devices score "
              "hypopneas. Cannot tell whether the drop affected oxygen.");
}

void AnalysisPreferencesPage::showRuleDescription()
{
    const QString text = ruleDescription(HypopneaRule(m_rule->currentData().toInt()));
    m_ruleText->setText(text);
    m_rule->setToolTip(text);
}

void AnalysisPreferencesPage::load(const AnalysisParams &p, const QList<double> &thresholds)
{
    AnalysisParams copy = p;
    m_enabled->setChecked(p.enabled);
    m_rule->setCurrentIndex(qMax(0, m_rule->findData(int(p.day.rule))));
    showRuleDescription();
    m_limitOxi->setChecked(p.day.limitOxiToCpap);
    m_pulseArousal->setChecked(p.day.pulseRiseAsArousal);
    m_classify->setChecked(p.flow.classifyApneas);
    QStringList parts;
    for (double t : thresholds) parts << QString::number(t);
    m_thresholds->setText(parts.join(QStringLiteral(", ")));
    for (const Field &f : m_fields) f.box->setValue(*f.field(copy) * f.scale);
    m_zoneMinDesats->setValue(p.oxi.zoneMinDesats);
}

AnalysisParams AnalysisPreferencesPage::params() const
{
    AnalysisParams p;
    p.enabled = m_enabled->isChecked();
    p.day.rule = HypopneaRule(m_rule->currentData().toInt());
    p.day.limitOxiToCpap = m_limitOxi->isChecked();
    p.day.pulseRiseAsArousal = m_pulseArousal->isChecked();
    p.flow.classifyApneas = m_classify->isChecked();
    for (const Field &f : m_fields) *f.field(p) = f.box->value() / f.scale;
    p.oxi.zoneMinDesats = int(m_zoneMinDesats->value());
    return p;
}

QList<double> AnalysisPreferencesPage::spo2Thresholds() const
{
    QList<double> out;
    for (const QString &part : m_thresholds->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const double t = part.trimmed().toDouble(&ok);
        if (ok && t >= 50 && t <= 100 && !out.contains(t)) out.append(t);
    }
    std::sort(out.begin(), out.end(), std::greater<double>());
    while (out.size() > 6) out.removeLast();
    return out;
}

void AnalysisPreferencesPage::resetToDefaults()
{
    load(AnalysisParams(), AnalysisSettings::defaultSpo2Thresholds());
}
