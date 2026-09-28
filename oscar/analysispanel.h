/* Sleep Analysis Panel Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSISPANEL_H
#define ANALYSISPANEL_H

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QWidget>

#include "SleepLib/analysis/day_scorer.h"

class Day;
class QTreeWidget;
class QTreeWidgetItem;

/*! \class AnalysisPanel
    \brief The Daily view's text parts for OSCAR's own sleep analysis (spec §5.1).
    */
class AnalysisPanel
{
    Q_DECLARE_TR_FUNCTIONS(AnalysisPanel)
  public:
    //! The "Analysis (second opinion)" sidebar section's body: the analysis next to the
    //! device, oximetry and pulse, the hypopnea index under each rule, agreement with
    //! the device and notes. Links: "analysis=differences" (the Analysis tab) and
    //! "align=oximeter" (align the oximeter by the suggested offset).
    static QString sidebarHtml(Day *day, const analysis::DayResult &result, const QString &oxiSource,
                               const QList<double> &spo2Thresholds);

    //! The disclaimer shown wherever the analysis is (spec §1).
    static QString disclaimer();

    //! "12m 05s", or "1h 02m" from an hour.
    static QString duration(qint64 ms);
    //! An offset as "+0:02:00".
    static QString offset(qint64 ms);
};

/*! \class AnalysisTab
    \brief Daily view tab listing where the analysis and the device differ, the problem
    zones and the unexplained desaturations; a click shows that stretch in the graphs.
    */
class AnalysisTab : public QWidget
{
    Q_OBJECT
  public:
    explicit AnalysisTab(QWidget *parent = nullptr);

    void setResult(const analysis::DayResult &result);
    void clear();

  signals:
    //! Show this span (device-corrected times) in the graphs.
    void showRange(qint64 from, qint64 to);

  private:
    void onItemClicked(QTreeWidgetItem *item);
    QTreeWidget *m_tree;
};

#endif // ANALYSISPANEL_H
