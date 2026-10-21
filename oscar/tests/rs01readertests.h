/* Contec RS01 Reader Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RS01READERTESTS_H
#define RS01READERTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for reading a Contec RS01 recording (.HD header, .SEG segments) from bytes.
class RS01ReaderTests : public QObject
{
    Q_OBJECT

private slots:
    void testHeaderParsesExample();
    void testHeaderSixtyHzPleth();
    void testHeaderInvalid();
    void testSegmentRecords();
    void testMissingMarkerIsGap();
    void testIndexOutOfRangeIsGap();
    void testSegmentsContinueInTime();
    void testMissingSegmentStops();
    void testFlowZeroAndSign();
    void testNoSignalZeros();
    void testTruncatedSegment();
    void testFindRecordings();
    void testRealNightIfGiven();
};
DECLARE_TEST(RS01ReaderTests)

#endif // RS01READERTESTS_H
