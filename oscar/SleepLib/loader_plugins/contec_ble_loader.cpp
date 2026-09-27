/* Contec BLE Oximeter Loader
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contec_ble_loader.h"

#include <QDebug>

static bool contecble_initialized = false;

ContecBleLoader::ContecBleLoader()
{
    m_type = MT_OXIMETER;
}

MachineInfo ContecBleLoader::infoForModel(const QString &model)
{
    return MachineInfo(MT_OXIMETER, 0, contecble_class_name, QObject::tr("Contec"),
                       model.isEmpty() ? QObject::tr("Bluetooth oximeter") : model,
                       QString(), QString(), model, QDateTime::currentDateTime(), contecble_data_version);
}

void ContecBleLoader::Register()
{
    if (contecble_initialized) return;
    qDebug() << "Registering ContecBleLoader";
    RegisterLoader(new ContecBleLoader());
    contecble_initialized = true;
}
