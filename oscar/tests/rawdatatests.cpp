/* Raw Data Unit Tests
 *
 * Copyright (c) 2021-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "rawdatatests.h"
#include "rawdata.h"

#include <QBuffer>

// Check QIODevice interface for consistency.
void RawDataTests::testQIODeviceInterface()
{
    // Create sample data.
    static const int DATA_SIZE = 256;
    QByteArray data(DATA_SIZE, 0);
    for (int i = 0; i < data.size(); i++) {
        data[i] = (DATA_SIZE-1) - i;
    }
    QBuffer qio(&data);

    // Create raw data wrapper.
    RawDataDevice raw_instance(qio, "sample");
    QVERIFY(raw_instance.name() == "sample");
    QIODevice & raw(raw_instance);  // cast to its generic interface for accurate testing


    // Connect signals for testing.
    _RawDataTestSignalSink sink;
    connect(&raw, SIGNAL(channelReadyRead(int)), &sink, SLOT(onChannelReadyRead(int)));
    connect(&raw, SIGNAL(readyRead()), &sink, SLOT(onReadyRead()));
    connect(&raw, SIGNAL(readChannelFinished()), &sink, SLOT(onReadChannelFinished()));
    connect(&raw, SIGNAL(aboutToClose()), &sink, SLOT(onAboutToClose()));


    // Open
    QVERIFY(raw.isOpen() == qio.isOpen());
    QVERIFY(raw.isReadable() == qio.isReadable());
    QVERIFY(raw.isWritable() == qio.isWritable());
    QVERIFY(raw.isWritable() == false);
    QVERIFY(raw.isSequential() == qio.isSequential());
    QVERIFY(raw.openMode() == qio.openMode());

    QVERIFY(raw.open(QIODevice::ReadWrite) == false);
    QVERIFY(raw.open(QIODevice::ReadOnly) == true);
    QVERIFY(raw.isOpen() == qio.isOpen());
    QVERIFY(raw.isReadable() == qio.isReadable());
    QVERIFY(raw.isWritable() == qio.isWritable());
    QVERIFY(raw.isWritable() == false);
    QVERIFY(raw.isSequential() == qio.isSequential());
    QVERIFY(raw.openMode() == qio.openMode());


    // waitForReadyRead and ready signals
    QVERIFY(raw.waitForReadyRead(10000) == false);
    //QVERIFY(sink.m_channelReadyRead != -1);
    //QVERIFY(sink.m_readyRead == true);


    // Channels
    QVERIFY(raw.readChannelCount() == qio.readChannelCount());
    for (int i = 0; i < raw.readChannelCount(); i++) {
        raw.setCurrentReadChannel(i);
        QVERIFY(raw.currentReadChannel() == i);
        QVERIFY(raw.currentReadChannel() == qio.currentReadChannel());
    }


    // Text mode
    // Text mode is pretty awful, it just drops all \x0D, even without a trailing \x0A.
    QVERIFY(raw.isTextModeEnabled() == false);
    QVERIFY(raw.isTextModeEnabled() == qio.isTextModeEnabled());
    raw.setTextModeEnabled(true);
    QVERIFY(raw.isTextModeEnabled() == true);
    raw.peek(1);  // force a sync of text mode
    QVERIFY(raw.isTextModeEnabled() == qio.isTextModeEnabled());
    raw.setTextModeEnabled(false);
    raw.peek(1);  // force a sync of text mode
    QVERIFY(raw.isTextModeEnabled() == qio.isTextModeEnabled());


    // seek/pos/getChar/ungetChar/readAll/atEnd
    // skip() is 5.10 or later, so we don't use or test it
    char ch=-1;
    int pos = raw.pos();
    QVERIFY(raw.pos() == qio.pos() - 1);  // peek (above) only retracts raw's position after reading qio
    QVERIFY(raw.getChar(&ch) == true);
    QVERIFY(raw.pos() == qio.pos());
    raw.ungetChar(ch);
    QVERIFY(raw.pos() == pos);
    QVERIFY(raw.pos() == qio.pos() - 1);  // ungetChar only affects raw's buffer/position
    QVERIFY(ch == data[0]);

    QVERIFY(raw.size() == qio.size());

    QVERIFY(raw.seek(16) == true);
    QVERIFY(raw.pos() == 16);
    QVERIFY(raw.pos() == qio.pos());
    QVERIFY(raw.atEnd() == qio.atEnd());
    
    
    // Check boundary conditions at end of device.
    QVERIFY(raw.seek(255) == true);
    QVERIFY(raw.getChar(&ch) == true);
    QVERIFY(raw.pos() == qio.pos());
    QVERIFY(raw.atEnd() == true);
    QVERIFY(raw.atEnd() == qio.atEnd());
    QVERIFY(raw.bytesAvailable() == qio.bytesAvailable());
    raw.ungetChar(ch);
    QVERIFY(raw.atEnd() == false);
    QVERIFY(raw.atEnd() != qio.atEnd());
    QVERIFY(raw.bytesAvailable() == qio.bytesAvailable() + 1);
    
    QVERIFY(raw.reset() == true);
    QVERIFY(raw.pos() == 0);
    QVERIFY(raw.pos() == qio.pos());
    QByteArray all = raw.readAll();
    QVERIFY(all == data);
    QVERIFY(raw.atEnd() == qio.atEnd());
    QVERIFY(raw.bytesAvailable() == qio.bytesAvailable());


    // canReadLine
    QVERIFY(raw.canReadLine() == qio.canReadLine());
    raw.seek(255 - 0x0A);
    QVERIFY(raw.canReadLine() == true);
    QVERIFY(raw.canReadLine() == qio.canReadLine());
    QVERIFY(raw.getChar(&ch) == true);
    QVERIFY(ch == 0x0A);
    QVERIFY(raw.canReadLine() == false);
    QVERIFY(raw.canReadLine() == qio.canReadLine());
    raw.ungetChar(ch);
    QVERIFY(raw.canReadLine() == true);
    QVERIFY(raw.canReadLine() != qio.canReadLine());


    // readLine x2
    QVERIFY(raw.reset() == true);
    QVERIFY(raw.canReadLine() == qio.canReadLine());

    char line[DATA_SIZE+1];  // plus trailing null
    int length = raw.readLine(line, sizeof(line));
    pos = raw.pos();
    raw.reset();
    char line2[DATA_SIZE+1];  // plus trailing null
    int length2 = qio.readLine(line2, sizeof(line2));
    QVERIFY(length == length2);
    QVERIFY(strcmp(line, line2) == 0);
    
    raw.reset();
    
    QByteArray raw_readLine = raw.readLine();
    raw.reset();
    QVERIFY(raw_readLine == qio.readLine());


    // read & peek x2
    QVERIFY(raw.reset() == true);
    
    length = raw.read(line, 128);
    QVERIFY(length == 128);
    QVERIFY(raw.pos() == 128);
    QVERIFY(raw.pos() == qio.pos());
    QVERIFY(memcmp(data.constData(), line, 128) == 0);

    QVERIFY(raw.pos() == 128);
    length2 = raw.peek(line2, 128);
    QVERIFY(raw.pos() == 128);
    QVERIFY(length == 128);
    QVERIFY(raw.pos() == qio.pos() - length);  // peek only retracts raw's position after reading qio
    QVERIFY(memcmp(data.constData()+128, line2, 128) == 0);

    raw.reset();

    QByteArray raw_read = raw.read(128);
    QVERIFY(length == 128);
    QVERIFY(raw.pos() == 128);
    QVERIFY(raw.pos() == qio.pos());
    QVERIFY(raw_read == data.mid(0, 128));

    QVERIFY(raw.pos() == 128);
    QByteArray raw_peek = raw.peek(128);
    QVERIFY(raw.pos() == 128);
    QVERIFY(length == 128);
    QVERIFY(raw.pos() == qio.pos() - 128);  // peek only retracts raw's position after reading qio
    QVERIFY(raw_peek == data.mid(128, 128));

    raw.reset();


    // Transactions
    // These exist solely within raw and don't pass through to the underlying device.
    QVERIFY(raw.isTransactionStarted() == false);
    raw.startTransaction();
    QVERIFY(raw.isTransactionStarted() == true);
    raw_peek = raw.read(128);
    QVERIFY(raw.pos() == 128);
    raw.rollbackTransaction();
    QVERIFY(raw.isTransactionStarted() == false);
    QVERIFY(raw.pos() == 0);
    raw.startTransaction();
    QVERIFY(raw.isTransactionStarted() == true);
    raw_read = raw.read(128);
    QVERIFY(raw.pos() == 128);
    raw.commitTransaction();
    QVERIFY(raw.isTransactionStarted() == false);
    QVERIFY(raw.pos() == 128);


    // Close
    raw.close();
    QVERIFY(raw.isOpen() == qio.isOpen());
    QVERIFY(sink.m_aboutToClose);
    //QVERIFY(sink.m_readChannelFinished);


    // Unimplemented/untested:
    // bytesToWrite
    // currentWriteChannel
    // setCurentWriteChannel
    // putChar
    // waitForBytesWritten
    // write x3
    // writeChannelCount
    // bytesWritten signal
    // channelBytesWritten signal
}

_RawDataTestSignalSink::_RawDataTestSignalSink() : QObject() {
    m_channelReadyRead = -1;
    m_readyRead = false;
    m_readChannelFinished = false;
    m_aboutToClose = false;
}

void _RawDataTestSignalSink::onAboutToClose()
{
    m_aboutToClose = true;
}

void _RawDataTestSignalSink::onChannelReadyRead(int channel)
{
    m_channelReadyRead = channel;
}

void _RawDataTestSignalSink::onReadChannelFinished()
{
    m_readChannelFinished = true;
}

void _RawDataTestSignalSink::onReadyRead()
{
    m_readyRead = true;
}


// TODO: Test sequential devices when we have a test case.
// TODO: Test waitForReadySignal when we have a test case.
// TODO: Test readyRead/channelReadyRead/onReadChannelFinished signals when we have a test case.
