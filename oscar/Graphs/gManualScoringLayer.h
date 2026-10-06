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
        enum Kind { Added, Removed, Retyped, Excluded };
        Kind kind;
        qint64 start, end;
        ChannelID channel;
        QString label;
    };

    //! \a result is shared by the layers of all graphs of the day.
    gManualScoringLayer(QSharedPointer<ManualScoring::Result> result, bool drawsMarkers);

    virtual void paint(QPainter &painter, gGraph &w, const QRegion &region) override;

    //! What is drawn for \a result between \a minX and \a maxX: the edited events when
    //! \a markers, and the excluded stretches always.
    static QList<Item> items(const ManualScoring::Result &result, qint64 minX, qint64 maxX, bool markers);

  private:
    QSharedPointer<ManualScoring::Result> m_result;
    bool m_markers;
};

#endif // GMANUALSCORINGLAYER_H
