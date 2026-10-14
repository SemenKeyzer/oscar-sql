/* O2Ring Protocol Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef O2RINGPROTOCOLTESTS_H
#define O2RINGPROTOCOLTESTS_H

#include "tests/AutoTest.h"

//! \brief The O2Ring Bluetooth protocol: frames, CRC-8, answers in pieces, ring info, file check
//!        (ported from the reverse-engineering session's tests).
class O2RingProtocolTests : public QObject
{
    Q_OBJECT
private slots:
    void testCrcVector();
    void testRequestWithPayloadAndSeq();
    void testFileNamePayload();
    void testTimePayload();
    void testReaderByteByByte();
    void testReaderTwoFramesInOneChunk();
    void testReaderSkipsJunk();
    void testReaderDropsBadCrc();
    void testReaderIgnoresRequests();
    void testReaderLenCap();
    void testReaderReset();
    void testParseInfo();
    void testValidateFile();
};
DECLARE_TEST(O2RingProtocolTests)

#endif // O2RINGPROTOCOLTESTS_H
