/* Contec BLE Protocol Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class ContecBleProtocolTests : public QObject
{
    Q_OBJECT
private slots:
    void testCommandBytes();
    void testChannelCommands();
    void testSetTimeCommand();
    void testPickFormat();
    void testDestructiveGuard();
    void testModelLookup();
    void testFrameLengths();
    void testSplitterHandlesSplitAndMergedNotifications();
    void testParseDeviceAnswers();
    void testRecordHeaderAndMidnight();
    void testEdVectorsMatchVendorCode();
    void testCodeDecoderAcrossPackets();
};
DECLARE_TEST(ContecBleProtocolTests)
