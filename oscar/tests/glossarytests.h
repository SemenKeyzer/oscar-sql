/* Glossary Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GLOSSARYTESTS_H
#define GLOSSARYTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the explanations shown on hover and in the help panel.
class GlossaryTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testEntriesComplete();
    void testRequiredKeys();
    void testTooltipAndPanel();
    void testChannelFallback();
    void testSearch();
};
DECLARE_TEST(GlossaryTests)

#endif // GLOSSARYTESTS_H
