/* Welcome page Implementation
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of Source Code. */

#include <cmath>
// remember to turn test_macros off for release.
#define TEST_MACROS_ENABLEDoff
#include <test_macros.h>

#include <QTimer>
#include <QResizeEvent>
#include <QTextBrowser>

#include "welcome.h"
#include "SleepLib/oximetry_summary.h"
#include "nightsummary.h"
#include "SleepLib/analysis/analysis_service.h"
#include "ui_welcome.h"

#include "mainwindow.h"
extern MainWindow * mainwin;


Welcome::Welcome(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Welcome)
{
    ui->setupUi(this);
    pixmap.load(":/icons/mask.png");
    m_nightSummary = new NightSummaryView(ui->frame);
    ui->verticalLayout_2->insertWidget(0, m_nightSummary);
    connect(m_nightSummary, &NightSummaryView::linkActivated, this, [](const QString &link) {
        mainwin->sendStatsUrl(link);   // daily=, import=cpap, analysis=recalculate
    });

    refreshPage();
}

Welcome::~Welcome()
{
    delete ui;
}

void Welcome::refreshPage()
{
    bool b;

    const auto & mlist = p_profile->GetMachines(MT_CPAP);
    b = mlist.size() > 0;

    QList<Machine *> oximachines = p_profile->GetMachines(MT_OXIMETER);
    QList<Machine *> posmachines = p_profile->GetMachines(MT_POSITION);
    QList<Machine *> stgmachines = p_profile->GetMachines(MT_SLEEPSTAGE);

    bool noMachines = mlist.isEmpty() && posmachines.isEmpty() && oximachines.isEmpty() && stgmachines.isEmpty();

    bool showCardWarning = noMachines;

    // The SDCard warning does not need to be seen anymore for people who DON'T use ResMed S9's.. show first import and only when S9 is present
    for (auto & mach :mlist) {
        if (mach->brand().contains(STR_MACH_ResMed) && mach->series().contains("S9")) showCardWarning = true;
    }

    ui->S9Warning->setVisible(showCardWarning);

    if (!b) {
        qDebug() << "No devices in Profile";
//        sleep(3);
        ui->cpapIcon->setPixmap(pixmap);
    }

    b = !noMachines;

    // Copy application font to tool buttons
    ui->importButton->setFont(QApplication::font());
    ui->dailyButton->setFont(QApplication::font());
    ui->overviewButton->setFont(QApplication::font());
    ui->statisticsButton->setFont(QApplication::font());
    ui->oximetryButton->setFont(QApplication::font());

    // Enable buttons that might be disabled
    ui->dailyButton->setEnabled(b);
    ui->oximetryButton->setEnabled(true);  // Import features always enabled
    ui->overviewButton->setEnabled(b);
    ui->statisticsButton->setEnabled(b);

    ui->importButton->repaint();
    ui->dailyButton->repaint();
    ui->overviewButton->repaint();
    ui->statisticsButton->repaint();
    ui->oximetryButton->repaint();

    mainwin->EnableTabs(b);

    // The last night as cards once there is data; the prose only before that, where it
    // says what to do first (generating it as well would load the night's SpO2 again).
    if (!showNightSummary()) {
        ui->cpapInfo->setHtml(GenerateCPAPHTML());
        ui->oxiInfo->setHtml(GenerateOxiHTML());
    }
    QTimer::singleShot(0, this, &Welcome::adjustInfoBrowserHeights);
}

bool Welcome::showNightSummary()
{
    const NightSummary s = buildNightSummary(p_profile, mainwin ? mainwin->analysisService() : nullptr,
                                             QDate::currentDate());
    const bool show = s.date.isValid();
    m_nightSummary->setVisible(show);
    if (show) m_nightSummary->setSummary(s);
    // The prose stays for a profile without data: it says what to do first.
    for (QWidget *w : { static_cast<QWidget *>(ui->cpapIcon), static_cast<QWidget *>(ui->cpapInfoFrame),
                        static_cast<QWidget *>(ui->oxiIcon), static_cast<QWidget *>(ui->oxiInfoFrame) }) {
        if (show) w->setVisible(false);
    }
    if (!show) {
        ui->cpapIcon->setVisible(true);
        ui->cpapInfoFrame->setVisible(true);
    }
    return show;
}

