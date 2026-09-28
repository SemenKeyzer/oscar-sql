/* Oximetry Session Builder Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OXIMETRY_SESSION_BUILDER_H
#define OXIMETRY_SESSION_BUILDER_H

#include <QVector>
#include "SleepLib/serialoximeter.h"

class Session;

//! Adds pulse, SpO2 and (optionally) perfusion event lists for records spaced stepMs apart,
//! starting at startMs. A zero value is a gap. Returns the time of the last record.
qint64 addOximetryEvents(Session *session, qint64 startMs, const QVector<OxiRecord> &records,
                         qint64 stepMs, bool havePerfIndex);

//! Flags drops and pulse changes, computes the summary values and marks the session changed.
//! Uses the current profile's oximetry settings.
void finishOximetrySession(Session *session, qint64 lastMs, bool havePerfIndex);

#endif // OXIMETRY_SESSION_BUILDER_H
