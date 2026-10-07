/* Drawing the doctor's manual scoring on the Daily graphs
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "Graphs/gManualScoringLayer.h"

#include <algorithm>

#include <QLocale>
#include <QPainter>

#include "Graphs/gGraph.h"
#include "Graphs/gGraphView.h"
#include "SleepLib/schema.h"

using ManualScoring::EffectiveEvent;
using ManualScoring::Origin;

gManualScoringLayer::gManualScoringLayer(QSharedPointer<ManualScoring::Result> result, bool drawsMarkers, bool drawsBoxes)
    : Layer(NoChannel), m_result(result), m_markers(drawsMarkers), m_boxes(drawsBoxes)
{
}

QList<gManualScoringLayer::Item> gManualScoringLayer::items(const ManualScoring::Result &result, qint64 minX, qint64 maxX,
                                                            bool markers, bool boxes)
{
    QList<Item> out;
    const bool zoomed = boxes && (maxX - minX) <= kBoxRangeMs;
    if (markers) {
        for (const EffectiveEvent &e : result.events) {
            const qint64 start = e.endMs - qint64(e.durationSec * 1000);
            if (e.endMs < minX || start > maxX) continue;
            // zoomed in: the type at the top left and the length at the top right, as a sleep lab marks them
            const QString type = zoomed ? schema::channel[e.channel].label() : QString();
            const QString length = zoomed ? QStringLiteral("%1 %2").arg(QLocale().toString(e.durationSec, 'f', 1), tr("s")) : QString();
            switch (e.origin) {
            case Origin::Device:
                // inside a stretch only when dragged there: the box stays, to drag it back out
                if (zoomed && (!e.excluded || e.resizeEditId > 0)) out.append({ Item::Event, start, e.endMs, e.channel, type, length });
                break;
            case Origin::Added:
                out.append({ Item::Added, start, e.endMs, e.channel, zoomed ? type + QStringLiteral(" · ") + tr("manual") : tr("manual"), length });
                break;
            case Origin::Removed: out.append({ Item::Removed, start, e.endMs, e.channel, QString(), QString() }); break;
            case Origin::Retyped: {
                const QString was = tr("was %1").arg(schema::channel[e.originalChannel].label());
                out.append({ Item::Retyped, start, e.endMs, e.channel, zoomed ? type + QStringLiteral(" · ") + was : was, length });
                break;
            }
            }
        }
    }
    for (const auto &span : result.excludedSpans) {
        if (span.second >= minX && span.first <= maxX) out.append({ Item::Excluded, span.first, span.second, 0, QString(), QString() });
    }
    return out;
}

QPair<qint64, qint64> gManualScoringLayer::drawnRange(bool blockZoom, qint64 minX, qint64 maxX, qint64 rMinX, qint64 rMaxX)
{
    return blockZoom ? qMakePair(rMinX, rMaxX) : qMakePair(minX, maxX);
}

void gManualScoringLayer::paint(QPainter &painter, gGraph &w, const QRegion &region)
{
    if (!m_visible || !m_result) return;
    const QRect r = region.boundingRect();
    const QPair<qint64, qint64> range = drawnRange(w.blockZoom(), w.min_x, w.max_x, w.rmin_x, w.rmax_x);
    const double span = double(range.second - range.first);
    if (span <= 0 || r.width() <= 0) return;
    auto px = [&](qint64 t) { return r.left() + (t - range.first) / span * r.width(); };

    // an edge being dragged: drawn where it is now
    const ManualScoring::Result shown = w.graphView() ? withDrag(*m_result, *w.graphView()) : *m_result;
    // the type at the top left, the length at the top right; a box too narrow for both has them in a row
    auto labels = [&painter, &r](const QRectF &box, const QString &label, const QString &duration, double inset, double down) {
        const QFontMetrics fm = painter.fontMetrics();
        const double y = r.top() + fm.ascent() + down;
        if (duration.isEmpty()) {
            painter.drawText(QPointF(box.left() + inset, y), label);
        } else if (fm.horizontalAdvance(label) + fm.horizontalAdvance(duration) + 3 * inset <= box.width()) {
            painter.drawText(QPointF(box.left() + inset, y), label);
            painter.drawText(QPointF(box.right() - inset - fm.horizontalAdvance(duration), y), duration);
        } else {
            painter.drawText(QPointF(box.left() + inset, y), label + QStringLiteral("  ") + duration);
        }
    };

    painter.save();
    painter.setClipRect(r);
    for (const Item &it : items(shown, range.first, range.second, m_markers, showsBoxes(m_boxes, w.graphView() && w.graphView()->scoringMode(), w.blockZoom()))) {
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
        case Item::Event:
            painter.fillRect(box, QColor(color.red(), color.green(), color.blue(), 55));
            painter.setPen(QPen(QColor(color.red(), color.green(), color.blue(), 170), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
            painter.setPen(QColor(40, 40, 40));
            labels(box, it.label, it.duration, 3, 2);
            break;
        case Item::Added:
            painter.setPen(QPen(color, 2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box.adjusted(1, 1, -1, -1));
            painter.setPen(color);
            labels(box, it.label, it.duration, 2, 1);
            break;
        case Item::Removed:
            painter.fillRect(box, QColor(255, 255, 255, 150));
            painter.setPen(QPen(QColor(192, 57, 43), 2));
            painter.drawLine(QPointF(x1, r.bottom()), QPointF(x2, r.top()));
            break;
        case Item::Retyped:
            painter.setPen(QPen(color, 2));
            painter.drawLine(QPointF(x2, r.top()), QPointF(x2, r.bottom()));
            painter.drawText(QPointF(x2 + 2, r.top() + painter.fontMetrics().ascent() + 1),
                             it.duration.isEmpty() ? it.label : it.label + QStringLiteral("  ") + it.duration);
            break;
        }
    }
    painter.restore();
}

bool gManualScoringLayer::showsBoxes(bool drawsBoxes, bool scoringMode, bool blockZoom)
{
    return drawsBoxes && scoringMode && !blockZoom;   // a scoring tool: the flow graph looks as before otherwise
}

ManualScoring::Result gManualScoringLayer::withDrag(const ManualScoring::Result &result, const gGraphView &view)
{
    ScoringResize::Target t;
    QPair<qint64, qint64> live;
    if (!view.scoringDragPreview(&t, &live)) return result;
    ManualScoring::Result out = result;
    if (t.kind == ScoringResize::Target::Event && t.eventIndex >= 0 && t.eventIndex < out.events.size()) {
        out.events[t.eventIndex].endMs = live.second;
        out.events[t.eventIndex].durationSec = (live.second - live.first) / 1000.0;
    } else if (t.kind == ScoringResize::Target::Excluded) {
        // the stretches again, with the dragged one where it is now
        QList<QPair<qint64, qint64>> spans;
        for (const ManualScoring::ExcludeEdit &x : out.excludeEdits) {
            const bool dragged = x.editId == t.editId || (x.startMs == t.startMs && x.endMs == t.endMs);
            spans.append(dragged ? live : qMakePair(x.startMs, x.endMs));
        }
        std::sort(spans.begin(), spans.end());
        out.excludedSpans.clear();
        for (const auto &s : spans) {
            if (!out.excludedSpans.isEmpty() && s.first <= out.excludedSpans.last().second) {
                out.excludedSpans.last().second = qMax(out.excludedSpans.last().second, s.second);
            } else {
                out.excludedSpans.append(s);
            }
        }
    }
    return out;
}
