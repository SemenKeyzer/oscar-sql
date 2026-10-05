/* HTML Pages
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "htmlpages.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QPrinter>
#include <QTextDocument>
#include <QUrl>
#include <cmath>

// The DPI QTextDocument lays text out at (QtGui, private header qfont_p.h).
Q_GUI_EXPORT int qt_defaultDpiY();

QSizeF htmlPageSize(QPrinter &printer)
{
    return printer.pageRect(QPrinter::Point).size() * (qt_defaultDpiY() / 72.0);
}

int paintHtmlPages(QPainter &painter, QPrinter &printer, const QString &html, const QFont &font,
                   const QHash<QString, QImage> &images, bool startOnNewPage)
{
    QTextDocument doc;
    const QSizeF page = htmlPageSize(printer);
    doc.setPageSize(page);
    doc.setDocumentMargin(0);
    doc.setDefaultFont(font);
    for (auto it = images.cbegin(); it != images.cend(); ++it) {
        doc.addResource(QTextDocument::ImageResource, QUrl(it.key()), it.value());
    }
    doc.setHtml(html);

    const int pages = std::max(1, doc.pageCount());
    // layout units to the printer's device pixels
    const double scale = printer.pageRect(QPrinter::DevicePixel).width() / page.width();
    for (int i = 0; i < pages; ++i) {
        if (i > 0 || startOnNewPage) printer.newPage();
        painter.save();
        painter.scale(scale, scale);
        painter.translate(0, -i * page.height());
        doc.drawContents(&painter, QRectF(0, i * page.height(), page.width(), page.height()));
        painter.restore();
    }
    return pages;
}
