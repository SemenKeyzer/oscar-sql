/* Sleep Analysis Overview Charts Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GANALYSISCHARTS_H
#define GANALYSISCHARTS_H

#include <QHash>

#include "Graphs/gSummaryChart.h"

/*! \class gAnalysisChart
    \brief Overview charts of OSCAR's own sleep analysis (spec §5.2), one bar per night
    from the analysis_daily rows (AnalysisService's cache): no events are loaded.
    */
class gAnalysisChart : public gSummaryChart
{
  public:
    enum Kind {
        Ahi,            //!< the analysis' AHI by event type (the device's in the tooltip)
        Odi,            //!< ODI 4 % with the rest of ODI 3 % on top
        Spo2Ranges,     //!< minutes in each SpO2 range of the configured thresholds
        ProblemZones,   //!< minutes in problem zones, marked ones apart
        HypoxicBurden,  //!< %·min/h of desaturations linked to breathing events
        FlowLimitation, //!< % of the analysed time with limited breaths
        PulseRises,     //!< pulse rises per hour
    };

    explicit gAnalysisChart(Kind kind);
    virtual ~gAnalysisChart() {}

    //! Graph code, title and y-axis units of a kind, for the Overview.
    static QString code(Kind kind);
    static QString title(Kind kind);
    static QString units(Kind kind);
    //! Every kind, in the Overview's order.
    static QList<Kind> kinds();

    virtual void preCalc();
    virtual void populate(Day *day, int idx);
    virtual void customCalc(Day *day, QVector<SummaryChartSlice> &slices);
    virtual QString tooltipData(Day *day, int idx);
    virtual void afterDraw(QPainter &painter, gGraph &graph, QRectF rect);

    virtual Layer *Clone() {
        gAnalysisChart *sc = new gAnalysisChart(m_kind);
        gSummaryChart::CloneInto(sc);
        return sc;
    }

  private:
    Kind m_kind;
    QHash<int, float> m_weight;     //!< hours behind each night's value
    QHash<int, float> m_device;     //!< the device's AHI (Ahi only)
    QHash<int, QString> m_tooltip;
    SummaryCalcItem m_deviceCalc;
};

#endif // GANALYSISCHARTS_H
