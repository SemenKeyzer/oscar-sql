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
#include <qnamespace.h>

class QTreeWidgetItem;

//! One look for everything OSCAR's own analysis computed, on every page: the word "analysis"
//! in a rounded pill and a purple bar along the row.
namespace AnalysisMark {

extern const char *const BarColor;        //!< the bar along an analysis row
extern const char *const PillBackground;
extern const char *const PillText;

//! A tree heading's marker role (apart from the manual edits' Qt::UserRole + 1).
constexpr int HeadingRole = Qt::UserRole + 10;

//! What the analysis is, for hover texts.
QString hoverText();
//! The pill for HTML pages; hovering it explains what the analysis is.
QString pill();
//! \a html with every screen pill turned into the printable one, for printing a page built for the screen.
QString forPrint(QString html);
//! The pill for printing and PDFs: readable in black and white.
QString printPill();
//! The bar of an analysis row: a purple glyph put before the row's name (Qt draws no per-cell border).
QString barMark();
//! A table row heading the analysis rows: bold \a title and the pill, across \a colspan columns.
QString headingRow(const QString &title, int colspan);
//! "<base> · analysis", the name of a chart or channel of the analysis.
QString title(const QString &base);
//! A heading row of the events tree that cannot be chosen; \a analysis gives it the mark's look.
QTreeWidgetItem *treeHeading(const QString &text, bool analysis);

} // namespace AnalysisMark

//! The mark of manual scoring: like the analysis' (a bar and a pill), in green.
namespace ManualMark {
//! The small pill «ручн.» with its hover text.
QString pill();
//! A green bar before a row's name.
QString barMark();
//! A table heading row: the bar, the title in bold, the pill.
QString headingRow(const QString &title, int colspan);
} // namespace ManualMark

#endif // ANALYSISMARK_H
