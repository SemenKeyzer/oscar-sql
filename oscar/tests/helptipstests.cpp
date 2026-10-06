/* Hover Explanations Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "helptipstests.h"

#include <QApplication>
#include <QHelpEvent>
#include <QLabel>
#include <QSignalSpy>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolTip>

#include "SleepLib/schema.h"
#include "glossary.h"
#include "helptips.h"

void HelpTipsTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
    if (CPAP_Obstructive == 0) schema::init();
    HelpTips::instance()->setEnabled(true);
}

void HelpTipsTests::cleanupTestCase()
{
    HelpTips::instance()->setEnabled(true);
    delete m_app;
    m_app = nullptr;
}

void HelpTipsTests::testAttachShowsTooltip()
{
    QLabel label(QStringLiteral("AHI"));
    label.resize(100, 30);
    label.show();
    HelpTips::attach(&label, QStringLiteral("ahi"));
    QSignalSpy hovered(HelpTips::instance(), &HelpTips::hovered);

    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(&label, &enter);
    QCOMPARE(hovered.count(), 1);
    QCOMPARE(hovered.first().first().toString(), QStringLiteral("ahi"));

    QHelpEvent help(QEvent::ToolTip, QPoint(5, 5), label.mapToGlobal(QPoint(5, 5)));
    QApplication::sendEvent(&label, &help);
    QVERIFY2(QToolTip::text().contains(QStringLiteral("AHI")), qPrintable(QToolTip::text()));
    QToolTip::hideText();
}

void HelpTipsTests::testDisabled()
{
    QLabel label(QStringLiteral("AHI"));
    label.show();
    HelpTips::attach(&label, QStringLiteral("ahi"));
    HelpTips::instance()->setEnabled(false);
    QSignalSpy hovered(HelpTips::instance(), &HelpTips::hovered);
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(&label, &enter);
    QHelpEvent help(QEvent::ToolTip, QPoint(5, 5), label.mapToGlobal(QPoint(5, 5)));
    QApplication::sendEvent(&label, &help);
    QCOMPARE(hovered.count(), 0);
    QVERIFY(!QToolTip::text().contains(QStringLiteral("Apnea-Hypopnea")));
    QCOMPARE(HelpTips::term(QStringLiteral("AHI"), QStringLiteral("ahi")), QStringLiteral("AHI"));
    HelpTips::instance()->setEnabled(true);
}

void HelpTipsTests::testTerm()
{
    const QString t = HelpTips::term(QStringLiteral("Leak"), QStringLiteral("leak"));
    QVERIFY(t.contains(QStringLiteral("href='help:leak'")));
    QVERIFY(t.contains(QStringLiteral(">Leak</a>")));
    QVERIFY(t.contains(QStringLiteral("text-decoration:none")));
    QCOMPARE(HelpTips::term(QStringLiteral("x"), QStringLiteral("no_such_key")), QStringLiteral("x"));
    QCOMPARE(HelpTips::keyOf(QUrl(QStringLiteral("help:leak"))), QStringLiteral("leak"));
    QVERIFY(HelpTips::keyOf(QUrl(QStringLiteral("daily=2026-10-01"))).isEmpty());
}

// The explanation goes into a title attribute: quotes, < and % must not break the page. The
// text is the page's own HTML, already escaped.
void HelpTipsTests::testTermEscapes()
{
    const QString page = QStringLiteral("<p>%1 and more</p>").arg(HelpTips::term(QStringLiteral("SpO2 &lt; 90%"), QStringLiteral("t90")));
    QTextDocument doc;
    doc.setHtml(page);
    QCOMPARE(doc.toPlainText(), QStringLiteral("SpO2 < 90% and more"));
    int anchors = 0;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isAnchor()) {
                ++anchors;
                QCOMPARE(it.fragment().charFormat().anchorHref(), QStringLiteral("help:t90"));
                QVERIFY(it.fragment().charFormat().toolTip().contains(QStringLiteral("90")));
            }
        }
    }
    QCOMPARE(anchors, 1);
}
