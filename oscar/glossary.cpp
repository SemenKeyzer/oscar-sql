/* Glossary
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "glossary.h"

#include <QCoreApplication>
#include <QEvent>
#include <QHash>
#include <QPointer>

#include "SleepLib/schema.h"
#include "uiglossary.h"

namespace {

struct Raw {
    const char *key, *term, *expansion, *summary, *details, *norm;
    bool experimental;
    const char *seeAlso;   // keys, comma separated
    const char *channels;  // channel codes, comma separated
};

// clang-format off
const Raw kEntries[] = {
    // ---- therapy
    { "usage", QT_TRANSLATE_NOOP("Glossary", "Usage"), QT_TRANSLATE_NOOP("Glossary", "hours with the mask on"),
      QT_TRANSLATE_NOOP("Glossary", "How long the device ran with the mask on that night."),
      QT_TRANSLATE_NOOP("Glossary", "Counted from the sessions the device recorded; short breaks with the mask off are not included. Several sessions in a night are added up."),
      QT_TRANSLATE_NOOP("Glossary", "4 hours or more a night is the usual compliance target; 7 hours or more gives the most benefit."), false, "compliance,sessions,mask_off", "" },
    { "compliance", QT_TRANSLATE_NOOP("Glossary", "Compliance"), QT_TRANSLATE_NOOP("Glossary", "nights with enough usage"),
      QT_TRANSLATE_NOOP("Glossary", "Whether the night reached the usage target set in the profile (4 hours by default)."),
      QT_TRANSLATE_NOOP("Glossary", "Insurers and doctors often count compliance as the share of nights with at least 4 hours of use, for example over any 30 days."),
      QT_TRANSLATE_NOOP("Glossary", "At least 70% of nights with 4 hours or more is the common requirement."), false, "usage,compliance_pct", "" },
    { "sessions", QT_TRANSLATE_NOOP("Glossary", "Session"), QT_TRANSLATE_NOOP("Glossary", "one stretch of therapy"),
      QT_TRANSLATE_NOOP("Glossary", "A continuous stretch from switching the device on (or putting the mask on) to switching it off."),
      QT_TRANSLATE_NOOP("Glossary", "Taking the mask off at night usually starts a new session. Many short sessions can mean sleep is broken or the mask is uncomfortable."),
      "", false, "usage,mask_off", "" },
    { "mask_off", QT_TRANSLATE_NOOP("Glossary", "Mask off"), QT_TRANSLATE_NOOP("Glossary", "time without the mask"),
      QT_TRANSLATE_NOOP("Glossary", "Breaks during the night when the device detected no mask on the face."),
      QT_TRANSLATE_NOOP("Glossary", "These breaks are not therapy time and are not counted as usage or in the indices."),
      "", false, "usage,sessions", "" },
    { "ahi", QT_TRANSLATE_NOOP("Glossary", "AHI"), QT_TRANSLATE_NOOP("Glossary", "Apnea-Hypopnea Index"),
      QT_TRANSLATE_NOOP("Glossary", "Apneas and hypopneas per hour of sleep with the device, as the device counted them."),
      QT_TRANSLATE_NOOP("Glossary", "The main measure of how well therapy keeps the airway open. The device counts events from the flow; its rules differ from a sleep lab's, and without an oximeter it cannot confirm hypopneas by a drop in oxygen."),
      QT_TRANSLATE_NOOP("Glossary", "Below 5 an hour is good; 5–15 mild, 15–30 moderate, over 30 severe (for an untreated person)."), false, "oai,cai,hi,rera,an_ahi,rdi", "" },
    { "manual_scoring", QT_TRANSLATE_NOOP("Glossary", "Manual scoring"), QT_TRANSLATE_NOOP("Glossary", "events corrected by hand"),
      QT_TRANSLATE_NOOP("Glossary", "The night's apneas and hypopneas as corrected by hand: events added, removed, retyped or given new bounds, and stretches left out (noise, awake)."),
      QT_TRANSLATE_NOOP("Glossary", "The device's own events stay stored; the corrections are kept apart and can be undone. The AHI then counts the corrected events over the hours less the stretches left out, on every page and in the PDF; usage and compliance keep the full hours. OSCAR's own analysis is not changed by it."),
      "", false, "ahi,usage", "" },
    { "rdi", QT_TRANSLATE_NOOP("Glossary", "RDI"), QT_TRANSLATE_NOOP("Glossary", "Respiratory Disturbance Index"),
      QT_TRANSLATE_NOOP("Glossary", "Like AHI, but also counts RERAs: events of extra breathing effort that do not reach a hypopnea."),
      QT_TRANSLATE_NOOP("Glossary", "RDI is AHI plus RERA per hour. Some devices do not record RERAs, then RDI equals AHI."),
      QT_TRANSLATE_NOOP("Glossary", "Below 5 an hour is a good result on therapy."), false, "ahi,rera", "" },
    { "oai", QT_TRANSLATE_NOOP("Glossary", "OAI"), QT_TRANSLATE_NOOP("Glossary", "Obstructive Apnea Index"),
      QT_TRANSLATE_NOOP("Glossary", "Obstructive apneas per hour: the airway closed while the breathing effort went on."),
      QT_TRANSLATE_NOOP("Glossary", "An obstructive apnea is a stop of airflow of 10 seconds or more with effort to breathe. Many of them on therapy usually mean the pressure is too low or the mask leaks."),
      QT_TRANSLATE_NOOP("Glossary", "Close to 0 on good therapy."), false, "ahi,cai,uai", "Obstructive" },
    { "cai", QT_TRANSLATE_NOOP("Glossary", "CAI"), QT_TRANSLATE_NOOP("Glossary", "Central (clear airway) Apnea Index"),
      QT_TRANSLATE_NOOP("Glossary", "Central apneas per hour: breathing stopped with the airway open, without effort."),
      QT_TRANSLATE_NOOP("Glossary", "The device tells them apart by small pressure pulses. A few are normal, for example while falling asleep; many may be caused by too much pressure or need a doctor's attention."),
      QT_TRANSLATE_NOOP("Glossary", "Below 5 an hour is usually fine."), false, "ahi,oai,csr", "ClearAirway" },
    { "uai", QT_TRANSLATE_NOOP("Glossary", "UAI"), QT_TRANSLATE_NOOP("Glossary", "Unclassified Apnea Index"),
      QT_TRANSLATE_NOOP("Glossary", "Apneas the device could not tell as obstructive or central."),
      QT_TRANSLATE_NOOP("Glossary", "Counted in AHI. Some devices record them when the airway test was not possible, for example with a large leak."),
      "", false, "ahi,oai,cai", "Apnea" },
    { "all_apnea", QT_TRANSLATE_NOOP("Glossary", "All apneas"), QT_TRANSLATE_NOOP("Glossary", "apneas of any kind"),
      QT_TRANSLATE_NOOP("Glossary", "All apneas the device recorded, without telling their kind."),
      QT_TRANSLATE_NOOP("Glossary", "Some devices record only this total."), "", false, "ahi,oai,cai", "AllApnea" },
    { "hi", QT_TRANSLATE_NOOP("Glossary", "HI"), QT_TRANSLATE_NOOP("Glossary", "Hypopnea Index"),
      QT_TRANSLATE_NOOP("Glossary", "Hypopneas per hour: breathing shallower by at least 30% for 10 seconds or more."),
      QT_TRANSLATE_NOOP("Glossary", "In a sleep lab a hypopnea also needs an oxygen drop of 3% (or 4%) or an arousal. A device sees only the flow, so its count may differ."),
      QT_TRANSLATE_NOOP("Glossary", "Together with apneas below 5 an hour."), false, "ahi,oh,ch,hypopnea_rule", "Hypopnea" },
    { "oh", QT_TRANSLATE_NOOP("Glossary", "OH"), QT_TRANSLATE_NOOP("Glossary", "obstructive hypopnea"),
      QT_TRANSLATE_NOOP("Glossary", "A hypopnea with signs of a narrowed airway."), QT_TRANSLATE_NOOP("Glossary", "Recorded by devices that classify hypopneas (and by OSCAR's analysis)."),
      "", false, "hi,ch", "ObstructiveHypopnea" },
    { "ch", QT_TRANSLATE_NOOP("Glossary", "CH"), QT_TRANSLATE_NOOP("Glossary", "central hypopnea"),
      QT_TRANSLATE_NOOP("Glossary", "A hypopnea from reduced breathing effort with an open airway."), QT_TRANSLATE_NOOP("Glossary", "Recorded by devices that classify hypopneas (and by OSCAR's analysis)."),
      "", false, "hi,oh", "CentralHypopnea" },
    { "rera", QT_TRANSLATE_NOOP("Glossary", "RERA"), QT_TRANSLATE_NOOP("Glossary", "Respiratory Effort Related Arousal"),
      QT_TRANSLATE_NOOP("Glossary", "Breaths getting harder and flatter until the sleeper partly wakes, without a full apnea or hypopnea."),
      QT_TRANSLATE_NOOP("Glossary", "A sign of a partly narrowed airway. Many RERAs with a low AHI can still leave you tired."),
      QT_TRANSLATE_NOOP("Glossary", "A few an hour are common; many may need more pressure."), false, "rdi,fl_device,fl_time", "RERA" },
    { "fl_device", QT_TRANSLATE_NOOP("Glossary", "FL"), QT_TRANSLATE_NOOP("Glossary", "flow limitation event (device)"),
      QT_TRANSLATE_NOOP("Glossary", "A stretch where the device saw flattened inspirations: the airway partly narrowed."),
      QT_TRANSLATE_NOOP("Glossary", "Devices mark it differently; it is not counted in AHI."), "", false, "flg,fl_time,rera", "FlowLimit" },
    { "flg", QT_TRANSLATE_NOOP("Glossary", "Flow limitation"), QT_TRANSLATE_NOOP("Glossary", "the device's flow limitation level"),
      QT_TRANSLATE_NOOP("Glossary", "How flattened the inspirations look to the device, from 0 (normal) to 1 (strongly limited)."),
      QT_TRANSLATE_NOOP("Glossary", "Shown by ResMed and some other devices about twice a second. Short peaks are common; long stretches above 0.3–0.5 suggest the airway is not fully held open."),
      QT_TRANSLATE_NOOP("Glossary", "Mostly 0; the 95th percentile ideally below about 0.3 (guide)."), false, "fl_device,fl_time,fl_score", "FLG" },
    { "csr", QT_TRANSLATE_NOOP("Glossary", "CSR / PB"), QT_TRANSLATE_NOOP("Glossary", "Cheyne-Stokes respiration, periodic breathing"),
      QT_TRANSLATE_NOOP("Glossary", "Breathing that waxes and wanes in regular cycles of about a minute."),
      QT_TRANSLATE_NOOP("Glossary", "Shown as % of the night. More than a little is worth showing to a doctor: it can come with heart problems, high altitude or some medicines."),
      QT_TRANSLATE_NOOP("Glossary", "Close to 0% of the night."), false, "cai,an_flags", "CSR,PB" },
    { "large_leak", QT_TRANSLATE_NOOP("Glossary", "Large leak"), QT_TRANSLATE_NOOP("Glossary", "leak above the device's limit"),
      QT_TRANSLATE_NOOP("Glossary", "Time when the leak was so high that the device could not keep the pressure or score events reliably."),
      QT_TRANSLATE_NOOP("Glossary", "Usually an ill-fitting mask, an open mouth with a nasal mask, or a moved mask."), QT_TRANSLATE_NOOP("Glossary", "Close to 0% of the night."), false, "leak,leak_redline", "LargeLeak" },
    { "leak", QT_TRANSLATE_NOOP("Glossary", "Leak"), QT_TRANSLATE_NOOP("Glossary", "unintended leak"),
      QT_TRANSLATE_NOOP("Glossary", "Air escaping past the mask or the mouth, over the vent the mask is meant to have."),
      QT_TRANSLATE_NOOP("Glossary", "Devices report leak differently: ResMed shows unintended leak, Philips and some others total leak including the vent. Compare a device only with itself."),
      QT_TRANSLATE_NOOP("Glossary", "ResMed: below 24 L/min; Prisma/Resvent: compare with the red line set in the profile."), false, "leak_total,large_leak,leak_redline", "Leak" },
    { "leak_total", QT_TRANSLATE_NOOP("Glossary", "Total leak"), QT_TRANSLATE_NOOP("Glossary", "vent and unintended leak together"),
      QT_TRANSLATE_NOOP("Glossary", "All air leaving the circuit: the mask's own vent plus any unintended leak."),
      QT_TRANSLATE_NOOP("Glossary", "The vent flow depends on the mask and pressure, so only rises above the usual level matter."), "", false, "leak", "LeakTotal" },
    { "leak_redline", QT_TRANSLATE_NOOP("Glossary", "Leak red line"), QT_TRANSLATE_NOOP("Glossary", "the leak threshold set in the profile"),
      QT_TRANSLATE_NOOP("Glossary", "The leak level above which OSCAR counts the time as 'large leak' and marks it red."),
      QT_TRANSLATE_NOOP("Glossary", "Set it in Preferences for your device and mask; the default suits ResMed."), "", false, "leak,large_leak", "" },
    { "pressure", QT_TRANSLATE_NOOP("Glossary", "Pressure"), QT_TRANSLATE_NOOP("Glossary", "treatment pressure"),
      QT_TRANSLATE_NOOP("Glossary", "The air pressure the device delivered, in cm H2O."),
      QT_TRANSLATE_NOOP("Glossary", "With an auto device (APAP) it changes during the night. The 95% figure is the pressure that was not exceeded 95% of the time, often used to choose a fixed pressure."),
      "", false, "pressure_set,p95,apap_range,pressure_max_time", "Pressure" },
    { "pressure_set", QT_TRANSLATE_NOOP("Glossary", "Set pressure"), QT_TRANSLATE_NOOP("Glossary", "pressure the device aimed for"),
      QT_TRANSLATE_NOOP("Glossary", "The pressure the device was trying to deliver, as opposed to the measured one."), QT_TRANSLATE_NOOP("Glossary", "Small differences from the measured pressure are normal."), "", false, "pressure", "PressureSet" },
    { "epap", QT_TRANSLATE_NOOP("Glossary", "EPAP"), QT_TRANSLATE_NOOP("Glossary", "Expiratory pressure"),
      QT_TRANSLATE_NOOP("Glossary", "The pressure while breathing out."), QT_TRANSLATE_NOOP("Glossary", "Lower than on inspiration with bilevel devices or pressure relief; it keeps the airway open at the end of the breath."), "", false, "ipap,ps,relief", "EPAP,EPAPSet" },
    { "ipap", QT_TRANSLATE_NOOP("Glossary", "IPAP"), QT_TRANSLATE_NOOP("Glossary", "Inspiratory pressure"),
      QT_TRANSLATE_NOOP("Glossary", "The pressure while breathing in."), QT_TRANSLATE_NOOP("Glossary", "With bilevel devices IPAP is higher than EPAP; the difference helps ventilation."), "", false, "epap,ps", "IPAP,IPAPSet" },
    { "ps", QT_TRANSLATE_NOOP("Glossary", "PS"), QT_TRANSLATE_NOOP("Glossary", "Pressure support"),
      QT_TRANSLATE_NOOP("Glossary", "The difference between IPAP and EPAP."), QT_TRANSLATE_NOOP("Glossary", "Bigger support helps breathe deeper; set by the doctor."), "", false, "ipap,epap", "PS" },
    { "pressure_max_time", QT_TRANSLATE_NOOP("Glossary", "Time at maximum"), QT_TRANSLATE_NOOP("Glossary", "time at the APAP's upper limit"),
      QT_TRANSLATE_NOOP("Glossary", "How long the auto device stayed at its upper pressure limit."),
      QT_TRANSLATE_NOOP("Glossary", "A lot of time at the maximum may mean the device would go higher if allowed: worth discussing the limit with the doctor."), QT_TRANSLATE_NOOP("Glossary", "A few % of the night at most (guide)."), false, "apap_range,pressure", "" },
    { "mode", QT_TRANSLATE_NOOP("Glossary", "Mode"), QT_TRANSLATE_NOOP("Glossary", "CPAP, APAP or bilevel"),
      QT_TRANSLATE_NOOP("Glossary", "How the device chooses the pressure: fixed (CPAP), automatic (APAP) or two levels (BiPAP/bilevel)."), QT_TRANSLATE_NOOP("Glossary", "Set by the doctor or in the device menu."), "", false, "apap_range,relief", "" },
    { "relief", QT_TRANSLATE_NOOP("Glossary", "Pressure relief"), QT_TRANSLATE_NOOP("Glossary", "EPR, softPAP, C-Flex, IPR"),
      QT_TRANSLATE_NOOP("Glossary", "A short drop of pressure at the start of breathing out, for comfort."),
      QT_TRANSLATE_NOOP("Glossary", "EPR (ResMed), softPAP (Löwenstein), C-Flex (Philips), IPR (Resvent): the level says how much the pressure drops. It does not change the treatment pressure itself."), "", false, "epap,mode", "" },
    { "ramp", QT_TRANSLATE_NOOP("Glossary", "Ramp"), QT_TRANSLATE_NOOP("Glossary", "slow start"),
      QT_TRANSLATE_NOOP("Glossary", "The pressure starts low and rises over the first minutes to help fall asleep."), QT_TRANSLATE_NOOP("Glossary", "Events during the ramp may be counted at low pressure."), "", false, "pressure", "" },
    { "apap_range", QT_TRANSLATE_NOOP("Glossary", "Min / max pressure"), QT_TRANSLATE_NOOP("Glossary", "the APAP's range"),
      QT_TRANSLATE_NOOP("Glossary", "The lowest and highest pressure the auto device may use."), QT_TRANSLATE_NOOP("Glossary", "Set by the doctor. Too low a minimum lets events through at the start; too low a maximum shows as time at the maximum."), "", false, "pressure,pressure_max_time", "" },
    { "resp_rate", QT_TRANSLATE_NOOP("Glossary", "Respiratory rate"), QT_TRANSLATE_NOOP("Glossary", "breaths per minute"),
      QT_TRANSLATE_NOOP("Glossary", "How many breaths a minute."), QT_TRANSLATE_NOOP("Glossary", "Calculated from the flow."), QT_TRANSLATE_NOOP("Glossary", "Usually 12–20 a minute in sleep."), false, "tidal_volume,minute_vent", "RespRate" },
    { "tidal_volume", QT_TRANSLATE_NOOP("Glossary", "Tidal volume"), QT_TRANSLATE_NOOP("Glossary", "air per breath"),
      QT_TRANSLATE_NOOP("Glossary", "How much air is breathed in with one breath, in mL."), QT_TRANSLATE_NOOP("Glossary", "Calculated from the flow."), QT_TRANSLATE_NOOP("Glossary", "Roughly 6–8 mL per kg of ideal body weight."), false, "resp_rate,minute_vent", "TidalVolume" },
    { "minute_vent", QT_TRANSLATE_NOOP("Glossary", "Minute ventilation"), QT_TRANSLATE_NOOP("Glossary", "air per minute"),
      QT_TRANSLATE_NOOP("Glossary", "How much air is breathed in a minute: rate times volume."), QT_TRANSLATE_NOOP("Glossary", "Drops in it can show shallow breathing."), QT_TRANSLATE_NOOP("Glossary", "Roughly 5–8 L/min at rest."), false, "resp_rate,tidal_volume", "MinuteVent" },
    { "ti_te", QT_TRANSLATE_NOOP("Glossary", "Ti / Te"), QT_TRANSLATE_NOOP("Glossary", "inspiration and expiration time"),
      QT_TRANSLATE_NOOP("Glossary", "How long each breath in (Ti) and out (Te) lasts, in seconds."), QT_TRANSLATE_NOOP("Glossary", "Matters mostly for bilevel settings."), "", false, "ie_ratio,resp_rate", "Ti,Te" },
    { "ie_ratio", QT_TRANSLATE_NOOP("Glossary", "I:E"), QT_TRANSLATE_NOOP("Glossary", "inspiration to expiration ratio"),
      QT_TRANSLATE_NOOP("Glossary", "How long breathing in takes compared with breathing out."), QT_TRANSLATE_NOOP("Glossary", "Usually breathing out is longer."), QT_TRANSLATE_NOOP("Glossary", "About 1:1.5 to 1:2."), false, "ti_te", "IE" },
    { "snore", QT_TRANSLATE_NOOP("Glossary", "Snore"), QT_TRANSLATE_NOOP("Glossary", "snoring detected by the device"),
      QT_TRANSLATE_NOOP("Glossary", "Vibration in the flow the device reads as snoring."), QT_TRANSLATE_NOOP("Glossary", "Snoring on therapy can mean the pressure is a little low or the mouth opens."), QT_TRANSLATE_NOOP("Glossary", "Close to 0."), false, "fl_device", "Snore" },
    { "flow_rate", QT_TRANSLATE_NOOP("Glossary", "Flow"), QT_TRANSLATE_NOOP("Glossary", "air flow, the breathing curve"),
      QT_TRANSLATE_NOOP("Glossary", "The air going in (above zero) and out (below zero), recorded many times a second."),
      QT_TRANSLATE_NOOP("Glossary", "All of OSCAR's own analysis is computed from this curve. Flat or double-peaked tops of the inspirations suggest a partly narrowed airway."), "", false, "fl_score,second_opinion", "FlowRate" },
    { "mask_pressure", QT_TRANSLATE_NOOP("Glossary", "Mask pressure"), QT_TRANSLATE_NOOP("Glossary", "pressure measured at the mask"),
      QT_TRANSLATE_NOOP("Glossary", "The pressure measured in the mask, recorded many times a second."), QT_TRANSLATE_NOOP("Glossary", "Follows the set pressure; dips can show leaks."), "", false, "pressure", "MaskPressure" },
    { "event_flags", QT_TRANSLATE_NOOP("Glossary", "Event flags"), QT_TRANSLATE_NOOP("Glossary", "events marked by the device"),
      QT_TRANSLATE_NOOP("Glossary", "The events the device recorded during the night, each kind on its own line."), QT_TRANSLATE_NOOP("Glossary", "Hover a flag to see what it is; click a line in the list on the left to jump to it."), "", false, "ahi,an_flags", "" },
    { "sensawake", QT_TRANSLATE_NOOP("Glossary", "SensAwake"), QT_TRANSLATE_NOOP("Glossary", "wake detected by the device"),
      QT_TRANSLATE_NOOP("Glossary", "Moments the device judged the sleeper awake and lowered the pressure."), QT_TRANSLATE_NOOP("Glossary", "ResMed feature."), "", false, "", "SensAwake" },
    { "user_flags", QT_TRANSLATE_NOOP("Glossary", "User flags"), QT_TRANSLATE_NOOP("Glossary", "events marked by OSCAR's flow rules"),
      QT_TRANSLATE_NOOP("Glossary", "Events OSCAR marks on the flow by your own rules in Preferences."), QT_TRANSLATE_NOOP("Glossary", "Not counted in AHI."), "", false, "", "UserFlag1,UserFlag2" },

    // ---- statistics
    { "median", QT_TRANSLATE_NOOP("Glossary", "Median"), QT_TRANSLATE_NOOP("Glossary", "the middle value"),
      QT_TRANSLATE_NOOP("Glossary", "Half of the time (or of the nights) the value was below it, half above."), QT_TRANSLATE_NOOP("Glossary", "Unlike the average it is not pulled by a few extreme values."), "", false, "p95,wavg,maximum", "" },
    { "p95", QT_TRANSLATE_NOOP("Glossary", "95%"), QT_TRANSLATE_NOOP("Glossary", "the 95th percentile"),
      QT_TRANSLATE_NOOP("Glossary", "The value not exceeded 95% of the time; only the highest 5% are above it."),
      QT_TRANSLATE_NOOP("Glossary", "Good for pressure and leak: it ignores short spikes but shows the usual high level."), "", false, "median,maximum", "" },
    { "maximum", QT_TRANSLATE_NOOP("Glossary", "Maximum"), QT_TRANSLATE_NOOP("Glossary", "the highest value"),
      QT_TRANSLATE_NOOP("Glossary", "The highest value recorded."), QT_TRANSLATE_NOOP("Glossary", "Often a short spike; the 95% figure is usually more useful."), "", false, "p95", "" },
    { "wavg", QT_TRANSLATE_NOOP("Glossary", "Average"), QT_TRANSLATE_NOOP("Glossary", "time-weighted average"),
      QT_TRANSLATE_NOOP("Glossary", "The average over the time, longer stretches counting more."), QT_TRANSLATE_NOOP("Glossary", "Used for pressure and leak: a short spike moves it little, a long high stretch moves it a lot."), "", false, "median", "" },
    { "nights_with_data", QT_TRANSLATE_NOOP("Glossary", "Nights with data"), QT_TRANSLATE_NOOP("Glossary", "nights the device was used"),
      QT_TRANSLATE_NOOP("Glossary", "Nights with at least one session in the period."), QT_TRANSLATE_NOOP("Glossary", "Days without data are counted as not used."), "", false, "compliance_pct", "" },
    { "compliance_pct", QT_TRANSLATE_NOOP("Glossary", "% of nights"), QT_TRANSLATE_NOOP("Glossary", "share of nights meeting the usage target"),
      QT_TRANSLATE_NOOP("Glossary", "The share of days in the period with at least the target hours of use."), QT_TRANSLATE_NOOP("Glossary", "The target (4 h by default) is set in the profile."), QT_TRANSLATE_NOOP("Glossary", "70% of nights or more."), false, "compliance,usage", "" },
    { "period", QT_TRANSLATE_NOOP("Glossary", "Period"), QT_TRANSLATE_NOOP("Glossary", "the columns of Statistics"),
      QT_TRANSLATE_NOOP("Glossary", "Each column summarises a time span: the last night, the last week, month, half year, year or all data."), QT_TRANSLATE_NOOP("Glossary", "Compare columns to see whether a change of settings helped."), "", false, "", "" },

    // ---- oximetry
    { "spo2", QT_TRANSLATE_NOOP("Glossary", "SpO2"), QT_TRANSLATE_NOOP("Glossary", "blood oxygen saturation"),
      QT_TRANSLATE_NOOP("Glossary", "How much of the blood's haemoglobin carries oxygen, measured by the finger oximeter."),
      QT_TRANSLATE_NOOP("Glossary", "Short drops follow apneas and hypopneas. A poor finger signal can show false drops."), QT_TRANSLATE_NOOP("Glossary", "95–100% awake; in sleep mostly above 90–92%."), false, "t90,odi3,spo2_nadir", "SPO2" },
    { "t90", QT_TRANSLATE_NOOP("Glossary", "Time below 90%"), QT_TRANSLATE_NOOP("Glossary", "SpO2 below a threshold"),
      QT_TRANSLATE_NOOP("Glossary", "The share of the recorded time with SpO2 below the threshold (90% unless chosen otherwise)."), QT_TRANSLATE_NOOP("Glossary", "Long time below 90% is a reason to see a doctor even if AHI is low."), QT_TRANSLATE_NOOP("Glossary", "Close to 0%; more than 5% is worth attention."), false, "spo2,odi3", "" },
    { "odi3", QT_TRANSLATE_NOOP("Glossary", "ODI 3%"), QT_TRANSLATE_NOOP("Glossary", "Oxygen Desaturation Index"),
      QT_TRANSLATE_NOOP("Glossary", "Drops of SpO2 by 3% or more per hour."), QT_TRANSLATE_NOOP("Glossary", "Often rises together with AHI; a high ODI with a low AHI may mean events the device misses."), QT_TRANSLATE_NOOP("Glossary", "Below 5 an hour."), false, "odi4,spo2,ahi", "" },
    { "odi4", QT_TRANSLATE_NOOP("Glossary", "ODI 4%"), QT_TRANSLATE_NOOP("Glossary", "desaturations of 4% or more"),
      QT_TRANSLATE_NOOP("Glossary", "Drops of SpO2 by 4% or more per hour."), QT_TRANSLATE_NOOP("Glossary", "A stricter count than ODI 3%; used by some guidelines."), QT_TRANSLATE_NOOP("Glossary", "Below 5 an hour."), false, "odi3", "" },
    { "spo2_nadir", QT_TRANSLATE_NOOP("Glossary", "SpO2 nadir"), QT_TRANSLATE_NOOP("Glossary", "the lowest SpO2"),
      QT_TRANSLATE_NOOP("Glossary", "The lowest saturation of the night."), QT_TRANSLATE_NOOP("Glossary", "Single low values can be artefacts of a moved oximeter; look at how long it stayed low."), QT_TRANSLATE_NOOP("Glossary", "Ideally not below 88–90%."), false, "spo2,t90", "" },
    { "pulse", QT_TRANSLATE_NOOP("Glossary", "Pulse"), QT_TRANSLATE_NOOP("Glossary", "heart rate"),
      QT_TRANSLATE_NOOP("Glossary", "Beats per minute from the oximeter."), QT_TRANSLATE_NOOP("Glossary", "Rises after events show the body reacting to them."), QT_TRANSLATE_NOOP("Glossary", "Roughly 50–90 in sleep."), false, "pulse_change,pulse_response", "Pulse" },
    { "pulse_change", QT_TRANSLATE_NOOP("Glossary", "Pulse change"), QT_TRANSLATE_NOOP("Glossary", "sudden pulse rises"),
      QT_TRANSLATE_NOOP("Glossary", "Moments the pulse rose quickly, often after an event or an arousal."), QT_TRANSLATE_NOOP("Glossary", "Marked by the oximeter software or OSCAR. Many of them can mean disturbed sleep even with a good AHI."), "", false, "pulse,pulse_response", "PulseChange" },
    { "perfusion", QT_TRANSLATE_NOOP("Glossary", "Perfusion index"), QT_TRANSLATE_NOOP("Glossary", "strength of the finger signal"),
      QT_TRANSLATE_NOOP("Glossary", "How strong the pulse signal is at the finger."), QT_TRANSLATE_NOOP("Glossary", "A low index makes SpO2 less reliable."), "", false, "spo2", "Perf. Index" },
    { "plethy", QT_TRANSLATE_NOOP("Glossary", "Plethysmogram"), QT_TRANSLATE_NOOP("Glossary", "the pulse wave"),
      QT_TRANSLATE_NOOP("Glossary", "The oximeter's pulse wave curve."), QT_TRANSLATE_NOOP("Glossary", "A smooth regular wave means a good signal."), "", false, "spo2", "Plethy" },
    { "spo2_drop", QT_TRANSLATE_NOOP("Glossary", "SpO2 drop"), QT_TRANSLATE_NOOP("Glossary", "a desaturation"),
      QT_TRANSLATE_NOOP("Glossary", "A drop of SpO2 marked by the oximeter software."), QT_TRANSLATE_NOOP("Glossary", "OSCAR's ODI counts its own desaturations; these marks come from the oximeter's software."), "", false, "odi3", "SPO2Drop" },

    // ---- OSCAR's analysis
    { "second_opinion", QT_TRANSLATE_NOOP("Glossary", "Analysis (second opinion)"), QT_TRANSLATE_NOOP("Glossary", "OSCAR's own scoring of the flow"),
      QT_TRANSLATE_NOOP("Glossary", "OSCAR scores apneas, hypopneas and flow limitation from the recorded flow itself, independently of the device."),
      QT_TRANSLATE_NOOP("Glossary", "Useful to check the device's figures and to see what it does not report. It is not a sleep-lab diagnosis."), "", true, "an_ahi,agreement,hypopnea_rule", "" },
    { "an_ahi", QT_TRANSLATE_NOOP("Glossary", "AHI (analysis)"), QT_TRANSLATE_NOOP("Glossary", "OSCAR's own AHI"),
      QT_TRANSLATE_NOOP("Glossary", "Apneas and hypopneas per hour as OSCAR's analysis scores them from the flow."),
      QT_TRANSLATE_NOOP("Glossary", "Can differ from the device's AHI; the agreement line shows by how much."), QT_TRANSLATE_NOOP("Glossary", "Below 5 an hour."), true, "ahi,agreement,hypopnea_rule", "" },
    { "hypopnea_rule", QT_TRANSLATE_NOOP("Glossary", "Hypopnea rule"), QT_TRANSLATE_NOOP("Glossary", "AASM 3%, CMS 4%, flow only"),
      QT_TRANSLATE_NOOP("Glossary", "Which rule confirms a hypopnea: an oxygen drop of 3% (AASM) or 4% (CMS), or the flow alone."),
      QT_TRANSLATE_NOOP("Glossary", "Without an oximeter only the flow rule is possible. Auto uses the oximeter where it covers the event."), "", true, "hi,an_ahi,odi3", "" },
    { "agreement", QT_TRANSLATE_NOOP("Glossary", "Agreement with the device"), QT_TRANSLATE_NOOP("Glossary", "how far the analysis matches the device"),
      QT_TRANSLATE_NOOP("Glossary", "The share of events both the device and the analysis found."), QT_TRANSLATE_NOOP("Glossary", "Low agreement is worth a look at the differences list."), "", true, "an_ahi,ahi", "" },
    { "hypoxic_burden", QT_TRANSLATE_NOOP("Glossary", "Hypoxic burden"), QT_TRANSLATE_NOOP("Glossary", "oxygen lost to breathing events"),
      QT_TRANSLATE_NOOP("Glossary", "The area of SpO2 drops linked to breathing events, per hour (%·min/h)."),
      QT_TRANSLATE_NOOP("Glossary", "Research links a high burden with heart risk better than AHI alone; OSCAR's figure is an approximation."), QT_TRANSLATE_NOOP("Glossary", "No accepted norm; lower is better."), true, "odi3,spo2", "" },
    { "oxi_zones", QT_TRANSLATE_NOOP("Glossary", "Problem zones"), QT_TRANSLATE_NOOP("Glossary", "long stretches of poor oxygenation"),
      QT_TRANSLATE_NOOP("Glossary", "Stretches where SpO2 stayed low or kept dropping."), QT_TRANSLATE_NOOP("Glossary", "Worth showing to a doctor if frequent."), "", true, "t90,spo2", "" },
    { "unexplained_desat", QT_TRANSLATE_NOOP("Glossary", "Unexplained desaturations"), QT_TRANSLATE_NOOP("Glossary", "oxygen drops without an event"),
      QT_TRANSLATE_NOOP("Glossary", "SpO2 drops with no breathing event found around them."), QT_TRANSLATE_NOOP("Glossary", "Can be artefacts, or events the flow does not show."), "", true, "odi3,agreement", "" },
    { "pulse_response", QT_TRANSLATE_NOOP("Glossary", "Pulse response to events"), QT_TRANSLATE_NOOP("Glossary", "how much the pulse rises after events"),
      QT_TRANSLATE_NOOP("Glossary", "The average pulse rise after breathing events, in beats per minute."), QT_TRANSLATE_NOOP("Glossary", "A strong rise means the body reacts to the events with arousals; it is one of the signs research links with heart strain."), QT_TRANSLATE_NOOP("Glossary", "No accepted norm."), true, "pulse,pulse_change", "" },
    { "unscoreable", QT_TRANSLATE_NOOP("Glossary", "Unscoreable time"), QT_TRANSLATE_NOOP("Glossary", "time the analysis could not score"),
      QT_TRANSLATE_NOOP("Glossary", "Gaps, large leaks or a weak signal where the analysis did not score events."), QT_TRANSLATE_NOOP("Glossary", "This time is left out of the analysis' indices. A lot of it makes them less reliable."), "", true, "large_leak", "AnUnscoreable" },
    { "an_flags", QT_TRANSLATE_NOOP("Glossary", "Analysis flags"), QT_TRANSLATE_NOOP("Glossary", "events found by OSCAR's analysis"),
      QT_TRANSLATE_NOOP("Glossary", "The events and stretches OSCAR's analysis found, each kind on its own line."), QT_TRANSLATE_NOOP("Glossary", "Compare them with the device's event flags above; the Analysis tab lists where they differ."), "", true, "event_flags,second_opinion", "" },
    { "fl_score", QT_TRANSLATE_NOOP("Glossary", "FL score"), QT_TRANSLATE_NOOP("Glossary", "how limited each inspiration looks"),
      QT_TRANSLATE_NOOP("Glossary", "OSCAR's score of each inspiration's shape, from 0 (normal) to 1 (clearly limited)."),
      QT_TRANSLATE_NOOP("Glossary", "Computed from the flow curve when it is recorded at 10 Hz or more."), QT_TRANSLATE_NOOP("Glossary", "Mostly below 0.5."), true, "fl_time,flg", "AnFLScore" },
    { "fl_time", QT_TRANSLATE_NOOP("Glossary", "Flow limitation time"), QT_TRANSLATE_NOOP("Glossary", "time with limited breathing"),
      QT_TRANSLATE_NOOP("Glossary", "Time in runs of flow-limited breaths (3 or more in a row, or 10 seconds or more), as minutes and % of the night."),
      QT_TRANSLATE_NOOP("Glossary", "Flow limitation is a partly narrowed airway: the breath goes on but cannot get faster. A lot of it with a low AHI can still disturb sleep."), QT_TRANSLATE_NOOP("Glossary", "No accepted norm; a few % of the night is common (guide)."), true, "fl_longest,fl_breaths,fl_score,glasgow", "AnFlowLimitation" },
    { "fl_longest", QT_TRANSLATE_NOOP("Glossary", "Longest run"), QT_TRANSLATE_NOOP("Glossary", "the longest stretch of flow limitation"),
      QT_TRANSLATE_NOOP("Glossary", "The longest uninterrupted run of flow-limited breaths that night."), QT_TRANSLATE_NOOP("Glossary", "Long runs, minutes rather than seconds, point to stretches when the pressure was not enough, often in one sleep position or stage."), "", true, "fl_time", "" },
    { "fl_breaths", QT_TRANSLATE_NOOP("Glossary", "% of breaths"), QT_TRANSLATE_NOOP("Glossary", "share of flow-limited breaths"),
      QT_TRANSLATE_NOOP("Glossary", "The share of scored breaths that looked flow-limited, single ones included."), QT_TRANSLATE_NOOP("Glossary", "Higher than the % of time because single breaths count too."), "", true, "fl_time", "" },
    { "glasgow", QT_TRANSLATE_NOOP("Glossary", "Glasgow Index"), QT_TRANSLATE_NOOP("Glossary", "DaveSkvn's index of breath shapes"),
      QT_TRANSLATE_NOOP("Glossary", "The sum of the shares of breaths with each of 8 unusual shape signs; 0 is clean breathing."),
      QT_TRANSLATE_NOOP("Glossary", "Made by DaveSkvn for ResMed recordings at 25 Hz. In practice it is driven mostly by 'no pause' and 'variable amplitude' and tells flow limitation apart weakly; rely on the flow limitation time first."),
      QT_TRANSLATE_NOOP("Glossary", "Author's guide: 0–0.2 clean, about 3 serious problems."), true, "glasgow_adapted,fl_time,gi_nopause,gi_flattop", "AnGlasgowIndex" },
    { "glasgow_adapted", QT_TRANSLATE_NOOP("Glossary", "Glasgow Index (adapted)"), QT_TRANSLATE_NOOP("Glossary", "thresholds relative to the breath"),
      QT_TRANSLATE_NOOP("Glossary", "The same signs on OSCAR's own breaths, with thresholds relative to the breath's size."), QT_TRANSLATE_NOOP("Glossary", "Fairer between devices and weak breaths than the original."), QT_TRANSLATE_NOOP("Glossary", "As the original."), true, "glasgow", "AnGlasgowAdapted" },
    { "gi_skew", QT_TRANSLATE_NOOP("Glossary", "Skew"), QT_TRANSLATE_NOOP("Glossary", "lopsided inspiration"), QT_TRANSLATE_NOOP("Glossary", "Most of the inspiration's volume falls before or after its middle."), QT_TRANSLATE_NOOP("Glossary", "Flagged when less than 45% or more than 55% of the volume is in the first half."), "", true, "glasgow", "" },
    { "gi_spike", QT_TRANSLATE_NOOP("Glossary", "Spike"), QT_TRANSLATE_NOOP("Glossary", "sharp peak"), QT_TRANSLATE_NOOP("Glossary", "The inspiration spends little time near its peak: a sharp, short top."), QT_TRANSLATE_NOOP("Glossary", "Flagged when less than 20% of the inspiration is above 90% of its peak."), "", true, "glasgow", "" },
    { "gi_flattop", QT_TRANSLATE_NOOP("Glossary", "Flat top"), QT_TRANSLATE_NOOP("Glossary", "flattened inspiration"), QT_TRANSLATE_NOOP("Glossary", "The middle of the inspiration is flat: the classic sign of flow limitation."), QT_TRANSLATE_NOOP("Glossary", "Flagged when the flow in the middle half of the inspiration hardly varies."), "", true, "glasgow,fl_time", "" },
    { "gi_topheavy", QT_TRANSLATE_NOOP("Glossary", "Top heavy"), QT_TRANSLATE_NOOP("Glossary", "long time near the peak"), QT_TRANSLATE_NOOP("Glossary", "The inspiration stays near its peak for long; not counted in the index."), QT_TRANSLATE_NOOP("Glossary", "Flagged when more than 40% of the inspiration is above 90% of its peak; the author leaves it out of the sum."), "", true, "glasgow", "" },
    { "gi_multipeak", QT_TRANSLATE_NOOP("Glossary", "Double peak"), QT_TRANSLATE_NOOP("Glossary", "two humps"), QT_TRANSLATE_NOOP("Glossary", "The inspiration has two peaks with a dip between them."), QT_TRANSLATE_NOOP("Glossary", "A sign of an airway that narrows during the breath; a coarse recording can fake it, which OSCAR smooths away."), "", true, "glasgow", "" },
    { "gi_nopause", QT_TRANSLATE_NOOP("Glossary", "No pause"), QT_TRANSLATE_NOOP("Glossary", "no rest before breathing in"), QT_TRANSLATE_NOOP("Glossary", "The next breath starts without the short rest after breathing out."), QT_TRANSLATE_NOOP("Glossary", "Common on CPAP; also triggered by a small flow offset during the rest."), "", true, "glasgow", "" },
    { "gi_inspirrate", QT_TRANSLATE_NOOP("Glossary", "Inspiration rate"), QT_TRANSLATE_NOOP("Glossary", "fast breathing"), QT_TRANSLATE_NOOP("Glossary", "More than 20 inspirations a minute over the last 5 breaths."), QT_TRANSLATE_NOOP("Glossary", "Fast breathing in sleep can follow arousals or come with heart or lung problems."), "", true, "glasgow,resp_rate", "" },
    { "gi_multibreath", QT_TRANSLATE_NOOP("Glossary", "Double inspiration"), QT_TRANSLATE_NOOP("Glossary", "two breaths for one exhale"), QT_TRANSLATE_NOOP("Glossary", "A second inspiration before a real expiration."), QT_TRANSLATE_NOOP("Glossary", "Two humps of inspiration with no breath out between them, as in a sigh or a broken breath."), "", true, "glasgow", "" },
    { "gi_ampvar", QT_TRANSLATE_NOOP("Glossary", "Variable amplitude"), QT_TRANSLATE_NOOP("Glossary", "uneven breath strength"), QT_TRANSLATE_NOOP("Glossary", "The peaks of the last 5 inspirations differ markedly."), QT_TRANSLATE_NOOP("Glossary", "Unsettled breathing: alternating deep and shallow breaths, as around events or arousals."), "", true, "glasgow", "" },

    // ---- comparison
    { "best_value", QT_TRANSLATE_NOOP("Glossary", "Best value"), QT_TRANSLATE_NOOP("Glossary", "green in the comparison"),
      QT_TRANSLATE_NOOP("Glossary", "The best figure among settings used for at least 3 nights."), QT_TRANSLATE_NOOP("Glossary", "Fewer nights are too few to judge."), "", false, "few_nights", "" },
    { "few_nights", QT_TRANSLATE_NOOP("Glossary", "Few nights"), QT_TRANSLATE_NOOP("Glossary", "grey rows"),
      QT_TRANSLATE_NOOP("Glossary", "Settings used for fewer than 3 nights: too few to draw conclusions."), QT_TRANSLATE_NOOP("Glossary", "Use the settings for a few more nights before comparing them."), "", false, "best_value", "" },
};
// clang-format on

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

GlossaryEntry entry(const UiGlossaryRaw &r)
{
    GlossaryEntry e;
    e.key = QString::fromLatin1(r.key);
    e.term = tr(r.term);
    e.place = tr(r.place);
    e.summary = tr(r.summary);
    e.advice = tr(r.advice);
    e.caution = tr(r.caution);
    e.seeAlso = split(r.seeAlso);
    return e;
}

//! The keys of both tables, in table order.
QStringList allKeys()
{
    QStringList out;
    for (const Raw &r : kEntries) out << QString::fromLatin1(r.key);
    int n = 0;
    const UiGlossaryRaw *ui = uiGlossaryEntries(n);
    for (int i = 0; i < n; ++i) out << QString::fromLatin1(ui[i].key);
    return out;
}

QString folded(const QString &s)
{
    QString out = s.toLower();
    out.replace(QChar(0x0451), QChar(0x0435));   // ё → е
    return out;
}

QString caveat() { return QCoreApplication::translate("Glossary", "Experimental measure of OSCAR's analysis, not a medical norm."); }

//! Counts the language changes of the application in use, so the cached texts know when to rebuild.
class LanguageWatch : public QObject
{
  public:
    int generation = 0;
    QPointer<QCoreApplication> app;
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::LanguageChange && o == app) ++generation;
        return false;
    }
};

int languageGeneration()
{
    static LanguageWatch *watch = new LanguageWatch;
    QCoreApplication *app = QCoreApplication::instance();
    if (app && watch->app != app) {   // tests make a new application per test class
        app->installEventFilter(watch);
        watch->app = app;
        ++watch->generation;
    }
    return watch->generation;
}

} // namespace

namespace Glossary {

const GlossaryEntry *find(const QString &key)
{
    // rebuilt when the language changes: the translated texts are cached per language
    static QHash<QString, GlossaryEntry> cache;
    static int cachedFor = -1;
    const int lang = languageGeneration();
    if (cachedFor != lang) {
        cache.clear();
        for (const Raw &r : kEntries) cache.insert(QString::fromLatin1(r.key), entry(r));
        int n = 0;
        const UiGlossaryRaw *ui = uiGlossaryEntries(n);
        for (int i = 0; i < n; ++i) cache.insert(QString::fromLatin1(ui[i].key), entry(ui[i]));
        cachedFor = lang;
    }
    const auto it = cache.constFind(key);
    return it == cache.constEnd() ? nullptr : &it.value();
}

QList<GlossaryEntry> all()
{
    QList<GlossaryEntry> out;
    for (const QString &k : allKeys()) out << *find(k);
    return out;
}

QString tooltip(const QString &key)
{
    const GlossaryEntry *e = find(key);
    if (!e) return {};
    QString html = QStringLiteral("<b>%1</b>").arg(e->term.toHtmlEscaped());
    if (!e->place.isEmpty()) {
        // a control: what it does, what to choose, what it deletes
        html += QStringLiteral(" — ") + e->summary.toHtmlEscaped();
        if (!e->advice.isEmpty()) html += QStringLiteral("<br><i>%1</i>").arg(e->advice.toHtmlEscaped());
        if (!e->caution.isEmpty()) html += QStringLiteral("<br><span style='color:#c0392b'>⚠ %1</span>").arg(e->caution.toHtmlEscaped());
        return html;
    }
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
    if (!e->place.isEmpty()) {
        html += QStringLiteral("<p><i>%1</i></p>").arg(e->place.toHtmlEscaped());
        html += heading(QT_TRANSLATE_NOOP("Glossary", "What it does")) + QStringLiteral("<p>%1</p>").arg(e->summary.toHtmlEscaped());
        if (!e->advice.isEmpty()) {
            html += heading(QT_TRANSLATE_NOOP("Glossary", "Advice")) + QStringLiteral("<p>%1</p>").arg(e->advice.toHtmlEscaped());
        }
        if (!e->caution.isEmpty()) {
            html += heading(QT_TRANSLATE_NOOP("Glossary", "Caution"))
                    + QStringLiteral("<p style='color:#c0392b'>⚠ %1</p>").arg(e->caution.toHtmlEscaped());
        }
    } else {
        html += heading(QT_TRANSLATE_NOOP("Glossary", "What it is")) + QStringLiteral("<p>%1</p>").arg(e->summary.toHtmlEscaped());
    }
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
        else if (folded(e.summary + QLatin1Char(' ') + e.details + QLatin1Char(' ') + e.place + QLatin1Char(' ') + e.advice)
                     .contains(q)) byText << e.key;
    }
    return byTerm + byText;
}

} // namespace Glossary