void Welcome::on_dailyButton_clicked()
{
    QTimer::singleShot(0, mainwin, []{ mainwin->JumpDaily(); });
}

void Welcome::on_overviewButton_clicked()
{
    QTimer::singleShot(0, mainwin, []{ mainwin->JumpOverview(); });
}

void Welcome::on_statisticsButton_clicked()
{
    QTimer::singleShot(0, mainwin, []{ mainwin->JumpStatistics(); });
}

void Welcome::on_oximetryButton_clicked()
{
    mainwin->JumpOxiWizard();
}

void Welcome::on_importButton_clicked()
{
    mainwin->JumpImport();
}


void Welcome::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, &Welcome::adjustInfoBrowserHeights);
}

void Welcome::adjustInfoBrowserHeights()
{
    for (auto* browser : {ui->cpapInfo, ui->oxiInfo}) {
        int vw = browser->viewport()->width();
        if (vw <= 0) continue;
        browser->document()->setTextWidth(vw);
        int h = int(browser->document()->size().height());
        browser->setMinimumHeight(h);
    }
}

extern EventDataType calcAHI(QDate start, QDate end);
extern EventDataType calcFL(QDate start, QDate end);


QString Welcome::GenerateCPAPHTML()
{
    auto cpap_machines = p_profile->GetMachines(MT_CPAP);
    auto oximeters = p_profile->GetMachines(MT_OXIMETER);
    QList<Machine *> mach;

    mach.append(cpap_machines);
    mach.append(oximeters);

    bool havecpapdata = false;
    bool haveoximeterdata = false;

    for (auto & mach : cpap_machines) {
        int daysize = mach->day.size();
        if (daysize > 0) {
            havecpapdata = true;
            break;
        }
    }
    for (auto & mach : oximeters) {
        int daysize = mach->day.size();
        if (daysize > 0) {
            haveoximeterdata = true;
            break;
        }
    }


    QString html = QString("<html><head>")+
//    "</head>"
    "<style type='text/css'>"
    "p,a,td,body { font-family: '"+QApplication::font().family()+"'; }"
    "p,a,td,body { font-size: "+QString::number(QApplication::font().pointSize() + 2)+"px; }"
    "body { color: black; }"
    "</style>"
    "</head>"

    "<body leftmargin=5 topmargin=10 rightmargin=5 bottommargin=5 vertical-align=center align=center>";
    html += "<font size='+0'>" ;

    Machine * cpap = nullptr;
    if (!havecpapdata && !haveoximeterdata) {
        html += "<p>" + tr("It would be a good idea to check File->Preferences first,") + "<br />" +
                        tr("as there are some options that affect import.")+"</p>" +
        "<p>" + tr("Note that some preferences are forced when a ResMed device is detected") + "</p>" +
        "<p>" + tr("First import can take a few minutes.") + "</p>";
    } else {
        QDate date = p_profile->LastDay(MT_CPAP);
        Day *day = p_profile->GetDay(date, MT_CPAP);

        if (havecpapdata && day) {
            cpap = day->machine(MT_CPAP);
        }
        if (day && (cpap != nullptr)) {
            QString cpapimage = cpap->getPixmapPath();
            ui->cpapIcon->setPixmap(QPixmap(cpapimage));

            html+= "<b>"+tr("The last time you used your %1...").arg(cpap->brand()+" "+cpap->model())+"</b><br/>";

            int daysto = date.daysTo(QDate::currentDate());
            QString daystring;
            if (daysto == 1) daystring += tr("last night");
            else if (daysto == 2) daystring += tr("1 day ago");
            else if (daysto == 0) daystring += tr("today");
            else daystring += tr("%2 days ago").arg(daysto-1);

            html += tr("was %1 (on %2)").arg(daystring).arg(QLocale::system().toString(date, QLocale::LongFormat)) + "<br/>";

            EventDataType hours = day->hours(MT_CPAP);
            html += "<br/>";

            int seconds = int(hours * 3600.0) % 60;
            int minutes = int(hours * 60) % 60;
            int hour = hours;
            QString timestr = tr("%1 hours, %2 minutes and %3 seconds").arg(hour).arg(minutes).arg(seconds);

            const EventDataType compliance_min = p_profile->cpap->m_complianceHours; // 4.0;
            if (hours > compliance_min) html += tr("Your device was on for %1.").arg(timestr)+"<br/>";
            else html += tr("<font color = red>You only had the mask on for %1.</font>").arg(timestr)+"<br/>";


            int averagedays = 7; // how many days to look back

            QDate starttime = date.addDays(-averagedays);
            QDate endtime = date.addDays(-1);


//            EventDataType ahi = (day->count(CPAP_AllApnea) + day->count(CPAP_Obstructive) + day->count(CPAP_Hypopnea) + day->count(CPAP_ClearAirway) + day->count(CPAP_Apnea)) / hours;
            // A night with no mask-on time (every slice off) has no hours: 0, not "nan".
            EventDataType ahi = (hours > 0) ? day->count(AllAhiChannels) / hours : 0;
            EventDataType ahidays = calcAHI(starttime, endtime);

            const QString under = tr("under");
            const QString over = tr("over");
            const QString close = tr("reasonably close to");
            const QString equal = tr("equal to");


            QString comp;
            if ((ahi < ahidays) && ((ahidays - ahi) >= 0.1)) {
                comp = under;
            } else if ((ahi > ahidays) && ((ahi - ahidays) >= 0.1)) {
                comp = over;
            } else if ((fabs(ahi - ahidays) >= 0.01) ) {
                comp = close;
            } else {
                comp = equal;
            }

            html += tr("You had an AHI of %1, which is %2 your %3 day average of %4.").arg(ahi,0,'f',2).arg(comp).arg(averagedays).arg(ahidays,0,'f',2);

            html += "<br/>";

            CPAPMode cpapmode = (CPAPMode)(int)day->settings_max(CPAP_Mode);
            ChannelID pressChanID = day->getPressureChannelID();     // Get channel id for pressure that we should report
            double perc= p_profile->general->prefCalcPercentile();

            // When CPAP_PressureSet and CPAP_IPAPSet have data (used for percentiles, etc.)
            // CPAP_Pressure and CPAP_IPAP are their corresponding settings channels.
            ChannelID pressSettingChanID;
            if (pressChanID == CPAP_PressureSet) {
                pressSettingChanID = CPAP_Pressure;
            } else if (pressChanID == CPAP_IPAPSet) {
                pressSettingChanID = CPAP_IPAP;
            } else {
                pressSettingChanID = pressChanID;
            }

            ChannelID epapDataChanID = CPAP_EPAP;
            if (day->channelHasData(CPAP_EPAPSet)) {
                epapDataChanID = CPAP_EPAPSet;
            }
            if (day->channelHasData(CPAP_EEPAP)) {
                epapDataChanID = CPAP_EEPAP;
            }

            if (pressChanID == NoChannel) {
                qWarning() << "Unable to find pressure channel for welcome summary!";
            }
            if (cpapmode == MODE_CPAP) {
                pressSettingChanID = CPAP_Pressure;  // DreamStation ventilators report EPAP/IPAP data, but the setting is Pressure
                EventDataType pressure = day->settings_max(pressSettingChanID);
                qDebug() << pressSettingChanID << pressure;
                html += tr("Your CPAP device used a constant %1 %2 of air")
                        .arg(pressure)
                        .arg(schema::channel[pressChanID].units());
            } else if (cpapmode == MODE_APAP) {
                EventDataType pressure = day->percentile(pressChanID, perc/100.0);
                html += tr("Your pressure was under %1 %2 for %3% of the time.")
                        .arg(pressure)
                        .arg(schema::channel[pressChanID].units())
                        .arg(perc);
            } else if (cpapmode == MODE_BILEVEL_FIXED) {
//                pressSettingChanID = CPAP_IPAP;
//                EventDataType ipap = day->settings_max(pressSettingChanID);
//                EventDataType epap = day->settings_min(CPAP_EPAP);
                html += tr("Your device used a constant %1-%2 %3 of air.")
                        .arg(day->validPressure(day->settings_min(CPAP_EPAP)))
                        .arg(day->validPressure(day->settings_max(CPAP_IPAP)))
                        .arg(schema::channel[CPAP_IPAP].units());
            } else if (cpapmode == MODE_BILEVEL_AUTO_FIXED_PS) {
                EventDataType ipap = day->percentile(pressChanID, perc/100.0);
                EventDataType epap = day->percentile(epapDataChanID, perc/100.0);
                html += tr("Your device was under %1-%2 %3 for %4% of the time.")
                        .arg(epap)
                        .arg(ipap)
                        .arg(schema::channel[pressChanID].units())
                        .arg(perc);
            } else if (cpapmode == MODE_ASV){
                EventDataType ipap = day->percentile(pressChanID, perc/100.0);
                EventDataType epap = qRound(10.0*day->settings_wavg(CPAP_EPAP))/10.0;
                html += tr("Your EPAP pressure fixed at %1 %2.")
                        .arg(epap)
                        .arg(schema::channel[epapDataChanID].units())+"<br/>";
                html += tr("Your IPAP pressure was under %1 %2 for %3% of the time.")
                        .arg(ipap)
                        .arg(schema::channel[pressChanID].units())
                        .arg(perc);
            } else if (cpapmode == MODE_AVAPS){
                EventDataType ipap = day->percentile(pressChanID, perc/100.0);
                // iVAPS: EPAP is fixed when AutoEPAP is off, or a min/max range when it is on.
                if (day->settingExists(CPAP_EPAPHi)) {      // AutoEPAP on
                    EventDataType epaplo = qRound(10.0*day->settings_min(CPAP_EPAPLo))/10.0;
                    EventDataType epaphi = qRound(10.0*day->settings_max(CPAP_EPAPHi))/10.0;
                    html += tr("Your EPAP pressure ranged from %1 to %2 %3.")
                            .arg(epaplo)
                            .arg(epaphi)
                            .arg(schema::channel[epapDataChanID].units())+"<br/>";
                } else {                                    // AutoEPAP off
                    EventDataType epap = qRound(10.0*day->settings_wavg(CPAP_EPAP))/10.0;
                    html += tr("Your EPAP pressure fixed at %1 %2.")
                            .arg(epap)
                            .arg(schema::channel[epapDataChanID].units())+"<br/>";
                }
                html += tr("Your IPAP pressure was under %1 %2 for %3% of the time.")
                        .arg(ipap)
                        .arg(schema::channel[pressChanID].units())
                        .arg(perc);
            } else if (cpapmode == MODE_ASV_VARIABLE_EPAP || cpapmode == MODE_BILEVEL_AUTO_VARIABLE_PS){
                EventDataType ipap = day->percentile(pressChanID, perc/100.0);
                EventDataType epap = day->percentile(epapDataChanID, perc/100.0);

                html += tr("Your EPAP pressure was under %1 %2 for %3% of the time.").arg(epap).arg(schema::channel[epapDataChanID].units()).arg(perc)+"<br/>";
                html += tr("Your IPAP pressure was under %1 %2 for %3% of the time.").arg(ipap).arg(schema::channel[pressChanID].units()).arg(perc);
            } else if (cpapmode == MODE_TRILEVEL_AUTO_VARIABLE_PDIFF){
                EventDataType ipap = day->percentile(pressChanID, perc/100.0);
                EventDataType eepap = day->percentile(epapDataChanID, perc/100.0);

                html += tr("Your EEPAP pressure was under %1 %2 for %3% of the time.").arg(eepap).arg(schema::channel[epapDataChanID].units()).arg(perc)+"<br/>";
                html += tr("Your IPAP pressure was under %1 %2 for %3% of the time.").arg(ipap).arg(schema::channel[pressChanID].units()).arg(perc);
            }
            html += "<br/>";

            //EventDataType lat = day->timeAboveThreshold(CPAP_Leak, p_profile->cpap->leakRedline())/ 60.0;
            //EventDataType leaks = 1.0/hours * lat;

            EventDataType leak = day->wavg(CPAP_Leak);
            EventDataType leakdays = p_profile->calcWavg(CPAP_Leak, MT_CPAP, starttime, endtime);

            if ((leak < leakdays) && ((leakdays - leak) >= 0.1)) {
                comp = under;
            } else if ((leak > leakdays) && ((leak - leakdays) >= 0.1)) {
                comp = over;
            } else if ((fabs(leak - leakdays) >= 0.01) ) {
                comp = close;
            } else {
                comp = equal;
            }

            html += tr("Your average leaks were %1 %2, which is %3 your %4 day average of %5.").arg(leak,0,'f',2).arg(schema::channel[CPAP_Leak].units()).arg(comp).arg(averagedays).arg(leakdays,0,'f',2);

            html += "<br/>";


        } else {
            html += "<p>"+tr("No CPAP data has been imported yet.")+"</p>";
        }
    }

    html += "</body></html>";
    return html;
}


