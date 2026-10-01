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
        Spo2Ranges,     //!< share of the night below the highest SpO2 threshold, by range
        ProblemZones,   //!< share of the night in problem zones, marked ones apart
        HypoxicBurden,  //!< %·min/h of desaturations linked to breathing events
        FlowLimitation, //!< % of the analysed time with limited breaths
        PulseRises,     //!< pulse rises per hour
    };

    explicit gAnalysisChart(Kind kind);

    //! One SpO2 range of a night: its label, seconds and share of the recorded time.
    struct RangeShare {
        QString name;
        int seconds = 0;
        double percent = 0;
        int colorIndex = 0;      //!< 0 for the highest range
    };
    //! The ranges of the profile's \a thresholds (highest first), lowest range first,
    //! from a night's SpO2 histogram (seconds per whole %).
    static QVector<RangeShare> spo2RangeShares(const QVector<int> &hist, int oxiSeconds, const QList<double> &thresholds);
    //! The label of the SpO2 range from \a lower (inclusive) to \a upper (exclusive) on
    //! whole-% readings, as the Overview and the Daily view show it: "90–93 %". A
    //! negative \a lower is the lowest range ("< 85 %"), an \a upper above 100 the
    //! highest ("≥ 94 %").
    static QString spo2RangeLabel(double lower, double upper);
    //! Colour of range \a index (0 the highest) of \a count: a colour-blind-safe sequence
    //! from yellow for the mildest range below the highest to dark red for the lowest;
    //! the problem zones use the same colours.
    static QColor rangeColor(int index, int count);
    //! Shares (%) of the recorded time in problem zones: all of them, and the marked ones.
    static QPair<double, double> problemZoneShares(int zoneSeconds, int markedSeconds, int oxiSeconds);
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
    virtual void drawBarOverlay(QPainter &painter, int idx, const QRectF &column, float miny, float ymult);
    virtual float overlayPeak(int idx);
    //! An AHI of 5 on the analysis' AHI, as a line.
    virtual float targetValue() { return m_kind == Ahi ? 5 : 0; }

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
    QHash<int, QVector<RangeShare>> m_ranges;   //!< each night's SpO2 ranges (Spo2Ranges only)
    QMap<int, RangeShare> m_rangeTotals;       //!< seconds per range over the shown nights
    qint64 m_rangeSeconds = 0;                  //!< recorded seconds over the shown nights
};

#endif // GANALYSISCHARTS_H
