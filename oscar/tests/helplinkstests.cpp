/* Help Links Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "helplinkstests.h"

#include <QApplication>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include "SleepLib/schema.h"
#include "Graphs/gAnalysisCharts.h"
#include "daily.h"
#include "glossary.h"
#include "helptips.h"
#include "overview.h"
#include "mytextbrowser.h"

namespace {

// The centre of the first character of the anchor \a href in \a browser's viewport.
QPoint anchorPoint(MyTextBrowser &browser, const QString &href)
{
    QTextDocument *doc = browser.document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().anchorHref() == href) {
                QTextCursor c(doc);
                c.setPosition(it.fragment().position() + 1);
                return browser.cursorRect(c).center() - QPoint(2, 0);
            }
        }
    }
    return {};
}

} // namespace

void HelpLinksTests::initTestCase()
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

void HelpLinksTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

// A page with its own links: a help term opens the explanation and the page stays.
void HelpLinksTests::testHelpLinkDoesNotNavigate()
{
    MyTextBrowser browser(nullptr);
    browser.setOpenLinks(false);
    browser.resize(400, 200);
    const QString html = QStringLiteral("<p>%1 &nbsp; &nbsp; <a href='daily=2026-10-01'>day</a></p>")
                             .arg(HelpTips::term(QStringLiteral("AHI value"), QStringLiteral("ahi")));
    browser.setHtml(html);
    browser.show();
    QVERIFY(QTest::qWaitForWindowExposed(&browser));
    QSignalSpy clicked(&browser, &QTextBrowser::anchorClicked);
    QSignalSpy opened(HelpTips::instance(), &HelpTips::openRequested);
    QSignalSpy hovered(HelpTips::instance(), &HelpTips::hovered);
    const QString before = browser.toHtml();

    const QPoint help = anchorPoint(browser, QStringLiteral("help:ahi"));
    QVERIFY(!help.isNull());
    QMouseEvent move(QEvent::MouseMove, help, browser.viewport()->mapToGlobal(help), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(browser.viewport(), &move);
    QTRY_VERIFY(hovered.count() >= 1);
    QCOMPARE(hovered.last().first().toString(), QStringLiteral("ahi"));
    QTest::mouseClick(browser.viewport(), Qt::LeftButton, Qt::NoModifier, help);
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.first().first().toString(), QStringLiteral("ahi"));
    for (const QList<QVariant> &args : clicked) QVERIFY(args.first().toUrl().scheme() != QLatin1String("help"));
    QCOMPARE(browser.toHtml(), before);

    QTest::mouseClick(browser.viewport(), Qt::LeftButton, Qt::NoModifier, anchorPoint(browser, QStringLiteral("daily=2026-10-01")));
    QCOMPARE(clicked.count(), 1);   // the page's own link still works
}

// Every graph of Daily and of Overview's analysis set explains itself: its own entry, or its
// channel's description.
void HelpLinksTests::testGraphKeysComplete()
{
    for (const QString &name : Daily::standardGraphNames()) {
        const QString key = Daily::helpKeyForGraph(name);
        const bool known = !key.isEmpty() && Glossary::find(key);
        const bool channelText = !Glossary::channelTooltip(schema::channel[name].id()).isEmpty();
        QVERIFY2(known || channelText, qPrintable(name));
    }
    QCOMPARE(Daily::helpKeyForGraph(QStringLiteral("SF")), QStringLiteral("event_flags"));
    QCOMPARE(Daily::helpKeyForGraph(QStringLiteral("AnGlasgowIndex")), QStringLiteral("glasgow"));
    for (gAnalysisChart::Kind kind : gAnalysisChart::kinds()) {
        const QString key = Overview::helpKeyForGraph(gAnalysisChart::code(kind));
        QVERIFY2(Glossary::find(key), qPrintable(gAnalysisChart::code(kind)));
    }
    QCOMPARE(Overview::helpKeyForGraph(QStringLiteral("AHIBreakdown")), QStringLiteral("ahi"));
    QCOMPARE(Overview::helpKeyForGraph(QStringLiteral("Leak")), QStringLiteral("leak"));
}
