/* Signal filters for display
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "signal_filter.h"

#include <cmath>

namespace analysis {

QVector<float> lowPass(const QVector<float> &x, double rateHz, double cutoffHz)
{
    if (x.size() < 2 || rateHz <= 0 || cutoffHz <= 0) return x;
    // first order, run forward then back: the phase shifts cancel, the response squares
    const double rc = 1.0 / (2 * M_PI * cutoffHz);
    const double dt = 1.0 / rateHz;
    const double alpha = dt / (rc + dt);
    QVector<float> y(x.size());
    double v = x.first();
    for (int i = 0; i < x.size(); ++i) {
        v += alpha * (x[i] - v);
        y[i] = float(v);
    }
    v = y.last();
    for (int i = x.size() - 1; i >= 0; --i) {
        v += alpha * (y[i] - v);
        y[i] = float(v);
    }
    return y;
}

} // namespace analysis
