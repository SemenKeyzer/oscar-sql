/* Bluetooth Oximeter Import Page Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BLUETOOTHOXIMETERPAGE_H
#define BLUETOOTHOXIMETERPAGE_H

#include <QDateTime>
#include <QList>
#include <QPointer>
#include <QWidget>
#include <memory>

#include "SleepLib/loader_plugins/contec_ble_import.h"
#include "SleepLib/loader_plugins/contec_ble_link.h"

class QCheckBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTableWidget;

/*! \class BluetoothOximeterPage
    \brief Oximeter wizard page that imports every new record from a Contec oximeter over
    Bluetooth, optionally sets its clock and (only when it is safe) erases it. */
class BluetoothOximeterPage : public QWidget
{
    Q_OBJECT
public:
    explicit BluetoothOximeterPage(QWidget *parent = nullptr);
    ~BluetoothOximeterPage() override;

    void start();
    bool isBusy() const { return m_busy; }
    void cancel();

signals:
    void finished(bool importedSomething);

private:
    enum Step { StepScan = 0, StepRead, StepDownload, StepSave, StepCount };
    struct Row {
        ContecBle::RecordHeader header;
        ContecBle::Decision decision = ContecBle::Decision::Import;
        bool pending = false;
        ContecBle::Outcome outcome = ContecBle::Outcome::NotDownloaded;
        QString otherDevice;
    };

    void setStep(int step, bool allDone = false);
    void connectTo(const ContecBleFoundDevice &device);
    void onScanFinished(const QList<ContecBleFoundDevice> &devices);
    void onLinkReady();
    bool wantRecord(const ContecBle::RecordHeader &h);
    void onRecordCount(int count);
    void onRecordDownloaded(const ContecBle::Record &r);
    void onProgress(int done, int total);
    void onDownloadFinished();
    void onClockSet(bool ok);
    void afterClock();
    void onEraseFinished(bool ok);
    void onFailed(const QString &message);
    void finish(const QString &error);
    void stopDevice();
    void addTableRow(const Row &row);
    void updateTableRow(int index);
    QString outcomeText(const Row &row) const;
    QString summaryText(const QString &error) const;

    QLabel *m_stepLabels[StepCount] = {};
    QString m_stepTexts[StepCount];
    QListWidget *m_deviceList = nullptr;
    QPushButton *m_connectButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QCheckBox *m_syncClock = nullptr;
    QCheckBox *m_eraseAfter = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_retryButton = nullptr;
    QPushButton *m_doneButton = nullptr;

    ContecBleScanner *m_scanner = nullptr;
    QPointer<QtContecBleLink> m_link;
    QPointer<ContecBleDownloader> m_downloader;
    std::unique_ptr<ContecBleImporter> m_importer;
    QList<ContecBleFoundDevice> m_found;
    QList<Row> m_rows;

    QString m_deviceName;
    QString m_model;
    QDateTime m_downloadStarted;
    QDateTime m_lastRecordEnd;
    int m_headersOnDevice = 0;
    bool m_downloadCompleted = false;
    bool m_importedAny = false;
    bool m_busy = false;
    QString m_clockText;
    QString m_eraseText;
};

#endif // BLUETOOTHOXIMETERPAGE_H
