/* HTML Pages Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "htmlpagestests.h"

#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QPrinter>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "htmlpages.h"

namespace {

QString longTable()
{
    QString html = QStringLiteral("<table width='100%'>");
    for (int i = 0; i < 200; ++i) html += QStringLiteral("<tr><td>Row %1</td><td>%2</td></tr>").arg(i).arg(i * 3);
    return html + QStringLiteral("</table>");
}

int pdfPages(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return -1;
    return int(QString::fromLatin1(f.readAll()).count(QRegularExpression(QStringLiteral("/Type\\s*/Page[^s]"))));
}

// Prints \a first (if any) on page one, then \a html with paintHtmlPages; returns its page count.
int print(const QString &path, const QString &html, bool afterFirstPage)
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageSize(QPageSize(QPageSize::A4));
    QPainter painter(&printer);
    if (afterFirstPage) painter.drawText(100, 100, QStringLiteral("first page"));
    QFont font(QStringLiteral("Helvetica"));
    font.setPointSizeF(9);
    const int pages = paintHtmlPages(painter, printer, html, font, {}, afterFirstPage);
    painter.end();
    return pages;
}

} // namespace

void HtmlPagesTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
}

void HtmlPagesTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

void HtmlPagesTests::testShortHtml()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("short.pdf"));
    QCOMPARE(print(path, QStringLiteral("<p>One</p><p>Two</p><p>Three</p>"), false), 1);
    QCOMPARE(pdfPages(path), 1);
}

void HtmlPagesTests::testLongTable()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("long.pdf"));
    const int pages = print(path, longTable(), false);
    QVERIFY2(pages > 1, qPrintable(QString::number(pages)));
    QCOMPARE(pdfPages(path), pages);
}

void HtmlPagesTests::testAfterAnotherPage()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("after.pdf"));
    const int pages = print(path, longTable(), true);
    QVERIFY(pages > 1);
    QCOMPARE(pdfPages(path), 1 + pages);
}
