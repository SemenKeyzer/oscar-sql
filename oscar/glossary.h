/* Glossary
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GLOSSARY_H
#define GLOSSARY_H

#include <QList>
#include <QString>
#include <QStringList>

#include "SleepLib/machine_common.h"

//! One explanation of a figure or a term, translated.
struct GlossaryEntry {
    QString key;
    QString term;         //!< "AHI"
    QString expansion;    //!< "Apnea-Hypopnea Index"
    QString summary;      //!< what it is, in a sentence or two (the hover tooltip)
    QString details;      //!< how it is counted and how to read it (the help panel)
    QString norm;         //!< the norm or a guide; empty when there is none
    bool experimental = false;   //!< a measure of OSCAR's own analysis, not a medical norm
    QStringList seeAlso;  //!< keys of related entries
    QStringList channels; //!< codes of the channels this entry explains
    // the controls' entries (key "ui.<window>.<objectName>")
    QString place;        //!< where the control is: "Preferences → Import"
    QString advice;       //!< what to choose; empty when there is nothing to advise
    QString caution;      //!< what the action deletes or recalculates; empty for safe controls
};

//! The explanations shown when hovering OSCAR's figures and in the help panel.
namespace Glossary {
const GlossaryEntry *find(const QString &key);
QList<GlossaryEntry> all();
//! A short HTML explanation for a tooltip; empty for an unknown key.
QString tooltip(const QString &key);
//! The full HTML article for the help panel; empty for an unknown key.
QString panel(const QString &key);
//! The entry explaining a channel; empty when there is none.
QString keyForChannel(ChannelID code);
//! The tooltip of the channel's entry, or else the channel's own name and description.
QString channelTooltip(ChannelID code);
//! Keys of the entries whose texts contain \a text (case and ё/е ignored).
QStringList search(const QString &text);
//! Keys of the controls whose explanation must carry a caution.
QStringList cautionKeys();
}

#endif // GLOSSARY_H
