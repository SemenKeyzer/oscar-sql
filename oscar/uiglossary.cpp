/* Explanations of OSCAR's controls
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "uiglossary.h"

#include <QCoreApplication>
#include <QStringList>

#include "glossary.h"

namespace {

// clang-format off
const UiGlossaryRaw kUiEntries[] = {
    // ---- menus
    { "ui.menu.actionPurgeCurrentDayAll", QT_TRANSLATE_NOOP("Glossary", "All including Notes"),
      QT_TRANSLATE_NOOP("Glossary", "Data menu → Purge Current Selected Day"),
      QT_TRANSLATE_NOOP("Glossary", "Removes everything recorded for the selected day: device data, oximetry, notes and bookmarks."),
      QT_TRANSLATE_NOOP("Glossary", "To load a night again, choose \"CPAP\" in the same menu instead: it keeps the notes."),
      QT_TRANSLATE_NOOP("Glossary", "Notes and bookmarks are lost for good; device data comes back only by importing the card again."), "" },

    // ---- preferences
    { "ui.prefs.timeEdit", QT_TRANSLATE_NOOP("Glossary", "Day Split Time"),
      QT_TRANSLATE_NOOP("Glossary", "Preferences → Import"),
      QT_TRANSLATE_NOOP("Glossary", "Sessions that start before this time are counted for the previous day."),
      QT_TRANSLATE_NOOP("Glossary", "Midday (12:00) suits most people. Move it only if you sleep during the day."), "", "sessions" },
};
// clang-format on

// the controls whose action deletes data, recalculates or replaces what was imported
const char *const kCautionKeys[] = {
    "ui.menu.actionPurgeCurrentDayAll",
};

} // namespace

const UiGlossaryRaw *uiGlossaryEntries(int &count)
{
    count = int(sizeof(kUiEntries) / sizeof(kUiEntries[0]));
    return kUiEntries;
}

QStringList Glossary::cautionKeys()
{
    QStringList out;
    for (const char *k : kCautionKeys) out << QString::fromLatin1(k);
    return out;
}
