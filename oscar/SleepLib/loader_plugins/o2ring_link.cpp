/* O2Ring Bluetooth link
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "o2ring_link.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothUuid>
#include <QDebug>
#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <algorithm>

namespace {
QBluetoothUuid uuid(const char *text) { return QBluetoothUuid(QString::fromLatin1(text)); }
}

O2RingScanner::O2RingScanner(QObject *parent)
    : QObject(parent)
{
}

void O2RingScanner::start(int timeoutMs)
{
    m_found.clear();
    if (!m_agent) {
        m_agent = new QBluetoothDeviceDiscoveryAgent(this);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &O2RingScanner::onDiscovered);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this, &O2RingScanner::onFinished);
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

void O2RingScanner::stop()
{
    if (m_agent && m_agent->isActive()) m_agent->stop();
}

void O2RingScanner::onDiscovered(const QBluetoothDeviceInfo &info)
{
    if (!(info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)) return;
    if (!info.name().startsWith(QString::fromLatin1(O2Ring::NamePrefix))) return;
    for (O2RingFoundDevice &f : m_found) {
        if (f.info.address() == info.address() && f.info.deviceUuid() == info.deviceUuid()) {
            f.info = info;
            f.rssi = info.rssi();
            return;
        }
    }
    O2RingFoundDevice f;
    f.info = info;
    f.name = info.name();
    f.rssi = info.rssi();
    m_found.append(f);
}

void O2RingScanner::onFinished()
{
    std::sort(m_found.begin(), m_found.end(),
              [](const O2RingFoundDevice &a, const O2RingFoundDevice &b) { return a.rssi > b.rssi; });
    emit finished(m_found);
}

QtO2RingLink::QtO2RingLink(QObject *parent)
    : O2RingLink(parent)
{
    m_connectTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, [this]() {
        if (!m_ready) failOnce(tr("Couldn't connect to the ring. Close the ViHealth app on the phone and try again."));
    });
}

QtO2RingLink::~QtO2RingLink()
{
    disconnectFromDevice();
}

void QtO2RingLink::failOnce(const QString &message)
{
    if (m_failed) return;
    m_failed = true;
    m_connectTimer.stop();
    qWarning() << "O2Ring link:" << message;
    emit failed(message);
}

void QtO2RingLink::connectTo(const QBluetoothDeviceInfo &device, int timeoutMs)
{
    m_controller = QLowEnergyController::createCentral(device, this);
    connect(m_controller, &QLowEnergyController::connected, this, [this]() {
        if (m_controller) m_controller->discoverServices();
    });
    connect(m_controller, &QLowEnergyController::discoveryFinished, this, &QtO2RingLink::setupService);
    connect(m_controller, &QLowEnergyController::errorOccurred, this, [this](QLowEnergyController::Error) {
        failOnce(m_controller->errorString());
    });
    connect(m_controller, &QLowEnergyController::disconnected, this, [this]() {
        if (m_ready) emit linkLost();
        else failOnce(tr("The ring disconnected."));
    });
    m_connectTimer.start(timeoutMs);
    m_controller->connectToDevice();
}

void QtO2RingLink::setupService()
{
    m_service = m_controller->createServiceObject(uuid(O2Ring::ServiceUuid), this);
    if (!m_service) {
        failOnce(tr("This ring is not supported yet."));
        return;
    }
    connect(m_service, &QLowEnergyService::stateChanged, this, &QtO2RingLink::onServiceState);
    connect(m_service, &QLowEnergyService::characteristicChanged, this,
            [this](const QLowEnergyCharacteristic &c, const QByteArray &value) {
        if (c.uuid() == uuid(O2Ring::NotifyUuid)) emit received(value);
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
        failOnce(tr("Bluetooth communication with the ring failed."));
    });
    m_service->discoverDetails();
}

void QtO2RingLink::onServiceState(QLowEnergyService::ServiceState state)
{
    if (state != QLowEnergyService::RemoteServiceDiscovered) return;
    m_writeChar = m_service->characteristic(uuid(O2Ring::WriteUuid));
    const QLowEnergyCharacteristic notify = m_service->characteristic(uuid(O2Ring::NotifyUuid));
    if (!m_writeChar.isValid() || !notify.isValid()) {
        failOnce(tr("This ring is not supported yet."));
        return;
    }
    const QLowEnergyDescriptor cccd = notify.clientCharacteristicConfiguration();
    if (!cccd.isValid()) {
        failOnce(tr("Couldn't turn on notifications from the ring."));
        return;
    }
    m_service->writeDescriptor(cccd, QLowEnergyCharacteristic::CCCDEnableNotification);
}

void QtO2RingLink::write(const QByteArray &data)
{
    if (!m_service || !m_writeChar.isValid()) return;
    const int piece = qMax(20, (m_controller ? m_controller->mtu() : 23) - 3);
    for (int i = 0; i < data.size(); i += piece) {
        m_service->writeCharacteristic(m_writeChar, data.mid(i, piece), QLowEnergyService::WriteWithoutResponse);
    }
}

void QtO2RingLink::disconnectFromDevice()
{
    m_connectTimer.stop();
    if (m_controller && m_controller->state() != QLowEnergyController::UnconnectedState) {
        m_ready = false;                 // a deliberate disconnect is not a lost link
        m_controller->disconnectFromDevice();
    }
}
