/* Sleep Analysis Preferences Page Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSISPREFS_H
#define ANALYSISPREFS_H

#include <QList>
#include <functional>
#include <QWidget>

#include "SleepLib/analysis/analysis_params.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QTimer;

/*! \class AnalysisPreferencesPage
    \brief The Analysis tab of the Preferences dialog (spec §5.4): OSCAR's own sleep
    analysis on or off, the hypopnea rule with what each one means, the options, the
    SpO2 thresholds for "time below", and every threshold under Advanced.
    */
class AnalysisPreferencesPage : public QWidget
{
    Q_OBJECT
  public:
    explicit AnalysisPreferencesPage(QWidget *parent = nullptr);

    void load(const analysis::AnalysisParams &params, const QList<double> &spo2Thresholds);
    analysis::AnalysisParams params() const;
    //! The thresholds typed in (at most six, 50-100 %), highest first.
    QList<double> spo2Thresholds() const;

    //! What a hypopnea rule means, as shown under the selector.
    static QString ruleDescription(analysis::HypopneaRule rule);

    //! Shows under the switch how many nights the settings on the page would recalculate, as
    //! \a counter says; \a enabledNow: whether the analysis is on in the saved settings.
    void setRecalculationCounter(const std::function<int(const analysis::AnalysisParams &)> &counter, bool enabledNow);

  private:
    void updateRecalculationNote();
    void showRuleDescription();
    void resetToDefaults();
    QDoubleSpinBox *number(double min, double max, double step, int decimals, const QString &suffix);

    QCheckBox *m_enabled;
    QComboBox *m_rule;
    QLabel *m_ruleText;
    QCheckBox *m_limitOxi;
    QCheckBox *m_pulseArousal;
    QCheckBox *m_classify;
    QLineEdit *m_thresholds;
    QGroupBox *m_advanced;

    struct Field {
        QDoubleSpinBox *box;
        double *(*field)(analysis::AnalysisParams &);
        double scale;   // shown value = stored value * scale (fractions as percent)
    };
    QList<Field> m_fields;
    QDoubleSpinBox *m_zoneMinDesats;
    QLabel *m_recalcNote;
    QTimer *m_recalcTimer;
    std::function<int(const analysis::AnalysisParams &)> m_counter;
    bool m_enabledNow = true;
};

#endif // ANALYSISPREFS_H
