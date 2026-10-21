/* Contec RS01 Import Page Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RS01PAGE_H
#define RS01PAGE_H

#include <QDateTime>
#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;

/*! \class RS01Page
    \brief Oximeter wizard page for the Contec RS01 sleep screener: finds its USB disk ("SLEEP
    METER") or a folder with a copy, lists the recordings (new ones ticked, those in OSCAR
    marked) and imports the ticked ones. Only reads the device's disk; sets its clock over USB HID
    when the device is plugged in, and on «Set clock». */
class RS01Page : public QWidget
{
    Q_OBJECT
public:
    explicit RS01Page(QWidget *parent = nullptr);

    //! Looks for the device and lists its recordings (or asks for a folder).
    void start();
    //! Lists the recordings under \a root.
    void listRecordings(const QString &root);
    //! What to tell when the device's volume at \a mountPoint (\a device) is mounted writable.
    static QString writableWarning(const QString &mountPoint, const QString &device);
    //! The line about the device's clock: set to \a when (\a ok), or not, because \a error.
    static QString clockText(bool ok, const QDateTime &when, const QString &error);
    bool importedAny() const { return m_imported > 0; }

signals:
    //! The user is done with the page; \a imported: something new is in OSCAR.
    void finished(bool imported);
    //! An import started (true) or ended: the wizard keeps Cancel off meanwhile.
    void busyChanged(bool busy);

private:
    void chooseFolder();
    void importChosen();
    void setDeviceClock();

    QLabel *m_status = nullptr;
    QLabel *m_clock = nullptr;
    QLabel *m_warning = nullptr;
    QTreeWidget *m_list = nullptr;
    QPushButton *m_folderButton = nullptr;
    QPushButton *m_clockButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_doneButton = nullptr;
    int m_imported = 0;
};

#endif // RS01PAGE_H
