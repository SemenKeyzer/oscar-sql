/* Time Alignment Session Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef TIMEALIGNSESSION_H
#define TIMEALIGNSESSION_H

#include <QObject>
#include <QDate>
#include <QList>
#include <QString>
#include <memory>
#include <optional>

#include "database/device_time_correction_repository.h"

class Machine;

//! \brief Storage for single-night alignment offsets. The default implementation wraps
//!        DeviceTimeCorrectionRepository; unit tests substitute an in-memory store.
class TimeAlignStore
{
public:
    virtual ~TimeAlignStore() = default;
    virtual QList<DeviceTimeCorrectionData> findActive(qint64 machineId) = 0;
    virtual QList<DeviceTimeCorrectionData> findManualOffsetRows(qint64 machineId) = 0;
    //! \brief Replaces the single-night 'offset' row for \a date; 0 removes it.
    //! \return false on a database error.
    virtual bool upsertOffset(qint64 machineId, const QString& date, qint64 offsetMs) = 0;
};

class RepositoryTimeAlignStore : public TimeAlignStore
{
public:
    QList<DeviceTimeCorrectionData> findActive(qint64 machineId) override;
    QList<DeviceTimeCorrectionData> findManualOffsetRows(qint64 machineId) override;
    bool upsertOffset(qint64 machineId, const QString& date, qint64 offsetMs) override;

private:
    DeviceTimeCorrectionRepository m_repo;
};

/*! \class TimeAlignSession
    \brief Edits one device's single-night time offset with a live, in-memory preview.

    The value being edited is the device's 'offset' row whose date_from and date_to are both
    the night. Other active corrections for the device keep applying and add to it, exactly as
    in Machine::correctionMs(). Nothing is written until commit(); cancel() restores the stored
    corrections. */
class TimeAlignSession : public QObject
{
    Q_OBJECT
public:
    static constexpr qint64 kMaxOffsetMs   = 12LL * 3600 * 1000;
    static constexpr qint64 kLargeOffsetMs = 3LL * 3600 * 1000;

    //! \param store Storage to use; nullptr uses the database repository. The session owns it.
    explicit TimeAlignSession(TimeAlignStore* store = nullptr, QObject* parent = nullptr);
    ~TimeAlignSession() override;

    //! \brief Starts editing \a mach on \a night, dropping any unsaved preview first.
    //! \return false if the machine has no database id (its corrections cannot be stored).
    bool begin(Machine* mach, const QDate& night);
    //! \brief Stops editing without touching the machine's corrections (commit or cancel first).
    void end();

    bool     isActive() const { return m_machine != nullptr; }
    Machine* machine() const { return m_machine; }
    QDate    night() const { return m_night; }
    qint64   offsetMs() const { return m_offsetMs; }
    qint64   savedMs() const { return m_savedMs; }
    bool     isDirty() const { return isActive() && (m_offsetMs != m_savedMs); }

    //! \brief Sum of the device's other active corrections on this night.
    qint64 otherCorrectionsMs() const;
    //! \brief The single-night offset of the nearest earlier night, if any.
    std::optional<qint64> previousNightOffset() const;

    //! \brief Previews \a ms (clamped to +/-kMaxOffsetMs) as this night's offset.
    void setOffsetMs(qint64 ms);
    void nudge(qint64 deltaMs) { setOffsetMs(m_offsetMs + deltaMs); }

    //! \brief Stores the previewed offset. On failure the preview stays and false is returned.
    bool commit();
    //! \brief Drops the preview and restores the stored corrections.
    void cancel();

    //! \brief Drag rounding step for the given zoom: 1 min, 10 s or 1 s.
    static qint64 snapStepMs(double msPerPx);
    //! \brief Rounds a raw drag distance to the step for this zoom.
    static qint64 snapDelta(double rawDeltaMs, double msPerPx);
    //! \brief Formats an offset as "+HH:MM:SS" / "-HH:MM:SS".
    static QString formatOffset(qint64 ms);

signals:
    void offsetChanged(qint64 ms);

private:
    bool isThisNightsOffsetRow(const DeviceTimeCorrectionData& d) const;
    void applyPreview();
    void restoreStored(Machine* mach);

    std::unique_ptr<TimeAlignStore> m_store;
    Machine* m_machine = nullptr;
    QDate    m_night;
    qint64   m_savedMs  = 0;
    qint64   m_offsetMs = 0;
    QList<DeviceTimeCorrectionData> m_otherRows;   //!< active rows except this night's own offset row
};

#endif // TIMEALIGNSESSION_H
