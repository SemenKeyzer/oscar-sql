/* HTML Pages Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef HTMLPAGES_H
#define HTMLPAGES_H

#include <QFont>
#include <QHash>
#include <QImage>
#include <QSizeF>
#include <QString>

class QPainter;
class QPrinter;

//! Size of a printer page in QTextDocument layout units. QTextDocument turns point sizes into
//! layout units at the screen's DPI (72 on macOS, 96 on Windows and Linux); sizing the page the
//! same way keeps text its size on paper wherever OSCAR runs.
QSizeF htmlPageSize(QPrinter &printer);

//! Lays \a html out on \a printer's pages and draws them with \a painter (already begun on the
//! printer) from its current page on, or from a new page with \a startOnNewPage. \a images are
//! the document's image resources by name. Returns the number of pages drawn.
int paintHtmlPages(QPainter &painter, QPrinter &printer, const QString &html, const QFont &font,
                   const QHash<QString, QImage> &images, bool startOnNewPage);

#endif // HTMLPAGES_H
