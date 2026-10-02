/* gDifferenceOverlay Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "Graphs/gDifferenceOverlay.h"
#include "Graphs/gGraph.h"
#include "Graphs/gGraphView.h"
#include "SleepLib/appsettings.h"

gDifferenceOverlay::gDifferenceOverlay()
    : Layer(NoChannel)
{
}

void gDifferenceOverlay::setSpans(const QVector<DifferenceSpan> &spans)
{
    m_spans = spans;
    m_currentStart = m_currentEnd = 0;
}

void gDifferenceOverlay::setCurrent(qint64 start, qint64 end)
{
    m_currentStart = start;
    m_currentEnd = end;
}

QColor gDifferenceOverlay::color(DifferenceSpan::Kind kind)
{
    switch (kind) {
    case DifferenceSpan::DeviceOnly: return QColor(0xba, 0x75, 0x17);     // amber: the device's
    case DifferenceSpan::AnalysisOnly: return QColor(0x53, 0x4a, 0xb7);   // purple: the analysis'
    case DifferenceSpan::DifferentType: break;
    }
    return QColor(0xef, 0x9f, 0x27);                                       // yellow: both, other type
}

QString gDifferenceOverlay::describe(const DifferenceSpan &d)
{
    const qint64 seconds = (d.end - d.start) / 1000;
    switch (d.kind) {
    case DifferenceSpan::DeviceOnly:
        return tr("Device only: %1, %2 s. The analysis found nothing here.").arg(d.label()).arg(seconds);
    case DifferenceSpan::AnalysisOnly:
        return tr("Analysis only: %1, %2 s. The device marked nothing here.").arg(d.label()).arg(seconds);
    case DifferenceSpan::DifferentType: break;
    }
    return tr("Different type: device %1, analysis a%2.").arg(d.device, d.analysis);
}

void gDifferenceOverlay::paint(QPainter &painter, gGraph &w, const QRegion &region)
{
    if (!m_visible || m_spans.isEmpty()) return;
    const QRect r = region.boundingRect();
    const qint64 range = w.max_x - w.min_x;
    if (range <= 0 || r.width() <= 0) return;
    const double px = double(r.width()) / double(range);
    const bool bands = range <= kBandRangeMs;
    const int strip = qMax(4, int(6 * w.printScaleY()));
    const QPoint mouse = w.graphView()->currentMousePos();

    QString tip;
    int tipX = 0;
    for (const DifferenceSpan &d : m_spans) {
        if (d.end < w.min_x || d.start > w.max_x) continue;
        const double x1 = qMax(double(r.left()), r.left() + (d.start - w.min_x) * px);
        const double x2 = qMin(double(r.right()), r.left() + (d.end - w.min_x) * px);
        const int width = qMax(2, int(x2 - x1));
        const bool current = m_currentEnd > m_currentStart && d.start == m_currentStart && d.end == m_currentEnd;
        QColor c = color(d.kind);
        QRect area;
        if (bands) {
            area = QRect(int(x1), r.top(), width, r.height());
            c.setAlpha(current ? 90 : 45);
            painter.fillRect(area, c);
            if (current) {
                QColor edge = color(d.kind);
                edge.setAlpha(220);
                painter.setPen(edge);
                painter.drawRect(area.adjusted(0, 0, -1, -1));
            }
            int tw = 0, th = 0;
            const QString text = d.label();
            GetTextExtent(text, tw, th);
            if (tw + 6 <= width) {
                w.renderText(text, x1 + 3, r.top() + th + 2, 0, color(d.kind).darker(160));
            }
        } else {
            area = QRect(int(x1), r.top(), width, strip);
            c.setAlpha(current ? 255 : 190);
            painter.fillRect(area, c);
            area.adjust(-2, 0, 2, 4);   // easier to point at
        }
        if (area.contains(mouse)) {
            tip = describe(d);
            tipX = int(x1);
        }
    }
    if (!tip.isEmpty() && !w.selectingArea()) {
        w.ToolTip(tip, tipX, r.top() + strip + 18, TT_AlignLeft, AppSetting->tooltipTimeout());
    }
}
