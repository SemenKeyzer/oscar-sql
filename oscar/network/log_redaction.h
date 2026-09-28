/* Log redaction helpers
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef LOG_REDACTION_H
#define LOG_REDACTION_H

#include <QString>
#include <QUrl>

/*!
 * \brief Shortened form of a share or download address, safe to write to debug.txt.
 *
 * A share link is a bearer credential: anyone holding it can download the health
 * data behind it, and debug.txt is attached to support requests.  Only the scheme
 * and host are kept.
 */
inline QString redactedUrl(const QString& url)
{
    const QUrl u(url);
    if (!u.isValid() || u.host().isEmpty()) {
        return QStringLiteral("<link>");
    }
    return u.scheme() + QStringLiteral("://") + u.host() + QStringLiteral("/…");
}

#endif // LOG_REDACTION_H