// The night's oximetry in figures, under the date of the most recent oximetry: OSCAR's
// analysis when the night has one (the figures the Daily view and the Overview show),
// else the classic SpO2 drop count, labelled as such.
static QString oximetryNightHtml(const OximetryNight &n)
{
    if (!n.valid) return QString();
    const int minutes = qRound(n.hours * 60);
    const QString recorded = n.spotChecks
        ? QObject::tr("recorded %1 h %2 min").arg(minutes / 60).arg(minutes % 60)
        : QObject::tr("SpO2 recorded %1 h %2 min").arg(minutes / 60).arg(minutes % 60);
    QString html = "<p>" + (n.device.isEmpty() ? recorded : n.device.toHtmlEscaped() + ", " + recorded) + "<br/>";
    if (n.spotChecks) {
        html += QObject::tr("%1 SpO2 spot checks, %2 pulse readings.").arg(n.spotSpo2).arg(n.spotPulse);
    } else {
        if (n.spo2Avg > 0) {
            html += QObject::tr("Average SpO2 %1%, lowest %2%.").arg(n.spo2Avg, 0, 'f', 1).arg(n.spo2Min, 0, 'f', 0) + "<br/>";
            html += QObject::tr("Below 90%: %1% of the time (%2 min).").arg(n.percentBelow90, 0, 'f', 1).arg(qRound(n.minutesBelow90)) + "<br/>";
            if (n.fromAnalysis) {
                html += QObject::tr("ODI 3%: %1 per hour (%2 desaturations).").arg(n.desaturations / n.hours, 0, 'f', 1).arg(n.desaturations) + "<br/>";
                if (n.zones > 0) {
                    html += QObject::tr("Problem zones: %1, %2 min.").arg(n.zones).arg(qRound(n.zoneMinutes)) + "<br/>";
                }
            } else {
                html += QObject::tr("SpO2 drops (classic method, %1% below baseline for %2 s or longer): %3, %4 per hour.")
                            .arg(n.dropPercent).arg(n.dropSeconds).arg(n.desaturations).arg(n.desaturations / n.hours, 0, 'f', 1) + "<br/>";
            }
        }
        if (n.pulseAvg > 0) {
            html += QObject::tr("Pulse averaged %1, from %2 to %3 bpm.").arg(n.pulseAvg, 0, 'f', 0).arg(n.pulseMin, 0, 'f', 0).arg(n.pulseMax, 0, 'f', 0);
        }
    }
    return html + "</p>";
}

