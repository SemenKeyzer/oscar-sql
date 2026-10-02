/* gDifferenceOverlay Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GDIFFERENCEOVERLAY_H
#define GDIFFERENCEOVERLAY_H

#include <QCoreApplication>
#include "Graphs/layer.h"
#include "analysispanel.h"

/*! \class gDifferenceOverlay
    \brief Marks on the flow graph where the device and OSCAR's analysis disagree
    (AnalysisPanel::differences()): a thin strip of marks along the top over a long range,
    translucent bands with a short label once zoomed in to a few minutes.
    */
class gDifferenceOverlay : public Layer
{
    Q_DECLARE_TR_FUNCTIONS(gDifferenceOverlay)
  public:
    gDifferenceOverlay();
    virtual ~gDifferenceOverlay() {}

    void setSpans(const QVector<DifferenceSpan> &spans);
    //! The difference the Analysis tab is showing, drawn stronger; an empty range for none.
    void setCurrent(qint64 start, qint64 end);

    virtual void paint(QPainter &painter, gGraph &w, const QRegion &region) override;
    //! Never makes the graph count as having data of its own.
    virtual bool isEmpty() override { return true; }
    virtual EventDataType Miny() override { return 0; }
    virtual EventDataType Maxy() override { return 0; }

    //! Ranges up to this long show bands; longer ones the strip of marks.
    static constexpr qint64 kBandRangeMs = 15 * 60 * 1000;
    static QColor color(DifferenceSpan::Kind kind);
    //! What the tooltip says about a difference.
    static QString describe(const DifferenceSpan &d);

  private:
    QVector<DifferenceSpan> m_spans;
    qint64 m_currentStart = 0, m_currentEnd = 0;
};

#endif // GDIFFERENCEOVERLAY_H
