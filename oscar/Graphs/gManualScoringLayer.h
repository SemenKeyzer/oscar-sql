/* Drawing the doctor's manual scoring on the Daily graphs
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GMANUALSCORINGLAYER_H
#define GMANUALSCORINGLAYER_H

#include <QCoreApplication>
#include <QSharedPointer>

#include "Graphs/layer.h"
#include "SleepLib/manual_scoring.h"

/*! \class gManualScoringLayer
    \brief Shows the manual scoring of the night: every graph hatches the excluded stretches;
           the flow and event flags graphs also mark the added (dashed), removed (pale, struck
           through) and retyped ("was CA") events.
    */
class gManualScoringLayer : public Layer
{
    Q_DECLARE_TR_FUNCTIONS(gManualScoringLayer)
  public:
    struct Item {
        enum Kind { Added, Removed, Retyped, Excluded, Event };   //!< Event: a device event shown as a box
        Kind kind;
        qint64 start, end;
        ChannelID channel;
        QString label;
    };

    //! \a result is shared by the layers of all graphs of the day.
    //! \a drawsBoxes: zoomed in to kBoxRangeMs or less, every counted event is a labelled box (flow graph).
    gManualScoringLayer(QSharedPointer<ManualScoring::Result> result, bool drawsMarkers, bool drawsBoxes = false);
    static constexpr qint64 kBoxRangeMs = 20 * 60000;
    //! Whether a layer that draws boxes shows them now.
    static bool showsBoxes(bool drawsBoxes, bool scoringMode, bool blockZoom);

    virtual void paint(QPainter &painter, gGraph &w, const QRegion &region) override;

    //! What is drawn for \a result between \a minX and \a maxX: the edited events when
    //! \a markers, and the excluded stretches always.
    static QList<Item> items(const ManualScoring::Result &result, qint64 minX, qint64 maxX, bool markers, bool boxes = false);
    //! The time range a graph draws: the whole night for a block-zoomed graph (event and analysis
    //! flags), else the zoomed range.
    static QPair<qint64, qint64> drawnRange(bool blockZoom, qint64 minX, qint64 maxX, qint64 rMinX, qint64 rMaxX);

  private:
    QSharedPointer<ManualScoring::Result> m_result;
    bool m_markers;
    bool m_boxes;
};

#endif // GMANUALSCORINGLAYER_H
