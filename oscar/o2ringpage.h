/* O2Ring Import Page Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef O2RINGPAGE_H
#define O2RINGPAGE_H

#include <QList>
#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <memory>

#include "SleepLib/loader_plugins/o2ring_import.h"
#include "SleepLib/loader_plugins/o2ring_link.h"

class QCheckBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTableWidget;

/*! \class O2RingPage
    \brief Oximeter wizard page that downloads the chosen records from an O2Ring over Bluetooth
    and imports them; new records are ticked, the ones already in OSCAR are not. Never erases. */
class O2RingPage : public QWidget
{
    Q_OBJECT
public:
    explicit O2RingPage(QWidget *parent = nullptr);
    ~O2RingPage() override;

    //! Searches for rings, connects and lists the ring's records.
    void start();
    //! As start(), over \a link that is already connected (\a name: the ring's Bluetooth name).
    void startWithLink(O2RingLink *link, const QString &name);
    void cancel();
    bool isBusy() const { return m_busy; }
    bool importedAny() const { return m_imported > 0; }
    //! How long to wait for each answer of the ring (tests shorten it).
    void setResponseTimeout(int ms) { m_timeoutMs = ms; }

signals:
    //! The import stopped (done or failed); the wizard now offers Done, and Retry when \a canRetry.
    void ended(bool canRetry);

private:
    void reset();
    void connectTo(const O2RingFoundDevice &device);
    void onScanFinished(const QList<O2RingFoundDevice> &devices);
    void begin(O2RingLink *link);
    void onInfo(const O2Ring::Info &info);
    void onDownloadClicked();
    void downloadChosen();
    void onFileProgress(const QString &name, qint64 done, qint64 total);
    void onFileDownloaded(const QString &name, const QByteArray &data);
    void onFileFailed(const QString &name, const QString &error);
    void finish(const QString &error);
    void showSummary();
    void stopDevice();
    int rowOf(const QString &name) const;
    void setStatus(const QString &name, const QString &text);
    QCheckBox *tickOf(int row) const;

    QLabel *m_status = nullptr;
    QLabel *m_info = nullptr;
    QListWidget *m_deviceList = nullptr;
    QPushButton *m_connectButton = nullptr;
    QCheckBox *m_setClock = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_downloadButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_summary = nullptr;

    O2RingScanner *m_scanner = nullptr;
    QPointer<QtO2RingLink> m_ownLink;      //!< the Bluetooth link this page made (none in the tests)
    QPointer<O2RingDownloader> m_downloader;
    std::shared_ptr<O2RingImporter> m_importer;
    QList<O2RingFoundDevice> m_found;
    QStringList m_names;                   //!< the ring's records, in table order
    QString m_deviceName;
    int m_imported = 0;
    int m_skipped = 0;
    int m_failed = 0;
    int m_timeoutMs = 5000;
    QString m_clockError;                  //!< set when the ring did not confirm its new clock
    QString m_error;                       //!< why the import stopped, empty when it finished
    bool m_busy = false;
};

#endif // O2RINGPAGE_H
