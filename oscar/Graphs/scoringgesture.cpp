/* What a mouse gesture means in manual scoring mode
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "Graphs/scoringgesture.h"

#include "common_gui.h"

namespace ScoringGesture {

Action actionFor(bool scoringMode, const QString &graphName, Qt::MouseButton button, bool dragged)
{
    if (!scoringMode) return Action::None;
    // the CPAP's flow, a sleep screener's nasal flow, and SpO2 for desaturations (doctor 2026-10-23)
    const bool scored = graphName == STR_GRAPH_FlowRate || graphName == STR_GRAPH_Oxi_NasalFlow || graphName == STR_GRAPH_Oxi_SPO2;
    if (button == Qt::LeftButton && dragged && scored) return Action::SelectRange;
    if (button == Qt::RightButton && !dragged && (scored || graphName == STR_GRAPH_SleepFlags)) {
        return Action::ContextMenu;
    }
    return Action::None;
}

QPair<qint64, qint64> rangeFor(qint64 minX, qint64 maxX, int plotWidth, int x1, int x2)
{
    const int w = qMax(1, plotWidth);
    const double msPerPx = double(maxX - minX) / w;
    auto at = [&](int x) { return minX + qint64(qBound(0, x, w) * msPerPx); };
    const qint64 a = at(x1), b = at(x2);
    return { qMin(a, b), qMax(a, b) };
}

} // namespace ScoringGesture
