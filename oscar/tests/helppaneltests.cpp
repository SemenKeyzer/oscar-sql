/* Help Panel Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "helppaneltests.h"

#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QTextBrowser>

#include "SleepLib/schema.h"
#include "helppanel.h"
#include "helptips.h"

namespace {

QString text(HelpPanel &p)
{
    return p.findChild<QTextBrowser *>()->toPlainText();
}

} // namespace

void HelpPanelTests::initTestCase()
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

void HelpPanelTests::cleanupTestCase()
{
    HelpTips::instance()->setEnabled(true);
    delete m_app;
    m_app = nullptr;
}

void HelpPanelTests::testShowsHoveredArticle()
{
    HelpPanel panel;
    QVERIFY(panel.currentKey().isEmpty());
    QVERIFY(text(panel).contains(QStringLiteral("Hover over a figure or a term")));   // the empty state
    emit HelpTips::instance()->hovered(QStringLiteral("odi3"));
    QCOMPARE(panel.currentKey(), QStringLiteral("odi3"));
    QVERIFY(text(panel).contains(QStringLiteral("ODI")));
}

void HelpPanelTests::testKeepsArticle()
{
    HelpPanel panel;
    panel.show(QStringLiteral("leak"));
    HelpTips::instance()->hover(QString());   // moving off a term
    panel.show(QStringLiteral("no_such_key"));
    QCOMPARE(panel.currentKey(), QStringLiteral("leak"));
}

void HelpPanelTests::testSeeAlso()
{
    HelpPanel panel;
    panel.show(QStringLiteral("glasgow"));
    QTextBrowser *browser = panel.findChild<QTextBrowser *>();
    emit browser->anchorClicked(QUrl(QStringLiteral("help:glasgow_adapted")));
    QCOMPARE(panel.currentKey(), QStringLiteral("glasgow_adapted"));
}

void HelpPanelTests::testSearch()
{
    HelpPanel panel;
    QLineEdit *search = panel.findChild<QLineEdit *>();
    QListWidget *results = panel.findChild<QListWidget *>();
    search->setText(QStringLiteral("glasgow"));
    QVERIFY(results->count() >= 2);
    QVERIFY(!results->isHidden());
    QListWidgetItem *adapted = nullptr;
    for (int i = 0; i < results->count(); ++i) {
        if (results->item(i)->data(Qt::UserRole).toString() == QLatin1String("glasgow_adapted")) adapted = results->item(i);
    }
    QVERIFY(adapted);
    emit results->itemClicked(adapted);
    QCOMPARE(panel.currentKey(), QStringLiteral("glasgow_adapted"));
    // choosing a result with the keyboard (or a screen reader) shows it too
    for (int i = 0; i < results->count(); ++i) {
        if (results->item(i)->data(Qt::UserRole).toString() == QLatin1String("glasgow")) results->setCurrentRow(i);
    }
    QCOMPARE(panel.currentKey(), QStringLiteral("glasgow"));
    search->clear();
    QVERIFY(results->isHidden());
}

void HelpPanelTests::testPanelWorksWithHoverOff()
{
    HelpTips::instance()->setEnabled(false);
    HelpPanel panel;
    panel.findChild<QLineEdit *>()->setText(QStringLiteral("leak"));
    QVERIFY(panel.findChild<QListWidget *>()->count() >= 1);
    panel.show(QStringLiteral("ahi"));
    emit panel.findChild<QTextBrowser *>()->anchorClicked(QUrl(QStringLiteral("help:oai")));
    QCOMPARE(panel.currentKey(), QStringLiteral("oai"));
    HelpTips::instance()->setEnabled(true);
}

void HelpPanelTests::testOpenRequestedShowsPanel()
{
    QMainWindow window;
    HelpPanel *panel = new HelpPanel(&window);
    window.addDockWidget(Qt::RightDockWidgetArea, panel);
    panel->hide();
    window.show();
    HelpTips::instance()->open(QStringLiteral("leak"));
    QVERIFY(panel->isVisible());
    QCOMPARE(panel->currentKey(), QStringLiteral("leak"));
}
