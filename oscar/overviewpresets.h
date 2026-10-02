/* Overview Graph Presets Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OVERVIEWPRESETS_H
#define OVERVIEWPRESETS_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

//! Ready-made sets of Overview graphs, shown with one click instead of ticking graphs one by one.
namespace OverviewPresets {

enum Preset {
    All,        //!< the user's own choice from the graph menu
    Therapy,
    MaskLeaks,
    Oxygen,
    Analysis,
};

//! Graph visibility by graph name (gGraph::name()).
typedef QHash<QString, bool> Visibility;

//! Every preset, in button order.
QList<Preset> presets();
//! The preset's button text.
QString title(Preset preset);
//! The name the profile stores the preset under.
QString key(Preset preset);
//! The preset stored as \a key; All for anything unknown.
Preset fromKey(const QString &key);
//! Names of the graphs \a preset shows; empty for All, which shows the user's own choice.
QStringList graphNames(Preset preset);
//! What \a preset shows of the graphs in \a own: \a own itself for All, otherwise the preset's
//! graphs and nothing else.
Visibility visibility(Preset preset, const Visibility &own);

/*! \brief Remembers the user's own choice while a preset is shown, so leaving the preset or
    saving the layout gets that choice back untouched. */
class State
{
  public:
    Preset preset() const { return m_preset; }
    //! Switches to \a preset; \a current is what the graphs show now. Returns what to show.
    Visibility switchTo(Preset preset, const Visibility &current);
    //! After the graphs were rebuilt with the user's own choice \a own: what to show now.
    Visibility reload(const Visibility &own);
    //! The user's own choice, to save, given what the graphs show now (\a current).
    Visibility own(const Visibility &current) const;

  private:
    Preset m_preset = All;
    Visibility m_own;
};

} // namespace OverviewPresets

#endif // OVERVIEWPRESETS_H
