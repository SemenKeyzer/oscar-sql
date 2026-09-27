/* Contec BLE Oximeter Loader Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CONTEC_BLE_LOADER_H
#define CONTEC_BLE_LOADER_H

#include "SleepLib/serialoximeter.h"

const QString contecble_class_name = "ContecBLE";
const int contecble_data_version = 1;

/*! \class ContecBleLoader
    \brief Identity of devices imported over Bluetooth (the import itself runs from the wizard).
    A SerialOximeter so GetOxiLoaders() lists it like the other oximeter loaders; it claims no files. */
class ContecBleLoader : public SerialOximeter
{
    Q_OBJECT
public:
    ContecBleLoader();
    ~ContecBleLoader() override = default;

    bool Detect(const QString &) override { return false; }
    int Open(const QString &) override { return 0; }
    static void Register();

    int Version() override { return contecble_data_version; }
    const QString &loaderName() override { return contecble_class_name; }
    MachineInfo newInfo() override { return infoForModel(QString()); }
    static MachineInfo infoForModel(const QString &model);
};

#endif // CONTEC_BLE_LOADER_H
