/* PDF Report Writer Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PDFREPORTWRITER_H
#define PDFREPORTWRITER_H

#include <QDate>
#include <QString>

#include "pdfreportoptions.h"

class Daily;
class Overview;
class ProgressDialog;

//! Writes the combined PDF report: the summary page, nights as the Daily tab prints them, the
//! Overview's graphs and the Statistics, each section from a new page.
class PdfReportWriter
{
  public:
    //! \a daily and \a overview may be null; their sections are then left out.
    PdfReportWriter(Daily *daily, Overview *overview);

    //! Writes the report \a o describes, up to \a lastNight, to \a path. False with \a error set
    //! when there is nothing to report or the file could not be written; false and cancelled()
    //! after cancel(). A file not finished is removed.
    bool write(const PdfReportOptions &o, const QDate &lastNight, const QString &path,
               ProgressDialog *progress, QString *error);
    //! Stops the report at the next night or section.
    void cancel() { m_cancelled = true; }
    bool cancelled() const { return m_cancelled; }

  private:
    Daily *m_daily;
    Overview *m_overview;
    bool m_cancelled = false;
};

#endif // PDFREPORTWRITER_H
