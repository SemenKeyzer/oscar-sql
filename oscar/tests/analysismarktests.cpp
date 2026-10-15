/* Analysis mark Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "glossary.h"
#include "Graphs/gAnalysisCharts.h"
#include "analysismarktests.h"
#include "analysismark.h"
#include "SleepLib/schema.h"
#include "SleepLib/analysis/analysis_channels.h"

#include <QApplication>
#include <QTreeWidgetItem>

void AnalysisMarkTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
    if (CPAP_Obstructive == 0) schema::init();
}

void AnalysisMarkTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

void AnalysisMarkTests::testPill()
{
    const QString p = AnalysisMark::pill();
    QVERIFY(p.contains(QStringLiteral("analysis")));
    QVERIFY(p.contains(QStringLiteral("#EEEDFE"), Qt::CaseInsensitive));
    QVERIFY(p.contains(QStringLiteral("#3C3489"), Qt::CaseInsensitive));
    QVERIFY(p.contains(QStringLiteral("second opinion, experimental")));
}

// Without colour the mark still reads as a word in brackets.
void AnalysisMarkTests::testPrintPill()
{
    const QString p = AnalysisMark::printPill();
    QVERIFY(p.contains(QStringLiteral("[analysis]")));
    QVERIFY(!p.contains(QStringLiteral("background")));
}

void AnalysisMarkTests::testHeadingRow()
{
    const QString row = AnalysisMark::headingRow(QStringLiteral("OSCAR analysis"), 2);
    QVERIFY(row.contains(QStringLiteral("colspan=2")));
    QVERIFY(row.contains(QStringLiteral("#7F77DD"), Qt::CaseInsensitive));
    QVERIFY(row.contains(QStringLiteral("<b>OSCAR analysis</b>")));
    QVERIFY(row.contains(AnalysisMark::pill()));
}

void AnalysisMarkTests::testTitle()
{
    QCOMPARE(AnalysisMark::title(QStringLiteral("AHI")), QStringLiteral("AHI · analysis"));
}

// Group headings in the events tree cannot be chosen; the analysis one carries the mark's look.
void AnalysisMarkTests::testTreeHeading()
{
    QScopedPointer<QTreeWidgetItem> an(AnalysisMark::treeHeading(QStringLiteral("OSCAR analysis"), true));
    QVERIFY(!(an->flags() & Qt::ItemIsSelectable));
    QVERIFY(!an->data(0, Qt::UserRole).isValid());
    QVERIFY(an->data(0, AnalysisMark::HeadingRole).toBool());
    QCOMPARE(an->background(0).color(), QColor(QStringLiteral("#EEEDFE")));
    QVERIFY(!an->icon(0).isNull());
    QVERIFY(an->font(0).bold());

    QScopedPointer<QTreeWidgetItem> manual(AnalysisMark::treeHeading(QStringLiteral("Manual scoring"), false));
    QVERIFY(!(manual->flags() & Qt::ItemIsSelectable));
    QVERIFY(manual->icon(0).isNull());
    QVERIFY(manual->font(0).bold());
}

void AnalysisMarkTests::testChannelBaseName()
{
    QCOMPARE(analysis::channelBaseName(AN_ObstructiveApnea), QStringLiteral("Obstructive Apnea"));
    QCOMPARE(schema::channel[AN_ObstructiveApnea].fullname(), AnalysisMark::title(QStringLiteral("Obstructive Apnea")));
    QVERIFY(analysis::channelBaseName(CPAP_Obstructive).isEmpty());
}

// Qt's rich text draws no per-cell border in these tables: the bar is a purple glyph that shows on screen and in print.
void AnalysisMarkTests::testBarMarkIsText()
{
    const QString bar = AnalysisMark::barMark();
    QVERIFY(bar.contains(QChar(0x258C)));
    QVERIFY(bar.contains(QStringLiteral("#7F77DD"), Qt::CaseInsensitive));
    QVERIFY(AnalysisMark::headingRow(QStringLiteral("OSCAR analysis"), 2).contains(bar));
    QVERIFY(!AnalysisMark::headingRow(QStringLiteral("OSCAR analysis"), 2).contains(QStringLiteral("border-left")));
}

// A page printed from its screen HTML gets the printable mark.
void AnalysisMarkTests::testForPrint()
{
    const QString screen = QStringLiteral("<td>AHI ") + AnalysisMark::pill() + QStringLiteral("</td>");
    const QString print = AnalysisMark::forPrint(screen);
    QVERIFY(!print.contains(AnalysisMark::pill()));
    QVERIFY(print.contains(AnalysisMark::printPill()));
}

// The heading marker has a role of its own, apart from the manual edits' id role (Qt::UserRole + 1).
void AnalysisMarkTests::testHeadingRole()
{
    QVERIFY(AnalysisMark::HeadingRole != Qt::UserRole + 1);
    QScopedPointer<QTreeWidgetItem> h(AnalysisMark::treeHeading(QStringLiteral("x"), true));
    QVERIFY(h->data(0, AnalysisMark::HeadingRole).toBool());
    QVERIFY(!h->data(0, Qt::UserRole + 1).isValid());
}

// No "(analysis)" spelling is left in the glossary term or the Overview axis.
void AnalysisMarkTests::testOldNamesGone()
{
    const GlossaryEntry *e = Glossary::find(QStringLiteral("an_ahi"));
    QVERIFY(e);
    QVERIFY2(!e->term.contains(QStringLiteral("(analysis)")), qPrintable(e->term));
    QVERIFY(!gAnalysisChart::units(gAnalysisChart::Ahi).contains(QStringLiteral("(analysis)")));
}
