/* Contec BLE Oximeter Bluetooth Link Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CONTEC_BLE_LINK_H
#define CONTEC_BLE_LINK_H

#include <QBluetoothDeviceInfo>
#include <QList>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyService>
#include <QTimer>

#include "SleepLib/loader_plugins/contec_ble_downloader.h"

class QBluetoothDeviceDiscoveryAgent;
class QLowEnergyController;

struct ContecBleFoundDevice {
    QBluetoothDeviceInfo info;
    QString name;
    QString model;
    int rssi = 0;
};

//! Finds Contec oximeters that advertise over Bluetooth LE.
class ContecBleScanner : public QObject
{
    Q_OBJECT
public:
    explicit ContecBleScanner(QObject *parent = nullptr);
    void start(int timeoutMs = 15000);
    void stop();
signals:
    void finished(const QList<ContecBleFoundDevice> &devices);   //!< strongest signal first
    void failed(const QString &message);
private:
    void onDiscovered(const QBluetoothDeviceInfo &info);
    void onFinished();
    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    QList<ContecBleFoundDevice> m_found;
};

//! ContecBleLink over Qt Bluetooth LE: service ff12, notifications from ff02, writes to ff01.
class QtContecBleLink : public ContecBleLink
{
    Q_OBJECT
public:
    explicit QtContecBleLink(QObject *parent = nullptr);
    ~QtContecBleLink() override;
    void connectTo(const QBluetoothDeviceInfo &device);
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
    QLowEnergyService::WriteMode m_writeMode = QLowEnergyService::WriteWithResponse;
    QTimer m_connectTimer;
    bool m_ready = false;
    bool m_failed = false;
};

#endif // CONTEC_BLE_LINK_H
