/* Reports Header
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef REPORTS_H
#define REPORTS_H
#include "Graphs/gGraphView.h"

class ProgressDialog;
class QPainter;
class QPrinter;

//! What a report drawn onto a page shows besides the graphs, and where it reports progress.
struct PrintTarget {
    bool personalData = true;           //!< the user's name, birth date and contact details
    bool bookmarks = false;             //!< the Daily report's bookmarked areas instead of the whole day
    ProgressDialog *progress = nullptr; //!< may be null
};

class Report
{
  public:
    Report();

    /*! \fn void PrintReport gGraphView *gv,QString name, QDate date=QDate::currentDate());
        \brief Prepares a report using gGraphView object, and sends to a created QPrinter object
        \param gGraphView *gv  GraphView Object containing which graph set to print
        \param QString name   Report Title
        \param QDate date
        */
    static void PrintReport(gGraphView *gv, QString name, QDate date = QDate::currentDate());

    /*! \brief Draws the report of \a gv onto \a printer with \a painter (already begun), from the
        painter's current page on: the header and the visible graphs, six to a page. Leaves the
        painter's state as it found it. Returns false when the printer could not take a page. */
    static bool paint(QPainter &painter, QPrinter &printer, gGraphView *gv, const QString &name,
                      const QDate &date, const PrintTarget &target);
};

#endif // REPORTS_H
