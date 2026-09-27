/* Time Alignment Bar Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef TIMEALIGNBAR_H
#define TIMEALIGNBAR_H

#include <QFrame>
#include <QList>

class Machine;
class QComboBox;
class QLabel;
class QPushButton;

/*! \class TimeAlignBar
    \brief The strip shown above the Daily graphs while a device's time is being aligned.
    Display and signals only; TimeAlignSession holds the state. */
class TimeAlignBar : public QFrame
{
    Q_OBJECT
public:
    enum class Severity { Info, Warning };

    explicit TimeAlignBar(QWidget *parent = nullptr);

    void setDevices(const QList<Machine *> &devices, Machine *current);
    void setOffset(qint64 ms);
    void setStatus(const QString &text, Severity severity);
    void setSameAsLastNightEnabled(bool enabled);

    //! \brief "Brand Model (serial)", as in the Time Corrections dialog; the serial is omitted when empty.
    static QString deviceLabel(Machine *mach);

signals:
    void deviceChosen(Machine *mach);
    void nudgeRequested(qint64 deltaMs);
    void sameAsLastNightRequested();
    void moreOptionsRequested();
    void saveRequested();
    void cancelRequested();

private:
    QComboBox   *m_deviceCombo = nullptr;
    QLabel      *m_offsetLabel = nullptr;
    QLabel      *m_statusLabel = nullptr;
    QPushButton *m_sameAsLastNight = nullptr;
};

#endif // TIMEALIGNBAR_H
