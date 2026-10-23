/* gDesatFill Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GDESATFILL_H
#define GDESATFILL_H

#include <QPolygonF>
#include <functional>
#include "Graphs/layer.h"
#include "SleepLib/analysis/oxi_analyzer.h"

class gLineChart;

/*! \class gDesatFill
    \brief Shades, on the SpO2 graph, the area each desaturation adds to the hypoxic burden:
    between the SpO2 curve and the peak it fell from, within the burden window around its nadir
    (doctor 2026-10-23, as Neurosoft shows it). Switched by AppSetting::showDesatFill().
    */
class gDesatFill : public Layer
{
  public:
    //! Shades under \a spo2, whose y scale it shares.
    explicit gDesatFill(gLineChart *spo2);
    virtual ~gDesatFill() {}

    virtual void paint(QPainter &painter, gGraph &w, const QRegion &region) override;
    virtual bool isEmpty() override { return true; }
    virtual EventDataType Miny() override { return 0; }
    virtual EventDataType Maxy() override { return 0; }

    //! The shaded area in (ms, %): the peak along the top, \a spo2 (the value at a time) along the
    //! bottom every \a stepMs from \a start to \a end, clipped to the burden window around \a nadir.
    //! Empty when nothing is below the peak.
    static QPolygonF shape(const std::function<float(qint64)> &spo2, qint64 start, qint64 end, qint64 nadir, float peak, int stepMs);

    //! The day's desaturations as the analysis scored them with the doctor's own (\a removed: the ends
    //! of the analysis' ones taken out by hand, drawn struck out); without them the stored ones are shaded.
    void setDesaturations(const QVector<analysis::Desaturation> &desats, const QVector<qint64> &removed);
    void clearDesaturations();
    //! The shading of a desaturation: the analysis' colour, or the manual green for one scored by hand.
    static QColor fillColor(bool manual);

  private:
    gLineChart *m_spo2;
    bool m_supplied = false;
    QVector<analysis::Desaturation> m_desats;
    QVector<qint64> m_removed;
};

#endif // GDESATFILL_H
