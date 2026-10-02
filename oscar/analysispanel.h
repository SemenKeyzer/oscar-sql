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

/*! \struct DifferenceSpan
    \brief One place where the device and the analysis disagree, as the flow graph marks it.
    */
struct DifferenceSpan {
    enum Kind { DeviceOnly, AnalysisOnly, DifferentType };
    Kind kind = DeviceOnly;
    qint64 start = 0, end = 0;   //!< device-corrected, as the graphs show them
    QString device;              //!< the device's event, e.g. "OA" (none for AnalysisOnly)
    QString analysis;            //!< the analysis' event, e.g. "H" (none for DeviceOnly)
    //! "OA", "aH" or "OA \u2194 aOH".
    QString label() const;
};

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

    //! The day's differences from the device, in time order (none without a comparison).
    static QVector<DifferenceSpan> differences(const analysis::DayResult &result);
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
    //! Shows the next (\a step 1) or previous (-1) difference from the device, in time
    //! order across the device-only, analysis-only and different-type groups.
    void stepDifference(int step);

  signals:
    //! Show this span (device-corrected times) in the graphs.
    void showRange(qint64 from, qint64 to);
    //! The difference now shown (its own span), or an empty one when something else is.
    void differenceShown(qint64 start, qint64 end);
    //! The "show on the flow graph" check box changed.
    void showOnFlowChanged(bool show);

  private:
    void onItemClicked(QTreeWidgetItem *item);
    void updateStepper();
    QTreeWidget *m_tree;
    class QPushButton *m_prev;
    class QPushButton *m_next;
    class QLabel *m_position;
    QList<QTreeWidgetItem *> m_differences;   //!< in time order
    int m_current = -1;
};

#endif // ANALYSISPANEL_H
