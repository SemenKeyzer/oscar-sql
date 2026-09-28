/* Loader Robustness Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

// Damaged files from a card must be rejected, never crash or overrun memory.
// The test build runs with AddressSanitizer, so an out-of-bounds read fails here.
class LoaderRobustnessTests : public QObject
{
    Q_OBJECT
private slots:
    void testEdfValidFileParses();
    void testEdfNegativeSampleCountRejected();
    void testEdfOverflowingSizesRejected();
    void testEdfTruncatedFileRejected();
    void testEdfAnnotationWithoutSeparator();
    void testWmedfWithoutStorageFlagsRejected();
    void testWmedfEightBitSignalParses();
};
DECLARE_TEST(LoaderRobustnessTests)
