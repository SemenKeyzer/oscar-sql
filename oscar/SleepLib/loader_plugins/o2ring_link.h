/* O2Ring Bluetooth link
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef O2RING_LINK_H
#define O2RING_LINK_H

#include <QBluetoothDeviceInfo>
#include <QList>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyService>
#include <QTimer>

#include "SleepLib/loader_plugins/o2ring_downloader.h"

class QBluetoothDeviceDiscoveryAgent;
class QLowEnergyController;

struct O2RingFoundDevice {
    QBluetoothDeviceInfo info;
    QString name;
    int rssi = 0;
};

//! Finds O2Rings ("O2Ring <digits>") that advertise over Bluetooth LE.
class O2RingScanner : public QObject
{
    Q_OBJECT
public:
    explicit O2RingScanner(QObject *parent = nullptr);
    void start(int timeoutMs = 15000);
    void stop();
signals:
    void finished(const QList<O2RingFoundDevice> &devices);   //!< strongest signal first
    void failed(const QString &message);
private:
    void onDiscovered(const QBluetoothDeviceInfo &info);
    void onFinished();
    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    QList<O2RingFoundDevice> m_found;
};

//! O2RingLink over Qt Bluetooth LE: notifications from the ring, writes without response.
class QtO2RingLink : public O2RingLink
{
    Q_OBJECT
public:
    explicit QtO2RingLink(QObject *parent = nullptr);
    ~QtO2RingLink() override;
    void connectTo(const QBluetoothDeviceInfo &device, int timeoutMs = 15000);
    void disconnectFromDevice();
    void write(const QByteArray &data) override;
signals:
    void ready();
    void failed(const QString &message);
private:
    void setupService();
    void onServiceState(QLowEnergyService::ServiceState state);
    void failOnce(const QString &message);

    QLowEnergyController *m_controller = nullptr;
    QLowEnergyService *m_service = nullptr;
    QLowEnergyCharacteristic m_writeChar;
    QTimer m_connectTimer;
    bool m_ready = false;
    bool m_failed = false;
};

#endif // O2RING_LINK_H
