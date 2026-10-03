/* Doctor Report Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DOCTORREPORTTESTS_H
#define DOCTORREPORTTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the one-page report for the doctor.
class DoctorReportTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testLevels();
    void testChartLayout();
    void testChart();
    void testChartLongPeriod();
    void testHtmlHeader();
    void testHtmlWithoutPersonalData();
    void testHtmlWithoutOximetry();
    void testHtmlWithoutAnalysis();
    void testHtmlRdi();
    void testHtmlSigns();
    void testHtmlEscapes();
    void testWritePdf();
    void testWritePdfFailsOnBadPath();
    void testDefaultFrom();
    void testHtmlAnalysisMissing();

private:
    class QApplication *m_app = nullptr;
};
DECLARE_TEST(DoctorReportTests)

#endif // DOCTORREPORTTESTS_H
