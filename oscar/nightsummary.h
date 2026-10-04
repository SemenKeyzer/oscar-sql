/* Start screen: the last night in figures
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef NIGHTSUMMARY_H
#define NIGHTSUMMARY_H

#include <QDate>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "SleepLib/oximetry_summary.h"

class Profile;
class QFrame;
class QLabel;
class QGridLayout;
class QVBoxLayout;
namespace analysis { class AnalysisService; }

/*! \struct NightSummary
    \brief The most recent night in figures, for the start screen: what a user wants to
    know first in the morning, each figure against its target, the 30 nights before it,
    and what needs doing.

    The targets are the profile's own (compliance hours, leak red line), an AHI below 5,
    and for oximetry less than 5 % of the time below 90 % and an ODI 3 % below 5.
    */
struct NightSummary {
    enum Level { Unknown, Good, Attention };
    static constexpr int kTrendNights = 30;
    static constexpr double kAhiTarget = 5;
    static constexpr double kT90Target = 5;     //!< % of the time below 90 %
    static constexpr double kOdiTarget = 5;     //!< desaturations of >= 3 % an hour

    QDate date;                     //!< invalid: nothing imported yet

    bool hasCpap = false;           //!< CPAP data that night
    QString cpapDevice;
    QString cpapPixmap;             //!< the device's picture
    double hours = 0;               //!< mask-on time
    double complianceHours = 4;
    double ahi = 0;                 //!< the device's
    bool hasAnalysisAhi = false;
    double analysisAhi = 0;         //!< OSCAR's analysis of the flow
    bool hasLeak = false;
    double leak = 0;                //!< weighted average
    double leakRedline = 0;         //!< 0: none set
    QString leakUnits;
    QString pressure;               //!< e.g. "9.8", or "5 / 9.8" for EPAP / IPAP
    QString pressureUnits;
    QString pressureNote;           //!< what the figure is, e.g. "95% of the time under"
    double pressureMax = 0;         //!< the APAP's upper limit; 0 when there is none
    double secondsAtMax = 0;        //!< time the pressure spent at that limit

    OximetryNight oxi;              //!< that night's oximetry (invalid without)
    QDate lastOximetry;             //!< the latest night with oximetry, when it is not this one

    //! The nights up to and including date, oldest first (at most kTrendNights, none
    //! before the first CPAP night); NaN AHI where nothing was recorded.
    QVector<double> usage, ahiTrend;

    int outdatedAnalysis = 0;       //!< nights waiting for the analysis
    bool hasOffsetHint = false;     //!< the analysis thinks the oximeter's clock is off
    qint64 offsetHintMs = 0;
    int daysSinceData = 0;          //!< from date to today

    Level usageLevel() const;
    Level ahiLevel() const;
    Level leakLevel() const;
    Level spo2Level() const;
    //! Nights of the trend used for complianceHours or more.
    int compliantNights() const;
    //! Median AHI over the trend's nights with data; NaN without any.
    double medianAhi() const;
    //! What is off its target that night, one short sentence each; empty when nothing is.
    QStringList concerns() const;
    //! "at the maximum 14: 25 min (5.8%)"; empty without an upper limit.
    QString pressureMaxNote() const;
    //! What the user may want to do now, as rich text with links (daily=, import=,
    //! analysis=recalculate) for MainWindow::sendStatsUrl().
    QStringList actions() const;
};

//! Builds the summary of the profile's most recent night (CPAP or oximetry). \a service
//! may be null (no analysis figures then).
NightSummary buildNightSummary(Profile *profile, analysis::AnalysisService *service, const QDate &today);
//! The summary of the night of \a date, as the start screen shows the latest one. Without
//! \a withTrends it leaves out the 30 nights before it and the count of outdated days.
NightSummary buildNightSummaryFor(Profile *profile, analysis::AnalysisService *service, const QDate &date,
                                  const QDate &today, bool withTrends = true);

/*! \class NightTrend
    \brief Up to 30 nights as small bars against a target line: bars at or past the target
    in one colour, the others in the attention colour, nights without data as a dot.
    */
class NightTrend : public QWidget
{
  public:
    explicit NightTrend(QWidget *parent = nullptr);
    //! \a aboveIsGood: whether reaching the target is good (usage) or bad (AHI).
    void setValues(const QVector<double> &values, double target, bool aboveIsGood, double minScale);
    QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    QVector<double> m_values;
    double m_target = 0;
    bool m_aboveIsGood = true;
    double m_minScale = 1;
};

/*! \class NightSummaryView
    \brief Shows a NightSummary as cards: the night's verdict, a tile per figure, the
    trends of the last 30 nights and what to do next.
    */
class NightSummaryView : public QWidget
{
    Q_OBJECT
  public:
    explicit NightSummaryView(QWidget *parent = nullptr);
    void setSummary(const NightSummary &summary);

    //! The colours the view uses for a level (also in the trends).
    static QColor levelColor(NightSummary::Level level);
    //! The night's usage, leak, pressure and SpO2 as small HTML tiles, two to a row, for
    //! the Daily view's sidebar; empty when there is nothing to show.
    static QString keyFiguresHtml(const NightSummary &s);

  signals:
    //! A link was clicked: daily=, import=cpap or analysis=recalculate.
    void linkActivated(const QString &link);

  private:
    QFrame *addTile(int column, const QString &caption, const QString &value, const QString &note,
                    NightSummary::Level level, const QString &tooltip = QString());
    QLabel *richLabel(const QString &text, const QString &objectName);
    void clearTiles();

    QLabel *m_icon;
    QLabel *m_title;
    QLabel *m_verdict;
    QLabel *m_concerns;
    QGridLayout *m_tiles;
    QFrame *m_trendsFrame;
    QLabel *m_usageCaption;
    NightTrend *m_usageTrend;
    QLabel *m_ahiCaption;
    NightTrend *m_ahiTrend;
    QLabel *m_actions;
};

#endif // NIGHTSUMMARY_H
