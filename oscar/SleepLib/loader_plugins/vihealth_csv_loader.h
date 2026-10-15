/* ViHealth CSV Oximetry Loader
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIHEALTH_CSV_LOADER_H
#define VIHEALTH_CSV_LOADER_H

#include "SleepLib/serialoximeter.h"

const QString vihealth_csv_class_name = "ViHealthCSV";
const int vihealth_csv_data_version = 1;

/*! \class ViHealthCsvLoader
    \brief Reads the CSV the ViHealth app exports for Wellue / Viatom oximeters (O2Ring and others):
    a header line in the app's language, then "HH:mm:ss MMM d yyyy,SpO2,pulse,motion,..." every few seconds. */
class ViHealthCsvLoader : public SerialOximeter
{
    Q_OBJECT
  public:
    ViHealthCsvLoader();
    ~ViHealthCsvLoader() override {}

    bool Detect(const QString &path) override { Q_UNUSED(path); return false; }
    //! A .csv path: 1 when it is a ViHealth export (its records are then in oxisessions), else 0.
    int Open(const QString &path) override;

    static void Register();

    int Version() override { return vihealth_csv_data_version; }
    const QString &loaderName() override { return vihealth_csv_class_name; }

    MachineInfo newInfo() override {
        return MachineInfo(MT_OXIMETER, 0, vihealth_csv_class_name, QObject::tr("Viatom"), QObject::tr("ViHealth CSV export"),
                           QString(), QString(), QObject::tr("ViHealth"), QDateTime::currentDateTime(), vihealth_csv_data_version);
    }
    QString getVendor() override { return QObject::tr("Viatom"); }
    QString getModel() override { return QObject::tr("ViHealth CSV export"); }

    bool isStartTimeValid() override { return true; }
    qint64 importResolution() override { return m_stepMs; }

  private:
    qint64 m_stepMs = 4000;
};

#endif // VIHEALTH_CSV_LOADER_H
