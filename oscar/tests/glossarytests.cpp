/* Glossary Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "glossarytests.h"

#include <QSet>

#include "SleepLib/schema.h"
#include "glossary.h"

void GlossaryTests::initTestCase()
{
    if (CPAP_Obstructive == 0) schema::init();
}

void GlossaryTests::testEntriesComplete()
{
    const QList<GlossaryEntry> entries = Glossary::all();
    QVERIFY2(entries.size() >= 75, qPrintable(QString::number(entries.size())));
    QSet<QString> keys;
    for (const GlossaryEntry &e : entries) {
        // the controls' entries (ui.*) have no details; they are checked in testUiEntriesComplete
        QVERIFY2(!e.key.isEmpty() && !e.term.isEmpty() && !e.summary.isEmpty()
                 && (e.key.startsWith(QLatin1String("ui.")) || !e.details.isEmpty()), qPrintable(e.key));
        QVERIFY2(e.summary.size() <= 300, qPrintable(e.key));
        QVERIFY2(!keys.contains(e.key), qPrintable(e.key));
        keys.insert(e.key);
    }
    for (const GlossaryEntry &e : entries) {
        for (const QString &k : e.seeAlso) QVERIFY2(keys.contains(k), qPrintable(e.key + QStringLiteral(" -> ") + k));
    }
}

void GlossaryTests::testRequiredKeys()
{
    const QStringList required {
        "usage", "compliance", "sessions", "mask_off", "ahi", "rdi", "oai", "cai", "uai", "all_apnea", "hi", "oh", "ch",
        "rera", "fl_device", "flg", "csr", "large_leak", "leak", "leak_total", "leak_redline", "pressure", "pressure_set",
        "epap", "ipap", "ps", "pressure_max_time", "mode", "relief", "ramp", "apap_range", "resp_rate", "tidal_volume",
        "minute_vent", "ti_te", "ie_ratio", "snore", "flow_rate", "mask_pressure", "event_flags", "sensawake", "user_flags",
        "median", "p95", "maximum", "wavg", "nights_with_data", "compliance_pct", "period",
        "spo2", "t90", "odi3", "odi4", "spo2_nadir", "pulse", "pulse_change", "perfusion", "plethy", "spo2_drop",
        "second_opinion", "an_ahi", "hypopnea_rule", "agreement", "hypoxic_burden", "oxi_zones", "unexplained_desat",
        "pulse_response", "unscoreable", "an_flags", "fl_score", "fl_time", "fl_longest", "fl_breaths", "glasgow",
        "glasgow_adapted", "gi_skew", "gi_spike", "gi_flattop", "gi_topheavy", "gi_multipeak", "gi_nopause",
        "gi_inspirrate", "gi_multibreath", "gi_ampvar", "best_value", "few_nights",
    };
    for (const QString &k : required) QVERIFY2(Glossary::find(k) != nullptr, qPrintable(k));
}

void GlossaryTests::testTooltipAndPanel()
{
    const QString ahi = Glossary::tooltip(QStringLiteral("ahi"));
    QVERIFY(ahi.contains(QStringLiteral("AHI")));
    QVERIFY(ahi.contains(Glossary::find(QStringLiteral("ahi"))->norm.toHtmlEscaped()));
    const QString glasgow = Glossary::panel(QStringLiteral("glasgow"));
    QVERIFY(glasgow.contains(QStringLiteral("help:glasgow_adapted")));
    QVERIFY(glasgow.contains(QCoreApplication::translate("Glossary", "Experimental measure of OSCAR's analysis, not a medical norm.")));
    QVERIFY(Glossary::tooltip(QStringLiteral("nope")).isEmpty());
    QVERIFY(Glossary::panel(QStringLiteral("nope")).isEmpty());
}

void GlossaryTests::testChannelFallback()
{
    QCOMPARE(Glossary::keyForChannel(CPAP_Obstructive), QStringLiteral("oai"));
    QCOMPARE(Glossary::keyForChannel(CPAP_Leak), QStringLiteral("leak"));
    QVERIFY(Glossary::keyForChannel(CPAP_Test1).isEmpty());
    const QString fallback = Glossary::channelTooltip(CPAP_Test1);
    QVERIFY(!fallback.isEmpty());
    QVERIFY(fallback.contains(schema::channel[CPAP_Test1].description().toHtmlEscaped()));
    QVERIFY(Glossary::channelTooltip(CPAP_Obstructive).contains(QStringLiteral("OAI")));
}

void GlossaryTests::testSearch()
{
    QVERIFY(Glossary::search(QStringLiteral("leak")).contains(QStringLiteral("leak")));
    QVERIFY(Glossary::search(QStringLiteral("LEAK")).contains(QStringLiteral("leak")));
    QVERIFY(Glossary::search(QStringLiteral("glasgow")).contains(QStringLiteral("glasgow_adapted")));
    QVERIFY(Glossary::search(QStringLiteral("x")).size() <= Glossary::all().size());
    QVERIFY(Glossary::search(QString()).isEmpty());
}

namespace {
const QString kPurgeDay = QStringLiteral("ui.menu.actionPurgeCurrentDayAll");
}

void GlossaryTests::testUiEntriesComplete()
{
    QSet<QString> keys;
    for (const GlossaryEntry &e : Glossary::all()) keys.insert(e.key);
    int ui = 0;
    for (const GlossaryEntry &e : Glossary::all()) {
        if (!e.key.startsWith(QLatin1String("ui."))) continue;
        ++ui;
        QVERIFY2(!e.term.isEmpty() && !e.place.isEmpty() && !e.summary.isEmpty(), qPrintable(e.key));
        QVERIFY2(e.summary.size() <= 300, qPrintable(e.key));
        for (const QString &k : e.seeAlso) QVERIFY2(keys.contains(k), qPrintable(e.key + QStringLiteral(" -> ") + k));
    }
    QVERIFY(ui > 0);
    QCOMPARE(keys.size(), Glossary::all().size());   // unique across both tables
}

void GlossaryTests::testCautionOnlyWhereListed()
{
    QSet<QString> withCaution;
    for (const GlossaryEntry &e : Glossary::all()) {
        if (!e.caution.isEmpty()) withCaution.insert(e.key);
    }
    const QStringList listed = Glossary::cautionKeys();
    QVERIFY(!listed.isEmpty());
    const QSet<QString> expected(listed.cbegin(), listed.cend());
    QVERIFY2(withCaution == expected,
             qPrintable(QStringList((withCaution - expected).values()).join(QStringLiteral(", ")) + QStringLiteral(" | missing: ")
                        + QStringList((expected - withCaution).values()).join(QStringLiteral(", "))));
}

void GlossaryTests::testUiTooltipShowsAdviceAndCaution()
{
    const GlossaryEntry *e = Glossary::find(kPurgeDay);
    QVERIFY(e);
    QVERIFY(!e->caution.isEmpty());
    const QString tip = Glossary::tooltip(kPurgeDay);
    QVERIFY(tip.contains(QStringLiteral("<b>") + e->term.toHtmlEscaped() + QStringLiteral("</b>")));
    QVERIFY(tip.contains(e->caution.toHtmlEscaped()));
    QVERIFY(tip.contains(QStringLiteral("color:")));
    if (!e->advice.isEmpty()) QVERIFY(tip.contains(e->advice.toHtmlEscaped()));
    QVERIFY(Glossary::panel(kPurgeDay).contains(e->place.toHtmlEscaped()));

    // an entry with advice shows it
    for (const GlossaryEntry &a : Glossary::all()) {
        if (a.key.startsWith(QLatin1String("ui.")) && !a.advice.isEmpty()) {
            QVERIFY(Glossary::tooltip(a.key).contains(a.advice.toHtmlEscaped()));
            QVERIFY(Glossary::panel(a.key).contains(a.advice.toHtmlEscaped()));
            return;
        }
    }
    QFAIL("no UI entry with advice");
}

void GlossaryTests::testSearchFindsUiEntries()
{
    QVERIFY(Glossary::search(QStringLiteral("purge")).contains(kPurgeDay));
}

// texts the final review found contradicting what the code does
void GlossaryTests::testExplanationsMatchCode()
{
    auto e = [](const char *key) {
        const GlossaryEntry *entry = Glossary::find(QString::fromLatin1(key));
        if (!entry) qFatal("no entry %s", key);
        return *entry;
    };
    // MainWindow::on_action_Rebuild_Oximetry_Index_triggered drops short pieces and the drop/change flags
    QVERIFY(e("ui.menu.action_Rebuild_Oximetry_Index").summary.contains(QStringLiteral("Discard short pieces")));
    // OximeterImport::onBluetoothFinished accepts the dialog
    QVERIFY(e("ui.oximport.bluetoothDoneButton").summary.contains(QStringLiteral("closes the wizard")));
    // the reminder asks to put the card back into the device
    QVERIFY(e("ui.prefs.removeCardNotificationCheckbox").summary.contains(QStringLiteral("back into")));
    // AnalysisParams::limitOxiToCpap is false by default
    QVERIFY(e("ui.prefs.limitOxi").advice.contains(QStringLiteral("Off by default")));
    // devices without card backups (oximeters) are lost for good
    QVERIFY(e("ui.menu.menuPurge_CPAP_Data").caution.contains(QStringLiteral("for good")));
    QVERIFY(!e("ui.menu.menuPurge_CPAP_Data").advice.contains(QStringLiteral("backup")));
    // the cable erase box is wired to nothing
    QVERIFY(e("ui.oximport.cms50EraseAfterwards").summary.contains(QStringLiteral("no effect")));
    QVERIFY(e("ui.oximport.cms50EraseAfterwards").caution.isEmpty());
    // schema::resetChannels() applies at once, before OK or Cancel
    QVERIFY(e("ui.prefs.resetChannelDefaults").caution.contains(QStringLiteral("Cancel")));
    QVERIFY(e("ui.prefs.resetWaveformChannels").caution.contains(QStringLiteral("Cancel")));
}
