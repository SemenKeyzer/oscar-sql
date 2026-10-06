/* Explanations of OSCAR's controls
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIGLOSSARY_H
#define UIGLOSSARY_H

//! One untranslated explanation of a menu item, button or setting (key "ui.<window>.<objectName>").
struct UiGlossaryRaw {
    const char *key, *term, *place, *summary, *advice, *caution;
    const char *seeAlso;   // keys, comma separated
};

//! The table of the controls' explanations; \a count gets its size.
const UiGlossaryRaw *uiGlossaryEntries(int &count);

#endif // UIGLOSSARY_H
