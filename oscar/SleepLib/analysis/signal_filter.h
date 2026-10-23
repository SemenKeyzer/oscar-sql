/* Signal filters for display
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SIGNAL_FILTER_H
#define SIGNAL_FILTER_H

#include <QVector>

namespace analysis {

//! A zero-phase first-order low-pass of \a x sampled at \a rateHz with the cut-off \a cutoffHz
//! (forward and back, so nothing shifts in time); \a cutoffHz of 0 or less returns \a x as it is.
QVector<float> lowPass(const QVector<float> &x, double rateHz, double cutoffHz);

} // namespace analysis

#endif // SIGNAL_FILTER_H
