/* Drawing the doctor's manual scoring on the Daily graphs
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "Graphs/gManualScoringLayer.h"

#include <QPainter>

#include "Graphs/gGraph.h"
#include "SleepLib/schema.h"

using ManualScoring::EffectiveEvent;
using ManualScoring::Origin;

gManualScoringLayer::gManualScoringLayer(QSharedPointer<ManualScoring::Result> result, bool drawsMarkers)
    : Layer(NoChannel), m_result(result), m_markers(drawsMarkers)
{
}

QList<gManualScoringLayer::Item> gManualScoringLayer::items(const ManualScoring::Result &result, qint64 minX, qint64 maxX,
                                                            bool markers)
{
    QList<Item> out;
    if (markers) {
        for (const EffectiveEvent &e : result.events) {
            const qint64 start = e.endMs - qint64(e.durationSec * 1000);
            if (e.origin == Origin::Device || e.endMs < minX || start > maxX) continue;
            switch (e.origin) {
            case Origin::Added: out.append({ Item::Added, start, e.endMs, e.channel, tr("manual") }); break;
            case Origin::Removed: out.append({ Item::Removed, start, e.endMs, e.channel, QString() }); break;
            case Origin::Retyped:
                out.append({ Item::Retyped, start, e.endMs, e.channel, tr("was %1").arg(schema::channel[e.originalChannel].label()) });
                break;
            case Origin::Device: break;
            }
        }
    }
    for (const auto &span : result.excludedSpans) {
        if (span.second >= minX && span.first <= maxX) out.append({ Item::Excluded, span.first, span.second, 0, QString() });
    }
    return out;
}

void gManualScoringLayer::paint(QPainter &painter, gGraph &w, const QRegion &region)
{
    if (!m_visible || !m_result) return;
    const QRect r = region.boundingRect();
    const double span = double(w.max_x - w.min_x);
    if (span <= 0 || r.width() <= 0) return;
    auto px = [&](qint64 t) { return r.left() + (t - w.min_x) / span * r.width(); };

    painter.save();
    painter.setClipRect(r);
    for (const Item &it : items(*m_result, w.min_x, w.max_x, m_markers)) {
        // at least 4 px, so a short stretch still shows on a whole night
        double x1 = px(it.start), x2 = px(it.end);
        if (x2 - x1 < 4) {
            const double mid = (x1 + x2) / 2;
            x1 = mid - 2;
            x2 = mid + 2;
        }
        const QRectF box(x1, r.top(), x2 - x1, r.height());
        const QColor color = it.channel ? schema::channel[it.channel].defaultColor() : QColor(Qt::gray);
        switch (it.kind) {
        case Item::Excluded:
            painter.fillRect(box, QColor(128, 128, 128, 45));
            painter.fillRect(box, QBrush(QColor(90, 90, 90, 140), Qt::BDiagPattern));
            painter.setPen(QPen(QColor(90, 90, 90, 180), 1));
            painter.drawLine(QPointF(x1, r.top()), QPointF(x1, r.bottom()));
            painter.drawLine(QPointF(x2, r.top()), QPointF(x2, r.bottom()));
            if (m_markers) painter.fillRect(QRectF(x1, r.top(), x2 - x1, 5), QColor(90, 90, 90));   // a tab on top
            break;
        case Item::Added:
            painter.setPen(QPen(color, 2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box.adjusted(1, 1, -1, -1));
            painter.setPen(color);
            painter.drawText(QPointF(x1 + 2, r.top() + painter.fontMetrics().ascent() + 1), it.label);
            break;
        case Item::Removed:
            painter.fillRect(box, QColor(255, 255, 255, 150));
            painter.setPen(QPen(QColor(192, 57, 43), 2));
            painter.drawLine(QPointF(x1, r.bottom()), QPointF(x2, r.top()));
            break;
        case Item::Retyped:
            painter.setPen(QPen(color, 2));
            painter.drawLine(QPointF(x2, r.top()), QPointF(x2, r.bottom()));
            painter.drawText(QPointF(x2 + 2, r.top() + painter.fontMetrics().ascent() + 1), it.label);
            break;
        }
    }
    painter.restore();
}
