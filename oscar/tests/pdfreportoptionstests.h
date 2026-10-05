/* PDF Report Options Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PDFREPORTOPTIONSTESTS_H
#define PDFREPORTOPTIONSTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the choices of the combined PDF report.
class PdfReportOptionsTests : public QObject
{
    Q_OBJECT
private slots:
    void testPresets();
    void testRange();
    void testNightsToPrint();
    void testEstimatePages();
    void testMapRoundTrip();
};
DECLARE_TEST(PdfReportOptionsTests)

#endif // PDFREPORTOPTIONSTESTS_H
