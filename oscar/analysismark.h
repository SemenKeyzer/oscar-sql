/* Analysis mark
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSISMARK_H
#define ANALYSISMARK_H

#include <QString>

class QTreeWidgetItem;

//! One look for everything OSCAR's own analysis computed, on every page: the word "analysis"
//! in a rounded pill and a purple bar along the row.
namespace AnalysisMark {

extern const char *const BarColor;        //!< the bar along an analysis row
extern const char *const PillBackground;
extern const char *const PillText;

//! The pill for HTML pages; hovering it explains what the analysis is.
QString pill();
//! The pill for printing and PDFs: readable in black and white.
QString printPill();
//! Inline CSS for a table cell of an analysis row: the bar on its left.
QString rowStyle();
//! A table row heading the analysis rows: bold \a title and the pill, across \a colspan columns.
QString headingRow(const QString &title, int colspan);
//! "<base> · analysis", the name of a chart or channel of the analysis.
QString title(const QString &base);
//! A heading row of the events tree that cannot be chosen; \a analysis gives it the mark's look.
QTreeWidgetItem *treeHeading(const QString &text, bool analysis);

} // namespace AnalysisMark

#endif // ANALYSISMARK_H
