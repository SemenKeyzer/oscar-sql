/* Glossary
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "glossary.h"

#include <QCoreApplication>
#include <QHash>

#include "SleepLib/schema.h"

namespace {

#define T(s) QT_TRANSLATE_NOOP("Glossary", s)

struct Raw {
    const char *key, *term, *expansion, *summary, *details, *norm;
    bool experimental;
    const char *seeAlso;   // keys, comma separated
    const char *channels;  // channel codes, comma separated
};

// clang-format off
const Raw kEntries[] = {
    // ---- therapy
    { "usage", T("Usage"), T("hours with the mask on"),
      T("How long the device ran with the mask on that night."),
      T("Counted from the sessions the device recorded; short breaks with the mask off are not included. Several sessions in a night are added up."),
      T("4 hours or more a night is the usual compliance target; 7 hours or more gives the most benefit."), false, "compliance,sessions,mask_off", "" },
    { "compliance", T("Compliance"), T("nights with enough usage"),
      T("Whether the night reached the usage target set in the profile (4 hours by default)."),
      T("Insurers and doctors often count compliance as the share of nights with at least 4 hours of use, for example over any 30 days."),
      T("At least 70% of nights with 4 hours or more is the common requirement."), false, "usage,compliance_pct", "" },
    { "sessions", T("Session"), T("one stretch of therapy"),
      T("A continuous stretch from switching the device on (or putting the mask on) to switching it off."),
      T("Taking the mask off at night usually starts a new session. Many short sessions can mean sleep is broken or the mask is uncomfortable."),
      "", false, "usage,mask_off", "" },
    { "mask_off", T("Mask off"), T("time without the mask"),
      T("Breaks during the night when the device detected no mask on the face."),
      T("These breaks are not therapy time and are not counted as usage or in the indices."),
      "", false, "usage,sessions", "" },
    { "ahi", T("AHI"), T("Apnea-Hypopnea Index"),
      T("Apneas and hypopneas per hour of sleep with the device, as the device counted them."),
      T("The main measure of how well therapy keeps the airway open. The device counts events from the flow; its rules differ from a sleep lab's, and without an oximeter it cannot confirm hypopneas by a drop in oxygen."),
      T("Below 5 an hour is good; 5–15 mild, 15–30 moderate, over 30 severe (for an untreated person)."), false, "oai,cai,hi,rera,an_ahi,rdi", "" },
    { "rdi", T("RDI"), T("Respiratory Disturbance Index"),
      T("Like AHI, but also counts RERAs: events of extra breathing effort that do not reach a hypopnea."),
      T("RDI is AHI plus RERA per hour. Some devices do not record RERAs, then RDI equals AHI."),
      T("Below 5 an hour is a good result on therapy."), false, "ahi,rera", "" },
    { "oai", T("OAI"), T("Obstructive Apnea Index"),
      T("Obstructive apneas per hour: the airway closed while the breathing effort went on."),
      T("An obstructive apnea is a stop of airflow of 10 seconds or more with effort to breathe. Many of them on therapy usually mean the pressure is too low or the mask leaks."),
      T("Close to 0 on good therapy."), false, "ahi,cai,uai", "Obstructive" },
    { "cai", T("CAI"), T("Central (clear airway) Apnea Index"),
      T("Central apneas per hour: breathing stopped with the airway open, without effort."),
      T("The device tells them apart by small pressure pulses. A few are normal, for example while falling asleep; many may be caused by too much pressure or need a doctor's attention."),
      T("Below 5 an hour is usually fine."), false, "ahi,oai,csr", "ClearAirway" },
    { "uai", T("UAI"), T("Unclassified Apnea Index"),
      T("Apneas the device could not tell as obstructive or central."),
      T("Counted in AHI. Some devices record them when the airway test was not possible, for example with a large leak."),
      "", false, "ahi,oai,cai", "Apnea" },
    { "all_apnea", T("All apneas"), T("apneas of any kind"),
      T("All apneas the device recorded, without telling their kind."),
      T("Some devices record only this total."), "", false, "ahi,oai,cai", "AllApnea" },
    { "hi", T("HI"), T("Hypopnea Index"),
      T("Hypopneas per hour: breathing shallower by at least 30% for 10 seconds or more."),
      T("In a sleep lab a hypopnea also needs an oxygen drop of 3% (or 4%) or an arousal. A device sees only the flow, so its count may differ."),
      T("Together with apneas below 5 an hour."), false, "ahi,oh,ch,hypopnea_rule", "Hypopnea" },
    { "oh", T("OH"), T("obstructive hypopnea"),
      T("A hypopnea with signs of a narrowed airway."), T("Recorded by devices that classify hypopneas (and by OSCAR's analysis)."),
      "", false, "hi,ch", "ObstructiveHypopnea" },
    { "ch", T("CH"), T("central hypopnea"),
      T("A hypopnea from reduced breathing effort with an open airway."), T("Recorded by devices that classify hypopneas (and by OSCAR's analysis)."),
      "", false, "hi,oh", "CentralHypopnea" },
    { "rera", T("RERA"), T("Respiratory Effort Related Arousal"),
      T("Breaths getting harder and flatter until the sleeper partly wakes, without a full apnea or hypopnea."),
      T("A sign of a partly narrowed airway. Many RERAs with a low AHI can still leave you tired."),
      T("A few an hour are common; many may need more pressure."), false, "rdi,fl_device,fl_time", "RERA" },
    { "fl_device", T("FL"), T("flow limitation event (device)"),
      T("A stretch where the device saw flattened inspirations: the airway partly narrowed."),
      T("Devices mark it differently; it is not counted in AHI."), "", false, "flg,fl_time,rera", "FlowLimit" },
    { "flg", T("Flow limitation"), T("the device's flow limitation level"),
      T("How flattened the inspirations look to the device, from 0 (normal) to 1 (strongly limited)."),
      T("Shown by ResMed and some other devices about twice a second. Short peaks are common; long stretches above 0.3–0.5 suggest the airway is not fully held open."),
      T("Mostly 0; the 95th percentile ideally below about 0.3 (guide)."), false, "fl_device,fl_time,fl_score", "FLG" },
    { "csr", T("CSR / PB"), T("Cheyne-Stokes respiration, periodic breathing"),
      T("Breathing that waxes and wanes in regular cycles of about a minute."),
      T("Shown as % of the night. More than a little is worth showing to a doctor: it can come with heart problems, high altitude or some medicines."),
      T("Close to 0% of the night."), false, "cai,an_flags", "CSR,PB" },
    { "large_leak", T("Large leak"), T("leak above the device's limit"),
      T("Time when the leak was so high that the device could not keep the pressure or score events reliably."),
      T("Usually an ill-fitting mask, an open mouth with a nasal mask, or a moved mask."), T("Close to 0% of the night."), false, "leak,leak_redline", "LargeLeak" },
    { "leak", T("Leak"), T("unintended leak"),
      T("Air escaping past the mask or the mouth, over the vent the mask is meant to have."),
      T("Devices report leak differently: ResMed shows unintended leak, Philips and some others total leak including the vent. Compare a device only with itself."),
      T("ResMed: below 24 L/min; Prisma/Resvent: compare with the red line set in the profile."), false, "leak_total,large_leak,leak_redline", "Leak" },
    { "leak_total", T("Total leak"), T("vent and unintended leak together"),
      T("All air leaving the circuit: the mask's own vent plus any unintended leak."),
      T("The vent flow depends on the mask and pressure, so only rises above the usual level matter."), "", false, "leak", "LeakTotal" },
    { "leak_redline", T("Leak red line"), T("the leak threshold set in the profile"),
      T("The leak level above which OSCAR counts the time as 'large leak' and marks it red."),
      T("Set it in Preferences for your device and mask; the default suits ResMed."), "", false, "leak,large_leak", "" },
    { "pressure", T("Pressure"), T("treatment pressure"),
      T("The air pressure the device delivered, in cm H2O."),
      T("With an auto device (APAP) it changes during the night. The 95% figure is the pressure that was not exceeded 95% of the time, often used to choose a fixed pressure."),
      "", false, "pressure_set,p95,apap_range,pressure_max_time", "Pressure" },
    { "pressure_set", T("Set pressure"), T("pressure the device aimed for"),
      T("The pressure the device was trying to deliver, as opposed to the measured one."), T("Small differences from the measured pressure are normal."), "", false, "pressure", "PressureSet" },
    { "epap", T("EPAP"), T("Expiratory pressure"),
      T("The pressure while breathing out."), T("Lower than on inspiration with bilevel devices or pressure relief; it keeps the airway open at the end of the breath."), "", false, "ipap,ps,relief", "EPAP,EPAPSet" },
    { "ipap", T("IPAP"), T("Inspiratory pressure"),
      T("The pressure while breathing in."), T("With bilevel devices IPAP is higher than EPAP; the difference helps ventilation."), "", false, "epap,ps", "IPAP,IPAPSet" },
    { "ps", T("PS"), T("Pressure support"),
      T("The difference between IPAP and EPAP."), T("Bigger support helps breathe deeper; set by the doctor."), "", false, "ipap,epap", "PS" },
    { "pressure_max_time", T("Time at maximum"), T("time at the APAP's upper limit"),
      T("How long the auto device stayed at its upper pressure limit."),
      T("A lot of time at the maximum may mean the device would go higher if allowed: worth discussing the limit with the doctor."), T("A few % of the night at most (guide)."), false, "apap_range,pressure", "" },
    { "mode", T("Mode"), T("CPAP, APAP or bilevel"),
      T("How the device chooses the pressure: fixed (CPAP), automatic (APAP) or two levels (BiPAP/bilevel)."), T("Set by the doctor or in the device menu."), "", false, "apap_range,relief", "" },
    { "relief", T("Pressure relief"), T("EPR, softPAP, C-Flex, IPR"),
      T("A short drop of pressure at the start of breathing out, for comfort."),
      T("EPR (ResMed), softPAP (Löwenstein), C-Flex (Philips), IPR (Resvent): the level says how much the pressure drops. It does not change the treatment pressure itself."), "", false, "epap,mode", "" },
    { "ramp", T("Ramp"), T("slow start"),
      T("The pressure starts low and rises over the first minutes to help fall asleep."), T("Events during the ramp may be counted at low pressure."), "", false, "pressure", "" },
    { "apap_range", T("Min / max pressure"), T("the APAP's range"),
      T("The lowest and highest pressure the auto device may use."), T("Set by the doctor. Too low a minimum lets events through at the start; too low a maximum shows as time at the maximum."), "", false, "pressure,pressure_max_time", "" },
    { "resp_rate", T("Respiratory rate"), T("breaths per minute"),
      T("How many breaths a minute."), T("Calculated from the flow."), T("Usually 12–20 a minute in sleep."), false, "tidal_volume,minute_vent", "RespRate" },
    { "tidal_volume", T("Tidal volume"), T("air per breath"),
      T("How much air is breathed in with one breath, in mL."), T("Calculated from the flow."), T("Roughly 6–8 mL per kg of ideal body weight."), false, "resp_rate,minute_vent", "TidalVolume" },
    { "minute_vent", T("Minute ventilation"), T("air per minute"),
      T("How much air is breathed in a minute: rate times volume."), T("Drops in it can show shallow breathing."), T("Roughly 5–8 L/min at rest."), false, "resp_rate,tidal_volume", "MinuteVent" },
    { "ti_te", T("Ti / Te"), T("inspiration and expiration time"),
      T("How long each breath in (Ti) and out (Te) lasts, in seconds."), T("Matters mostly for bilevel settings."), "", false, "ie_ratio,resp_rate", "Ti,Te" },
    { "ie_ratio", T("I:E"), T("inspiration to expiration ratio"),
      T("How long breathing in takes compared with breathing out."), T("Usually breathing out is longer."), T("About 1:1.5 to 1:2."), false, "ti_te", "IE" },
    { "snore", T("Snore"), T("snoring detected by the device"),
      T("Vibration in the flow the device reads as snoring."), T("Snoring on therapy can mean the pressure is a little low or the mouth opens."), T("Close to 0."), false, "fl_device", "Snore" },
    { "flow_rate", T("Flow"), T("air flow, the breathing curve"),
      T("The air going in (above zero) and out (below zero), recorded many times a second."),
      T("All of OSCAR's own analysis is computed from this curve. Flat or double-peaked tops of the inspirations suggest a partly narrowed airway."), "", false, "fl_score,second_opinion", "FlowRate" },
    { "mask_pressure", T("Mask pressure"), T("pressure measured at the mask"),
      T("The pressure measured in the mask, recorded many times a second."), T("Follows the set pressure; dips can show leaks."), "", false, "pressure", "MaskPressure" },
    { "event_flags", T("Event flags"), T("events marked by the device"),
      T("The events the device recorded during the night, each kind on its own line."), T("Hover a flag to see what it is; click a line in the list on the left to jump to it."), "", false, "ahi,an_flags", "" },
    { "sensawake", T("SensAwake"), T("wake detected by the device"),
      T("Moments the device judged the sleeper awake and lowered the pressure."), T("ResMed feature."), "", false, "", "SensAwake" },
    { "user_flags", T("User flags"), T("events marked by OSCAR's flow rules"),
      T("Events OSCAR marks on the flow by your own rules in Preferences."), T("Not counted in AHI."), "", false, "", "UserFlag1,UserFlag2" },

    // ---- statistics
    { "median", T("Median"), T("the middle value"),
      T("Half of the time (or of the nights) the value was below it, half above."), T("Unlike the average it is not pulled by a few extreme values."), "", false, "p95,wavg,maximum", "" },
    { "p95", T("95%"), T("the 95th percentile"),
      T("The value not exceeded 95% of the time; only the highest 5% are above it."),
      T("Good for pressure and leak: it ignores short spikes but shows the usual high level."), "", false, "median,maximum", "" },
    { "maximum", T("Maximum"), T("the highest value"),
      T("The highest value recorded."), T("Often a short spike; the 95% figure is usually more useful."), "", false, "p95", "" },
    { "wavg", T("Average"), T("time-weighted average"),
      T("The average over the time, longer stretches counting more."), T("Used for pressure and leak: a short spike moves it little, a long high stretch moves it a lot."), "", false, "median", "" },
    { "nights_with_data", T("Nights with data"), T("nights the device was used"),
      T("Nights with at least one session in the period."), T("Days without data are counted as not used."), "", false, "compliance_pct", "" },
    { "compliance_pct", T("% of nights"), T("share of nights meeting the usage target"),
      T("The share of days in the period with at least the target hours of use."), T("The target (4 h by default) is set in the profile."), T("70% of nights or more."), false, "compliance,usage", "" },
    { "period", T("Period"), T("the columns of Statistics"),
      T("Each column summarises a time span: the last night, the last week, month, half year, year or all data."), T("Compare columns to see whether a change of settings helped."), "", false, "", "" },

    // ---- oximetry
    { "spo2", T("SpO2"), T("blood oxygen saturation"),
      T("How much of the blood's haemoglobin carries oxygen, measured by the finger oximeter."),
      T("Short drops follow apneas and hypopneas. A poor finger signal can show false drops."), T("95–100% awake; in sleep mostly above 90–92%."), false, "t90,odi3,spo2_nadir", "SPO2" },
    { "t90", T("Time below 90%"), T("SpO2 below a threshold"),
      T("The share of the recorded time with SpO2 below the threshold (90% unless chosen otherwise)."), T("Long time below 90% is a reason to see a doctor even if AHI is low."), T("Close to 0%; more than 5% is worth attention."), false, "spo2,odi3", "" },
    { "odi3", T("ODI 3%"), T("Oxygen Desaturation Index"),
      T("Drops of SpO2 by 3% or more per hour."), T("Often rises together with AHI; a high ODI with a low AHI may mean events the device misses."), T("Below 5 an hour."), false, "odi4,spo2,ahi", "" },
    { "odi4", T("ODI 4%"), T("desaturations of 4% or more"),
      T("Drops of SpO2 by 4% or more per hour."), T("A stricter count than ODI 3%; used by some guidelines."), T("Below 5 an hour."), false, "odi3", "" },
    { "spo2_nadir", T("SpO2 nadir"), T("the lowest SpO2"),
      T("The lowest saturation of the night."), T("Single low values can be artefacts of a moved oximeter; look at how long it stayed low."), T("Ideally not below 88–90%."), false, "spo2,t90", "" },
    { "pulse", T("Pulse"), T("heart rate"),
      T("Beats per minute from the oximeter."), T("Rises after events show the body reacting to them."), T("Roughly 50–90 in sleep."), false, "pulse_change,pulse_response", "Pulse" },
    { "pulse_change", T("Pulse change"), T("sudden pulse rises"),
      T("Moments the pulse rose quickly, often after an event or an arousal."), T("Marked by the oximeter software or OSCAR. Many of them can mean disturbed sleep even with a good AHI."), "", false, "pulse,pulse_response", "PulseChange" },
    { "perfusion", T("Perfusion index"), T("strength of the finger signal"),
      T("How strong the pulse signal is at the finger."), T("A low index makes SpO2 less reliable."), "", false, "spo2", "Perf. Index" },
    { "plethy", T("Plethysmogram"), T("the pulse wave"),
      T("The oximeter's pulse wave curve."), T("A smooth regular wave means a good signal."), "", false, "spo2", "Plethy" },
    { "spo2_drop", T("SpO2 drop"), T("a desaturation"),
      T("A drop of SpO2 marked by the oximeter software."), T("OSCAR's ODI counts its own desaturations; these marks come from the oximeter's software."), "", false, "odi3", "SPO2Drop" },

    // ---- OSCAR's analysis
    { "second_opinion", T("Analysis (second opinion)"), T("OSCAR's own scoring of the flow"),
      T("OSCAR scores apneas, hypopneas and flow limitation from the recorded flow itself, independently of the device."),
      T("Useful to check the device's figures and to see what it does not report. It is not a sleep-lab diagnosis."), "", true, "an_ahi,agreement,hypopnea_rule", "" },
    { "an_ahi", T("AHI (analysis)"), T("OSCAR's own AHI"),
      T("Apneas and hypopneas per hour as OSCAR's analysis scores them from the flow."),
      T("Can differ from the device's AHI; the agreement line shows by how much."), T("Below 5 an hour."), true, "ahi,agreement,hypopnea_rule", "" },
    { "hypopnea_rule", T("Hypopnea rule"), T("AASM 3%, CMS 4%, flow only"),
      T("Which rule confirms a hypopnea: an oxygen drop of 3% (AASM) or 4% (CMS), or the flow alone."),
      T("Without an oximeter only the flow rule is possible. Auto uses the oximeter where it covers the event."), "", true, "hi,an_ahi,odi3", "" },
    { "agreement", T("Agreement with the device"), T("how far the analysis matches the device"),
      T("The share of events both the device and the analysis found."), T("Low agreement is worth a look at the differences list."), "", true, "an_ahi,ahi", "" },
    { "hypoxic_burden", T("Hypoxic burden"), T("oxygen lost to breathing events"),
      T("The area of SpO2 drops linked to breathing events, per hour (%·min/h)."),
      T("Research links a high burden with heart risk better than AHI alone; OSCAR's figure is an approximation."), T("No accepted norm; lower is better."), true, "odi3,spo2", "" },
    { "oxi_zones", T("Problem zones"), T("long stretches of poor oxygenation"),
      T("Stretches where SpO2 stayed low or kept dropping."), T("Worth showing to a doctor if frequent."), "", true, "t90,spo2", "" },
    { "unexplained_desat", T("Unexplained desaturations"), T("oxygen drops without an event"),
      T("SpO2 drops with no breathing event found around them."), T("Can be artefacts, or events the flow does not show."), "", true, "odi3,agreement", "" },
    { "pulse_response", T("Pulse response to events"), T("how much the pulse rises after events"),
      T("The average pulse rise after breathing events, in beats per minute."), T("A strong rise means the body reacts to the events with arousals; it is one of the signs research links with heart strain."), T("No accepted norm."), true, "pulse,pulse_change", "" },
    { "unscoreable", T("Unscoreable time"), T("time the analysis could not score"),
      T("Gaps, large leaks or a weak signal where the analysis did not score events."), T("This time is left out of the analysis' indices. A lot of it makes them less reliable."), "", true, "large_leak", "AnUnscoreable" },
    { "an_flags", T("Analysis flags"), T("events found by OSCAR's analysis"),
      T("The events and stretches OSCAR's analysis found, each kind on its own line."), T("Compare them with the device's event flags above; the Analysis tab lists where they differ."), "", true, "event_flags,second_opinion", "" },
    { "fl_score", T("FL score"), T("how limited each inspiration looks"),
      T("OSCAR's score of each inspiration's shape, from 0 (normal) to 1 (clearly limited)."),
      T("Computed from the flow curve when it is recorded at 10 Hz or more."), T("Mostly below 0.5."), true, "fl_time,flg", "AnFLScore" },
    { "fl_time", T("Flow limitation time"), T("time with limited breathing"),
      T("Time in runs of flow-limited breaths (3 or more in a row, or 10 seconds or more), as minutes and % of the night."),
      T("Flow limitation is a partly narrowed airway: the breath goes on but cannot get faster. A lot of it with a low AHI can still disturb sleep."), T("No accepted norm; a few % of the night is common (guide)."), true, "fl_longest,fl_breaths,fl_score,glasgow", "AnFlowLimitation" },
    { "fl_longest", T("Longest run"), T("the longest stretch of flow limitation"),
      T("The longest uninterrupted run of flow-limited breaths that night."), T("Long runs, minutes rather than seconds, point to stretches when the pressure was not enough, often in one sleep position or stage."), "", true, "fl_time", "" },
    { "fl_breaths", T("% of breaths"), T("share of flow-limited breaths"),
      T("The share of scored breaths that looked flow-limited, single ones included."), T("Higher than the % of time because single breaths count too."), "", true, "fl_time", "" },
    { "glasgow", T("Glasgow Index"), T("DaveSkvn's index of breath shapes"),
      T("The sum of the shares of breaths with each of 8 unusual shape signs; 0 is clean breathing."),
      T("Made by DaveSkvn for ResMed recordings at 25 Hz. In practice it is driven mostly by 'no pause' and 'variable amplitude' and tells flow limitation apart weakly; rely on the flow limitation time first."),
      T("Author's guide: 0–0.2 clean, about 3 serious problems."), true, "glasgow_adapted,fl_time,gi_nopause,gi_flattop", "AnGlasgowIndex" },
    { "glasgow_adapted", T("Glasgow Index (adapted)"), T("thresholds relative to the breath"),
      T("The same signs on OSCAR's own breaths, with thresholds relative to the breath's size."), T("Fairer between devices and weak breaths than the original."), T("As the original."), true, "glasgow", "AnGlasgowAdapted" },
    { "gi_skew", T("Skew"), T("lopsided inspiration"), T("Most of the inspiration's volume falls before or after its middle."), T("Flagged when less than 45% or more than 55% of the volume is in the first half."), "", true, "glasgow", "" },
    { "gi_spike", T("Spike"), T("sharp peak"), T("The inspiration spends little time near its peak: a sharp, short top."), T("Flagged when less than 20% of the inspiration is above 90% of its peak."), "", true, "glasgow", "" },
    { "gi_flattop", T("Flat top"), T("flattened inspiration"), T("The middle of the inspiration is flat: the classic sign of flow limitation."), T("Flagged when the flow in the middle half of the inspiration hardly varies."), "", true, "glasgow,fl_time", "" },
    { "gi_topheavy", T("Top heavy"), T("long time near the peak"), T("The inspiration stays near its peak for long; not counted in the index."), T("Flagged when more than 40% of the inspiration is above 90% of its peak; the author leaves it out of the sum."), "", true, "glasgow", "" },
    { "gi_multipeak", T("Double peak"), T("two humps"), T("The inspiration has two peaks with a dip between them."), T("A sign of an airway that narrows during the breath; a coarse recording can fake it, which OSCAR smooths away."), "", true, "glasgow", "" },
    { "gi_nopause", T("No pause"), T("no rest before breathing in"), T("The next breath starts without the short rest after breathing out."), T("Common on CPAP; also triggered by a small flow offset during the rest."), "", true, "glasgow", "" },
    { "gi_inspirrate", T("Inspiration rate"), T("fast breathing"), T("More than 20 inspirations a minute over the last 5 breaths."), T("Fast breathing in sleep can follow arousals or come with heart or lung problems."), "", true, "glasgow,resp_rate", "" },
    { "gi_multibreath", T("Double inspiration"), T("two breaths for one exhale"), T("A second inspiration before a real expiration."), T("Two humps of inspiration with no breath out between them, as in a sigh or a broken breath."), "", true, "glasgow", "" },
    { "gi_ampvar", T("Variable amplitude"), T("uneven breath strength"), T("The peaks of the last 5 inspirations differ markedly."), T("Unsettled breathing: alternating deep and shallow breaths, as around events or arousals."), "", true, "glasgow", "" },

    // ---- comparison
    { "best_value", T("Best value"), T("green in the comparison"),
      T("The best figure among settings used for at least 3 nights."), T("Fewer nights are too few to judge."), "", false, "few_nights", "" },
    { "few_nights", T("Few nights"), T("grey rows"),
      T("Settings used for fewer than 3 nights: too few to draw conclusions."), T("Use the settings for a few more nights before comparing them."), "", false, "best_value", "" },
};
// clang-format on

#undef T

QString tr(const char *s) { return s && *s ? QCoreApplication::translate("Glossary", s) : QString(); }

QStringList split(const char *s)
{
    return QString::fromLatin1(s).split(QLatin1Char(','), Qt::SkipEmptyParts);
}

GlossaryEntry entry(const Raw &r)
{
    GlossaryEntry e;
    e.key = QString::fromLatin1(r.key);
    e.term = tr(r.term);
    e.expansion = tr(r.expansion);
    e.summary = tr(r.summary);
    e.details = tr(r.details);
    e.norm = tr(r.norm);
    e.experimental = r.experimental;
    e.seeAlso = split(r.seeAlso);
    e.channels = split(r.channels);
    return e;
}

QString folded(const QString &s)
{
    QString out = s.toLower();
    out.replace(QChar(0x0451), QChar(0x0435));   // ё → е
    return out;
}

QString caveat() { return QCoreApplication::translate("Glossary", "Experimental measure of OSCAR's analysis, not a medical norm."); }

} // namespace

namespace Glossary {

const GlossaryEntry *find(const QString &key)
{
    // rebuilt when the language changes: the translated texts are cached per language
    static QHash<QString, GlossaryEntry> cache;
    static QString cachedFor;
    const QString lang = QCoreApplication::translate("Glossary", "Usage");
    if (cachedFor != lang) {
        cache.clear();
        for (const Raw &r : kEntries) cache.insert(QString::fromLatin1(r.key), entry(r));
        cachedFor = lang;
    }
    const auto it = cache.constFind(key);
    return it == cache.constEnd() ? nullptr : &it.value();
}

QList<GlossaryEntry> all()
{
    QList<GlossaryEntry> out;
    for (const Raw &r : kEntries) out << *find(QString::fromLatin1(r.key));
    return out;
}

QString tooltip(const QString &key)
{
    const GlossaryEntry *e = find(key);
    if (!e) return {};
    QString html = QStringLiteral("<b>%1</b>").arg(e->term.toHtmlEscaped());
    if (!e->expansion.isEmpty()) html += QStringLiteral(" — ") + e->expansion.toHtmlEscaped();
    html += QStringLiteral("<br>") + e->summary.toHtmlEscaped();
    if (!e->norm.isEmpty()) html += QStringLiteral("<br><i>%1</i>").arg(e->norm.toHtmlEscaped());
    return html;
}

QString panel(const QString &key)
{
    const GlossaryEntry *e = find(key);
    if (!e) return {};
    auto heading = [](const char *s) {
        return QStringLiteral("<h4>%1</h4>").arg(QCoreApplication::translate("Glossary", s).toHtmlEscaped());
    };
    QString html = QStringLiteral("<h2>%1</h2>").arg(e->term.toHtmlEscaped());
    if (!e->expansion.isEmpty()) html += QStringLiteral("<p><i>%1</i></p>").arg(e->expansion.toHtmlEscaped());
    html += heading(QT_TRANSLATE_NOOP("Glossary", "What it is")) + QStringLiteral("<p>%1</p>").arg(e->summary.toHtmlEscaped());
    if (!e->details.isEmpty()) {
        html += heading(QT_TRANSLATE_NOOP("Glossary", "How to read it")) + QStringLiteral("<p>%1</p>").arg(e->details.toHtmlEscaped());
    }
    if (!e->norm.isEmpty()) {
        html += heading(QT_TRANSLATE_NOOP("Glossary", "Norm / guide")) + QStringLiteral("<p>%1</p>").arg(e->norm.toHtmlEscaped());
    }
    if (e->experimental) {
        html += heading(QT_TRANSLATE_NOOP("Glossary", "Caveat")) + QStringLiteral("<p>%1</p>").arg(caveat().toHtmlEscaped());
    }
    if (!e->seeAlso.isEmpty()) {
        QStringList links;
        for (const QString &k : e->seeAlso) {
            if (const GlossaryEntry *other = find(k)) {
                links << QStringLiteral("<a href='help:%1'>%2</a>").arg(k, other->term.toHtmlEscaped());
            }
        }
        html += heading(QT_TRANSLATE_NOOP("Glossary", "See also")) + QStringLiteral("<p>%1</p>").arg(links.join(QStringLiteral(" · ")));
    }
    return html;
}

QString keyForChannel(ChannelID code)
{
    const QString name = schema::channel[code].code();
    if (name.isEmpty()) return {};
    for (const Raw &r : kEntries) {
        if (split(r.channels).contains(name)) return QString::fromLatin1(r.key);
    }
    return {};
}

QString channelTooltip(ChannelID code)
{
    const QString key = keyForChannel(code);
    if (!key.isEmpty()) return tooltip(key);
    schema::Channel &ch = schema::channel[code];
    if (ch.fullname().isEmpty() && ch.description().isEmpty()) return {};
    return QStringLiteral("<b>%1</b><br>%2").arg(ch.fullname().toHtmlEscaped(), ch.description().toHtmlEscaped());
}

QStringList search(const QString &text)
{
    const QString q = folded(text.trimmed());
    if (q.isEmpty()) return {};
    QStringList byTerm, byText;
    for (const GlossaryEntry &e : all()) {
        if (folded(e.term + QLatin1Char(' ') + e.expansion).contains(q)) byTerm << e.key;
        else if (folded(e.summary + QLatin1Char(' ') + e.details).contains(q)) byText << e.key;
    }
    return byTerm + byText;
}

} // namespace Glossary
