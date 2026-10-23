/* gDesatFill
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "Graphs/gDesatFill.h"

#include <QPainter>
#include "Graphs/gGraph.h"
#include "Graphs/gLineChart.h"
#include "SleepLib/appsettings.h"
#include "SleepLib/day.h"
#include "SleepLib/session.h"
#include "SleepLib/analysis/analysis_channels.h"
#include "SleepLib/analysis/oxi_analyzer.h"

gDesatFill::gDesatFill(gLineChart *spo2)
    : Layer(NoChannel), m_spo2(spo2)
{
}

void gDesatFill::setDesaturations(const QVector<analysis::Desaturation> &desats, const QVector<qint64> &removed)
{
    m_desats = desats;
    m_removed = removed;
    m_supplied = true;
}

void gDesatFill::clearDesaturations()
{
    m_desats.clear();
    m_removed.clear();
    m_supplied = false;
}

QColor gDesatFill::fillColor(bool manual)
{
    QColor c = manual ? QColor(QStringLiteral("#2E8B57")) : schema::channel[AN_Desaturation].defaultColor();
    c.setAlpha(70);
    return c;
}

QPolygonF gDesatFill::shape(const std::function<float(qint64)> &spo2, qint64 start, qint64 end, qint64 nadir, float peak, int stepMs)
{
    // the burden counts the area within the window around the nadir only
    const qint64 half = qint64(analysis::kBurdenWindowSec) * 500;
    const qint64 from = qMax(start, nadir - half), to = qMin(end, nadir + half);
    if (to <= from || stepMs <= 0) return QPolygonF();
    QPolygonF poly;
    bool below = false;
    poly << QPointF(double(from), peak);
    for (qint64 t = from; t <= to; t += stepMs) {
        const float v = qMin(spo2(t), peak);
        below = below || v < peak;
        poly << QPointF(double(t), v);
    }
    if (poly.last().x() < double(to)) poly << QPointF(double(to), qMin(spo2(to), peak));
    poly << QPointF(double(to), peak);
    return below ? poly : QPolygonF();
}

void gDesatFill::paint(QPainter &painter, gGraph &w, const QRegion &region)
{
    if (!m_visible || !m_day || !m_spo2 || !AppSetting || !AppSetting->showDesatFill()) return;
    const QRect r = region.boundingRect();
    const qint64 range = w.max_x - w.min_x;
    if (range <= 0 || r.width() <= 0 || r.height() <= 4) return;
    // the SpO2 line's own scale (gLineChart::paint), so the shading sits under the curve
    EventDataType miny = m_spo2->physMiny(), maxy = m_spo2->physMaxy();
    w.roundY(miny, maxy);
    if (maxy <= miny) return;
    const double xmult = double(r.width()) / double(range);
    const double ymult = double(r.height() - 3) / double(maxy - miny);
    const double xst = r.left() + 1, yst = r.top() + r.height() + 1;
    auto spo2At = [this](qint64 t) { return float(m_day->lookupValue(OXI_SPO2, t, false)); };

    auto toScreen = [&](QPolygonF poly) {
        for (QPointF &p : poly) p = QPointF(xst + (p.x() - w.min_x) * xmult, yst - (p.y() - miny) * ymult);
        return poly;
    };
    painter.save();
    painter.setPen(Qt::NoPen);
    if (m_supplied) {   // the analysis' set with the doctor's own: manual ones green, removed ones struck out
        for (const analysis::Desaturation &d : m_desats) {
            if (d.end < w.min_x || d.start > w.max_x) continue;
            const QPolygonF poly = shape(spo2At, d.start, d.end, d.nadirTime, d.peak, 1000);
            if (poly.isEmpty()) continue;
            painter.setBrush(fillColor(d.manual));
            painter.drawPolygon(toScreen(poly));
        }
    }
    for (Session *sess : m_day->sessions) {
        if (!sess->enabled()) continue;
        const qint64 drift = sess->correctionMs();
        for (EventList *el : sess->eventlist.value(AN_Desaturation)) {
            for (quint32 i = 0; i < el->count(); ++i) {
                const qint64 end = el->time(i) + drift;
                const qint64 start = end - qint64(el->data(i) * 1000);
                if (end < w.min_x || start > w.max_x || end <= start) continue;
                bool removed = false;
                for (qint64 t : m_removed) removed = removed || qAbs(t - end) <= 2000;
                if (removed) {   // taken out by hand: hatched over its span
                    painter.setBrush(QBrush(QColor(0x80, 0x80, 0x80, 150), Qt::BDiagPattern));
                    const double x1 = xst + (start - w.min_x) * xmult, x2 = xst + (end - w.min_x) * xmult;
                    painter.drawRect(QRectF(x1, r.top(), qMax(2.0, x2 - x1), r.height()));
                    continue;
                }
                if (m_supplied) continue;   // drawn from the set above
                // the nadir: the lowest second; the peak it fell from is that plus the depth
                qint64 nadir = start;
                float low = 1000;
                for (qint64 t = start; t <= end; t += 1000) {
                    const float v = spo2At(t);
                    if (v > 0 && v < low) { low = v; nadir = t; }
                }
                if (low >= 1000) continue;
                const float peak = low + float(el->data2(i));
                const QPolygonF poly = shape(spo2At, start, end, nadir, peak, 1000);
                if (poly.isEmpty()) continue;
                painter.setBrush(fillColor(false));
                painter.drawPolygon(toScreen(poly));
            }
        }
    }
    painter.restore();
}
