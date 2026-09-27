/* Contec BLE Oximeter Bluetooth Link
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contec_ble_link.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothUuid>
#include <QDebug>
#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <algorithm>

using namespace ContecBle;

ContecBleScanner::ContecBleScanner(QObject *parent)
    : QObject(parent)
{
}

void ContecBleScanner::start(int timeoutMs)
{
    m_found.clear();
    if (!m_agent) {
        m_agent = new QBluetoothDeviceDiscoveryAgent(this);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &ContecBleScanner::onDiscovered);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this, &ContecBleScanner::onFinished);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
                [this](QBluetoothDeviceDiscoveryAgent::Error error) {
            switch (error) {
            case QBluetoothDeviceDiscoveryAgent::PoweredOffError:
                emit failed(tr("Bluetooth is turned off on this computer."));
                break;
            case QBluetoothDeviceDiscoveryAgent::MissingPermissionsError:
                emit failed(tr("OSCAR isn't allowed to use Bluetooth. Allow it in System Settings > Privacy & Security > Bluetooth."));
                break;
            default:
                emit failed(m_agent->errorString());
                break;
            }
        });
    }
    m_agent->setLowEnergyDiscoveryTimeout(timeoutMs);
    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void ContecBleScanner::stop()
{
    if (m_agent && m_agent->isActive()) m_agent->stop();
}

void ContecBleScanner::onDiscovered(const QBluetoothDeviceInfo &info)
{
    if (!(info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)) return;
    const ModelInfo model = modelForName(info.name());
    if (!model.isValid()) return;
    for (ContecBleFoundDevice &f : m_found) {
        if (f.info.address() == info.address() && f.info.deviceUuid() == info.deviceUuid()) {
            f.info = info;
            f.rssi = info.rssi();
            return;
        }
    }
    ContecBleFoundDevice f;
    f.info = info;
    f.name = info.name();
    f.model = model.model;
    f.rssi = info.rssi();
    m_found.append(f);
}

void ContecBleScanner::onFinished()
{
    std::sort(m_found.begin(), m_found.end(),
              [](const ContecBleFoundDevice &a, const ContecBleFoundDevice &b) { return a.rssi > b.rssi; });
    emit finished(m_found);
}

QtContecBleLink::QtContecBleLink(QObject *parent)
    : ContecBleLink(parent)
{
    m_connectTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, [this]() {
        if (!m_ready) failOnce(tr("Couldn't connect to the oximeter."));
    });
}

QtContecBleLink::~QtContecBleLink()
{
    disconnectFromDevice();
}

void QtContecBleLink::failOnce(const QString &message)
{
    if (m_failed) return;
    m_failed = true;
    m_connectTimer.stop();
    qWarning() << "ContecBLE link:" << message;
    emit failed(message);
}

void QtContecBleLink::connectTo(const QBluetoothDeviceInfo &device)
{
    m_controller = QLowEnergyController::createCentral(device, this);
    connect(m_controller, &QLowEnergyController::connected, this, [this]() {
        QTimer::singleShot(800, this, [this]() {       // the vendor SDK waits before discovery
            if (m_controller) m_controller->discoverServices();
        });
    });
    connect(m_controller, &QLowEnergyController::discoveryFinished, this, &QtContecBleLink::setupService);
    connect(m_controller, &QLowEnergyController::errorOccurred, this, [this](QLowEnergyController::Error) {
        failOnce(m_controller->errorString());
    });
    connect(m_controller, &QLowEnergyController::disconnected, this, [this]() {
        if (m_ready) emit linkLost();
        else failOnce(tr("The oximeter disconnected."));
    });
    m_connectTimer.start(20000);
    m_controller->connectToDevice();
}

void QtContecBleLink::setupService()
{
    m_service = m_controller->createServiceObject(QBluetoothUuid(kServiceShortUuid), this);
    if (!m_service) {
        failOnce(tr("This device doesn't offer the Contec oximeter service."));
        return;
    }
    connect(m_service, &QLowEnergyService::stateChanged, this, &QtContecBleLink::onServiceState);
    connect(m_service, &QLowEnergyService::characteristicChanged, this,
            [this](const QLowEnergyCharacteristic &c, const QByteArray &value) {
        if (c.uuid() == QBluetoothUuid(kNotifyShortUuid)) emit received(value);
    });
    connect(m_service, &QLowEnergyService::descriptorWritten, this,
            [this](const QLowEnergyDescriptor &, const QByteArray &value) {
        if (value == QLowEnergyCharacteristic::CCCDEnableNotification && !m_ready) {
            m_ready = true;
            m_connectTimer.stop();
            emit ready();
        }
    });
    connect(m_service, &QLowEnergyService::errorOccurred, this, [this](QLowEnergyService::ServiceError) {
        failOnce(tr("Bluetooth communication with the oximeter failed."));
    });
    m_service->discoverDetails();
}

void QtContecBleLink::onServiceState(QLowEnergyService::ServiceState state)
{
    if (state != QLowEnergyService::RemoteServiceDiscovered) return;
    m_writeChar = m_service->characteristic(QBluetoothUuid(kWriteShortUuid));
    const QLowEnergyCharacteristic notify = m_service->characteristic(QBluetoothUuid(kNotifyShortUuid));
    if (!m_writeChar.isValid() || !notify.isValid()) {
        failOnce(tr("This device doesn't offer the Contec oximeter service."));
        return;
    }
    m_writeMode = (m_writeChar.properties() & QLowEnergyCharacteristic::WriteNoResponse)
                      ? QLowEnergyService::WriteWithoutResponse : QLowEnergyService::WriteWithResponse;
    const QLowEnergyDescriptor cccd = notify.clientCharacteristicConfiguration();
    if (!cccd.isValid()) {
        failOnce(tr("Couldn't turn on notifications from the oximeter."));
        return;
    }
    m_service->writeDescriptor(cccd, QLowEnergyCharacteristic::CCCDEnableNotification);
}

void QtContecBleLink::write(const QByteArray &data)
{
    if (!m_service || !m_writeChar.isValid()) return;
    for (int i = 0; i < data.size(); i += 20) {      // default ATT MTU: 20 bytes per write
        m_service->writeCharacteristic(m_writeChar, data.mid(i, 20), m_writeMode);
    }
}

void QtContecBleLink::disconnectFromDevice()
{
    m_connectTimer.stop();
    if (m_controller && m_controller->state() != QLowEnergyController::UnconnectedState) {
        m_ready = false;                 // a deliberate disconnect is not a lost link
        m_controller->disconnectFromDevice();
    }
}
