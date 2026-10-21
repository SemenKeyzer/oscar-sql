/* Contec RS01 USB HID Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RS01_HID_H
#define RS01_HID_H

#include <QDateTime>
#include <QString>

/*! \struct RS01Hid
    \brief Talks to the RS01 over its USB HID interface (VID 28E9, PID 01F3) through hidapi: only to
    identify it and set its clock. Nothing goes to its disk. macOS and Windows only. */
struct RS01Hid {
    enum class Error { None, NotAvailable, NotFound, CannotOpen, NotRS01, NoReply, BadReply };
    //! Whether this build can talk to the device at all.
    static bool available();
    //! Sets the device's clock to \a local (the computer's local time); false with \a error why not.
    static bool setClock(const QDateTime &local, Error *error = nullptr);
    //! What to tell the user about \a error.
    static QString errorText(Error error);
};

#endif // RS01_HID_H
