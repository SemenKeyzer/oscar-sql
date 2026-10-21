/* Contec RS01 USB HID
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "rs01_hid.h"

#include <QObject>
#include "rs01_clock.h"

#ifdef OSCAR_HIDAPI
#include "hidapi.h"
#endif

namespace {

#ifdef OSCAR_HIDAPI
constexpr unsigned short kVendor = 0x28E9;
constexpr unsigned short kProduct = 0x01F3;
constexpr int kTimeoutMs = 1000;

// Sends one 64-byte command (report ID 0 in front) and returns the first reply that \a accepted
// takes; a few unrelated reports are let pass.
QByteArray exchange(hid_device *dev, const QByteArray &command, bool (*accepted)(const QByteArray &))
{
    const QByteArray report = QByteArray(1, '\0') + command;
    if (hid_write(dev, reinterpret_cast<const unsigned char *>(report.constData()), size_t(report.size())) < 0) return QByteArray();
    for (int attempt = 0; attempt < 3; ++attempt) {
        unsigned char buf[65] = {};
        const int n = hid_read_timeout(dev, buf, sizeof(buf), kTimeoutMs);
        if (n <= 0) return QByteArray();
        const QByteArray reply(reinterpret_cast<const char *>(buf), n);
        if (accepted(reply)) return reply;
        if (attempt == 2) return reply;
    }
    return QByteArray();
}
#endif

} // namespace

bool RS01Hid::available()
{
#ifdef OSCAR_HIDAPI
    return true;
#else
    return false;
#endif
}

bool RS01Hid::setClock(const QDateTime &local, Error *error)
{
    auto fail = [error](Error e) {
        if (error) *error = e;
        return false;
    };
#ifdef OSCAR_HIDAPI
    const QByteArray clock = rs01ClockCommand(local);
    if (clock.isEmpty()) return fail(Error::BadReply);
    if (hid_init() != 0) return fail(Error::CannotOpen);
    hid_device_info *found = hid_enumerate(kVendor, kProduct);
    const bool present = found != nullptr;
    hid_free_enumeration(found);
    if (!present) {
        hid_exit();
        return fail(Error::NotFound);
    }
    hid_device *dev = hid_open(kVendor, kProduct, nullptr);
    if (!dev) {
        hid_exit();
        return fail(Error::CannotOpen);
    }
    Error result = Error::None;
    const QByteArray who = exchange(dev, rs01IdentifyCommand(), rs01IsIdentifyReply);
    if (who.isEmpty()) result = Error::NoReply;
    else if (!rs01IsIdentifyReply(who)) result = Error::NotRS01;
    else {
        const QByteArray done = exchange(dev, clock, rs01IsClockReply);
        if (done.isEmpty()) result = Error::NoReply;
        else if (!rs01IsClockReply(done)) result = Error::BadReply;
    }
    hid_close(dev);
    hid_exit();
    if (result != Error::None) return fail(result);
    if (error) *error = Error::None;
    return true;
#else
    Q_UNUSED(local)
    return fail(Error::NotAvailable);
#endif
}

QString RS01Hid::errorText(Error error)
{
    switch (error) {
    case Error::None: return QString();
    case Error::NotAvailable: return QObject::tr("setting the clock is not available on this system");
    case Error::NotFound: return QObject::tr("the RS01 was not found on USB (switch it on before plugging in the cable)");
    case Error::CannotOpen: return QObject::tr("the RS01 could not be opened (another program may be using it, or the system asks for permission)");
    case Error::NotRS01: return QObject::tr("the device did not answer as an RS01");
    case Error::NoReply: return QObject::tr("the RS01 did not answer");
    case Error::BadReply: return QObject::tr("the RS01 did not confirm the new time");
    }
    return QString();
}
