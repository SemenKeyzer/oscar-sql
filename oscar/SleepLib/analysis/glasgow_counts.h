/* Glasgow Index counts
 *
 * Copyright (c) 2026 The OSCAR Team
 * The Glasgow Index is by DaveSkvn (https://github.com/DaveSkvn/GlasgowIndex, GPL-3.0-or-later).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GLASGOW_COUNTS_H
#define GLASGOW_COUNTS_H

#include <QString>
#include <QVector>
#include <array>

namespace analysis {

//! The shape signs of an inspiration the Glasgow Index looks for, in the author's order.
enum GlasgowComponent { GiSkew, GiSpike, GiFlatTop, GiTopHeavy, GiMultiPeak, GiNoPause,
                        GiInspirRate, GiMultiBreath, GiAmpVar, GiComponentCount };

//! How many inspirations were looked at and how many showed each sign.
struct GlasgowCounts {
    int breaths = 0;
    std::array<int, GiComponentCount> flagged {};

    bool isEmpty() const { return breaths == 0; }
    //! flagged / breaths; NaN when empty.
    double fraction(GlasgowComponent c) const;
    //! The index: the fractions summed, Top Heavy left out as the author does; NaN when empty.
    double index() const;
    GlasgowCounts &operator+=(const GlasgowCounts &o);
    bool operator==(const GlasgowCounts &o) const { return breaths == o.breaths && flagged == o.flagged; }
    //! The nine counts as "a,b,…"; empty when there are no breaths.
    QString toText() const;
    static GlasgowCounts fromText(int breaths, const QString &text);
};

//! One inspiration's signs.
struct GlasgowBreath {
    qint64 start = 0;      //!< ms
    std::array<bool, GiComponentCount> flags {};
    bool counted = true;   //!< false: left out (inside an event or unscoreable time)
};

struct GlasgowResult {
    GlasgowCounts counts;
    QVector<GlasgowBreath> breaths;
};

} // namespace analysis

#endif // GLASGOW_COUNTS_H