QString Welcome::GenerateOxiHTML()
{
    auto oximeters = p_profile->GetMachines(MT_OXIMETER);

    bool haveoximeterdata = false;
    MachineType oxiSourceType = MT_OXIMETER;

    for (auto & mach : oximeters) {
        int daysize = mach->day.size();
        if (daysize > 0) {
            haveoximeterdata = true;
            break;
        }
    }

    // If no dedicated oximeter machine, oximetry data may live in CPAP sessions
    // (some CPAP devices have built-in SpO2/pulse channels).
    ChannelID spo2id  = schema::channel["SPO2"].id();
    ChannelID pulseid = schema::channel["Pulse"].id();
    if (!haveoximeterdata) {
        if ((spo2id  != NoChannel && p_profile->channelAvailable(spo2id))
                || (pulseid != NoChannel && p_profile->channelAvailable(pulseid))) {
            haveoximeterdata = true;
            oxiSourceType = MT_CPAP;
        }
    }

    QString html = QString("<html><head>")+
//    "</head>"
    "<style type='text/css'>"
    "p,a,td,body { font-family: '"+QApplication::font().family()+"'; }"
    "p,a,td,body { font-size: "+QString::number(QApplication::font().pointSize() + 2)+"px; }"
    "body { color: black; }"
    "</style>"
    "</head>"

    "<body leftmargin=5 topmargin=5 rightmargin=5 bottommargin=5 valign=center align=center>";
    html += "<font size='+0'>" ;

    if (haveoximeterdata) {
        QDate oxidate;
        if (oxiSourceType == MT_OXIMETER) {
            oxidate = p_profile->LastDay(MT_OXIMETER);
        } else {
            // Walk back from the last CPAP day to find one with oximetry channel data.
            QDate first = p_profile->FirstDay(MT_CPAP);
            QDate d = p_profile->LastDay(MT_CPAP);
            while (d.isValid() && first.isValid() && d >= first) {
                Day * day = p_profile->GetDay(d, MT_CPAP);
                if (day && ((spo2id != NoChannel && day->channelHasData(spo2id))
                            || (pulseid != NoChannel && day->channelHasData(pulseid)))) {
                    oxidate = d;
                    break;
                }
                d = d.addDays(-1);
            }
            if (!oxidate.isValid()) oxidate = p_profile->LastDay(MT_CPAP);
        }
        int daysto = oxidate.daysTo(QDate::currentDate());

        html += "<p>"+QObject::tr("Most recent Oximetry data: <a onclick='alert(\"daily=%2\");'>%1</a> ").arg(oxidate.toString(QLocale::system().dateFormat(QLocale::LongFormat))).arg(oxidate.toString(Qt::ISODate));
        if (daysto == 1) html += QObject::tr("(last night)");
        else if (daysto == 2) html += QObject::tr("(1 day ago)");
        else html += QObject::tr("(%2 days ago)").arg(oxidate.daysTo(QDate::currentDate()));
        html+="</p>";
        OximetryNight night;
        analysis::AnalysisService *service = mainwin ? mainwin->analysisService() : nullptr;
        if (service && service->params().enabled) night = summarizeOximetry(service->row(oxidate));
        if (!night.valid) night = summarizeOximetry(p_profile->GetDay(oxidate, oxiSourceType), oxiSourceType);
        html += oximetryNightHtml(night);
        ui->oxiIcon->setVisible(true);
        ui->oxiInfoFrame->setVisible(true);
    } else {
        html += "<p>"+QObject::tr("No oximetry data has been imported yet.")+"</p>";
        ui->oxiIcon->setVisible(false);
        ui->oxiInfoFrame->setVisible(false);
    }

    html += "</body></html>";
    return html;
}

