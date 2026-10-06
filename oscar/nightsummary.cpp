/* Start screen: the last night in figures
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "nightsummary.h"
#include "helptips.h"

#include <QCoreApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/machine_common.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/analysis/analysis_service.h"

namespace {

QString tr(const char *text, const char *disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate("NightSummary", text, disambiguation, n);
}

QString num(double v, int decimals = 1)
{
    return QString::number(v, 'f', decimals);
}

// A pressure as the devices set it: 10, 9.8.
QString pressureText(double v)
{
    return QString::number(std::round(v * 10) / 10);
}

QString duration(double hours)
{
    const int minutes = int(std::lround(hours * 60));
    return QCoreApplication::translate("NightSummary", "%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

// The pressure tile's note: what the figure is, then how long the APAP sat at its limit.
QString pressureTileNote(const NightSummary &s)
{
    const QString atMax = s.pressureMaxNote();
    return atMax.isEmpty() ? s.pressureNote : s.pressureNote + QStringLiteral("; ") + atMax;
}

// A figure with its unit in smaller type, so that the number reads first and the tile
// keeps to one line: "7 h 12 min", "9.8 cmH2O".
QString figure(const QString &number, const QString &unit)
{
    if (unit.isEmpty()) return number.toHtmlEscaped();
    return number.toHtmlEscaped() + QStringLiteral("<span style='font-size:60%; font-weight:normal'>&nbsp;")
         + unit.toHtmlEscaped() + QStringLiteral("</span>");
}

QString usageFigure(double hours)
{
    const int minutes = int(std::lround(hours * 60));
    return figure(QString::number(minutes / 60), QCoreApplication::translate("NightSummary", "h"))
         + QStringLiteral(" ") + figure(QString::number(minutes % 60), QCoreApplication::translate("NightSummary", "min"));
}

QString offsetText(qint64 ms)
{
    const qint64 a = ms < 0 ? -ms : ms;
    if (a >= 60000) return QCoreApplication::translate("NightSummary", "%1 min").arg(std::lround(a / 60000.0));
    return QCoreApplication::translate("NightSummary", "%1 s").arg(std::lround(a / 1000.0));
}

// The night's pressure in one figure, with what the figure is.
void pressureFigures(Day *day, QString &value, QString &units, QString &note)
{
    const ChannelID press = day->getPressureChannelID();
    if (press == NoChannel) return;
    const double perc = p_profile->general->prefCalcPercentile();
    units = schema::channel[press].units();
    ChannelID epapData = CPAP_EPAP;
    if (day->channelHasData(CPAP_EPAPSet)) epapData = CPAP_EPAPSet;
    if (day->channelHasData(CPAP_EEPAP)) epapData = CPAP_EEPAP;
    const QString under = QCoreApplication::translate("NightSummary", "%1% of the time under").arg(perc);

    switch (CPAPMode(int(day->settings_max(CPAP_Mode)))) {
    case MODE_CPAP:
        value = pressureText(day->settings_max(CPAP_Pressure));
        note = QCoreApplication::translate("NightSummary", "constant");
        return;
    case MODE_APAP:
        value = pressureText(day->percentile(press, perc / 100.0));
        note = under;
        return;
    case MODE_BILEVEL_FIXED:
        value = pressureText(day->settings_min(CPAP_EPAP)) + QStringLiteral(" / ")
              + pressureText(day->settings_max(CPAP_IPAP));
        note = QCoreApplication::translate("NightSummary", "EPAP / IPAP, constant");
        return;
    case MODE_ASV:
        value = pressureText(day->settings_wavg(CPAP_EPAP)) + QStringLiteral(" / ")
              + pressureText(day->percentile(press, perc / 100.0));
        note = QCoreApplication::translate("NightSummary", "EPAP fixed / IPAP %1").arg(under);
        return;
    case MODE_UNKNOWN:
        value = pressureText(day->percentile(press, perc / 100.0));
        note = under;
        return;
    default:   // the bilevel and ASV modes with a varying EPAP, iVAPS, trilevel
        value = pressureText(day->percentile(epapData, perc / 100.0)) + QStringLiteral(" / ")
              + pressureText(day->percentile(press, perc / 100.0));
        note = (epapData == CPAP_EEPAP ? QCoreApplication::translate("NightSummary", "EEPAP / IPAP, %1")
                                       : QCoreApplication::translate("NightSummary", "EPAP / IPAP, %1")).arg(under);
        return;
    }
}

const char *const kTileStyle =
    "#nsTile { background-color: white; border: 1px solid #d9d9d9; border-radius: 8px; }"
    "#nsTrends { background-color: white; border: 1px solid #d9d9d9; border-radius: 8px; }"
    "#nsActions { background-color: #fff6e0; border: 1px solid #ecd49a; border-radius: 8px; padding: 6px 10px; color: #3d3000; }"
    "#nsTile QLabel, #nsTrends QLabel { background: transparent; }"
    "#nsCaption { color: #5f5f5f; }"
    "#nsValue { color: #1f1f1f; }"
    "#nsNote { color: #4d4d4d; }"
    "#nsTitle { color: #1f1f1f; }"
    "#nsConcerns { color: #3d3000; }";

} // namespace

// --- NightSummary -----------------------------------------------------------------------

NightSummary::Level NightSummary::usageLevel() const
{
    if (!hasCpap) return Unknown;
    return hours >= complianceHours ? Good : Attention;
}

NightSummary::Level NightSummary::ahiLevel() const
{
    if (!hasCpap || hours <= 0) return Unknown;
    return ahi < kAhiTarget ? Good : Attention;
}

NightSummary::Level NightSummary::leakLevel() const
{
    if (!hasLeak || leakRedline <= 0) return Unknown;
    return leak < leakRedline ? Good : Attention;
}

QString NightSummary::pressureMaxNote() const
{
    if (pressureMax <= 0 || hours <= 0) return QString();
    const int minutes = int(std::lround(secondsAtMax / 60));
    const QString time = minutes >= 60 ? duration(minutes / 60.0)
                                       : QCoreApplication::translate("NightSummary", "%1 min").arg(minutes);
    return QCoreApplication::translate("NightSummary", "at the maximum %1: %2 (%3%)")
        .arg(pressureText(pressureMax), time, num(100.0 * secondsAtMax / (hours * 3600)));
}

int NightSummary::compliantNights() const
{
    return int(std::count_if(usage.cbegin(), usage.cend(), [this](double h) { return h >= complianceHours; }));
}

double NightSummary::medianAhi() const
{
    QVector<double> v;
    for (double a : ahiTrend) {
        if (!std::isnan(a)) v.append(a);
    }
    if (v.isEmpty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(v.begin(), v.end());
    const int n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

QStringList NightSummary::concerns() const
{
    QStringList out;
    if (usageLevel() == Attention) {
        out << tr("Used for %1, less than your %2 h.").arg(duration(hours)).arg(complianceHours);
    }
    if (ahiLevel() == Attention) {
        out << tr("AHI %1, at or above %2.").arg(num(ahi)).arg(kAhiTarget);
    } else if (ahiLevel() == Good && hasAnalysisAhi && analysisAhi >= kAhiTarget) {
        out << tr("OSCAR's analysis counts an AHI of %1, more than the device's %2.").arg(num(analysisAhi), num(ahi));
    }
    if (leakLevel() == Attention) {
        out << tr("Average leak %1 %2, at or above your red line of %3.").arg(num(leak), leakUnits).arg(leakRedline);
    }
    if (spo2Level() == Attention) {
        if (oxi.percentBelow90 >= kT90Target) {
            out << tr("SpO2 below 90% for %1% of the time, at or above %2%.").arg(num(oxi.percentBelow90)).arg(kT90Target);
        }
        if (oxi.fromAnalysis && oxi.desaturations / oxi.hours >= kOdiTarget) {
            out << tr("ODI 3% %1 per hour, at or above %2.").arg(num(oxi.desaturations / oxi.hours)).arg(kOdiTarget);
        }
    }
    return out;
}

QStringList NightSummary::actions() const
{
    QStringList out;
    const QString link = QStringLiteral(" <a href='%1'>%2</a>");
    if (outdatedAnalysis > 0) {
        out << tr("%n night(s) waiting for OSCAR's analysis.", nullptr, outdatedAnalysis)
               + link.arg(QStringLiteral("analysis=recalculate"), tr("Analyse now"));
    }
    if (hasOffsetHint) {
        out << tr("The oximeter's clock may be off by about %1 that night.").arg(offsetText(offsetHintMs))
               + link.arg(QStringLiteral("daily=") + date.toString(Qt::ISODate), tr("Open the day to align it"));
    }
    if (hasCpap && daysSinceData >= 3) {
        out << tr("The latest CPAP data is %n day(s) old.", nullptr, daysSinceData)
               + link.arg(QStringLiteral("import=cpap"), tr("Import"));
    }
    return out;
}

NightSummary buildNightSummary(Profile *profile, analysis::AnalysisService *service, const QDate &today)
{
    if (!profile) return NightSummary();
    const QDate cpapDate = profile->LastDay(MT_CPAP);
    const QDate oxiDate = profile->LastDay(MT_OXIMETER);
    const QDate latest = cpapDate.isValid() && (!oxiDate.isValid() || cpapDate >= oxiDate) ? cpapDate : oxiDate;
    if (!latest.isValid()) return NightSummary();
    return buildNightSummaryFor(profile, service, latest, today);
}

NightSummary buildNightSummaryFor(Profile *profile, analysis::AnalysisService *service, const QDate &date,
                                  const QDate &today, bool withTrends)
{
    NightSummary s;
    if (!profile || !date.isValid()) return s;
    const QDate oxiDate = profile->LastDay(MT_OXIMETER);
    s.date = date;
    s.daysSinceData = int(s.date.daysTo(today));
    s.complianceHours = profile->cpap->complianceHours();
    const bool analysisOn = service && service->params().enabled;
    const AnalysisDailyData row = analysisOn ? service->row(s.date) : AnalysisDailyData();

    Day *day = profile->GetDay(s.date, MT_CPAP);
    Machine *cpap = day ? day->machine(MT_CPAP) : nullptr;
    if (cpap && day->hours(MT_CPAP) > 0) {
        s.hasCpap = true;
        s.cpapDevice = (cpap->brand() + QLatin1Char(' ') + cpap->model()).trimmed();
        s.cpapPixmap = cpap->getPixmapPath();
        s.hours = day->hours(MT_CPAP);
        s.ahi = day->calcAHI();
        s.correctedByHand = day->hasManualScoring();
        s.deviceAhi = day->deviceAHI();
        if (row.id && row.hasFlow && row.flowSeconds > 0) {
            s.hasAnalysisAhi = true;
            const int events = row.nObstructiveApnea + row.nCentralApnea + row.nApnea
                             + row.nObstructiveHypopnea + row.nCentralHypopnea + row.nHypopnea;
            s.analysisAhi = events / (row.flowSeconds / 3600.0);
        }
        s.takeFlowLimitation(row);
        if (day->channelHasData(CPAP_Leak)) {
            s.hasLeak = true;
            s.leak = day->wavg(CPAP_Leak);
            s.leakRedline = profile->cpap->leakRedline();
            s.leakUnits = schema::channel[CPAP_Leak].units();
        }
        pressureFigures(day, s.pressure, s.pressureUnits, s.pressureNote);
        if (CPAPMode(int(day->settings_max(CPAP_Mode))) == MODE_APAP && day->getPressureChannelID() != NoChannel) {
            s.pressureMax = day->settings_max(CPAP_PressureMax);
            // the stored values carry a gain, so 14 reads 13.99998: allow a hair below the limit;
            // timeAboveThreshold() counts minutes
            if (s.pressureMax > 0)
                s.secondsAtMax = 60.0 * day->timeAboveThreshold(day->getPressureChannelID(), s.pressureMax - 0.05);
        }
    }

    // That night's oximetry: the analysis' figures where it ran, else the classic count.
    s.oxi = summarizeOximetry(row);
    if (!s.oxi.valid) {
        Day *oxiDay = profile->GetDay(s.date, MT_OXIMETER);
        if (oxiDay && oxiDay->machine(MT_OXIMETER)) {
            s.oxi = summarizeOximetry(oxiDay, MT_OXIMETER);
        } else if (day && (day->channelHasData(OXI_SPO2) || day->channelHasData(OXI_Pulse))) {
            s.oxi = summarizeOximetry(day, MT_CPAP);
        }
    }
    if (!s.oxi.valid && oxiDate.isValid() && oxiDate != s.date) s.lastOximetry = oxiDate;
    s.hasOffsetHint = row.id && row.hasOffsetHint;
    s.offsetHintMs = row.oxiOffsetHintMs;

    // The nights before, for the trends: no further back than the first CPAP night.
    if (withTrends && s.hasCpap) {
        QDate from = s.date.addDays(1 - NightSummary::kTrendNights);
        const QDate first = profile->FirstDay(MT_CPAP);
        if (first.isValid() && first > from) from = first;
        for (QDate d = from; d <= s.date; d = d.addDays(1)) {
            Day *dd = profile->GetDay(d, MT_CPAP);
            const double h = dd && dd->machine(MT_CPAP) ? dd->hours(MT_CPAP) : 0;
            s.usage.append(h);
            s.ahiTrend.append(h > 0 ? dd->calcAHI() : std::numeric_limits<double>::quiet_NaN());
        }
    }
    s.outdatedAnalysis = withTrends && analysisOn ? service->outdatedCount() : 0;
    return s;
}

// --- NightTrend --------------------------------------------------------------------------

NightTrend::NightTrend(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void NightTrend::setValues(const QVector<double> &values, double target, bool aboveIsGood, double minScale)
{
    m_values = values;
    m_target = target;
    m_aboveIsGood = aboveIsGood;
    m_minScale = minScale;
    update();
}

QSize NightTrend::sizeHint() const
{
    return QSize(240, 52);
}

void NightTrend::paintEvent(QPaintEvent *)
{
    if (m_values.isEmpty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 4, -1, -2);
    double top = m_minScale;
    for (double v : m_values) {
        if (!std::isnan(v)) top = std::max(top, v);
    }
    top *= 1.1;
    const int n = m_values.size();
    const double slot = r.width() / NightSummary::kTrendNights;      // a month always spans the same width
    const double bar = std::max(2.0, slot * 0.7);
    const double x0 = r.right() - n * slot;
    const QColor good(0x4a, 0x7f, 0xb5), attention = NightSummaryView::levelColor(NightSummary::Attention);
    for (int i = 0; i < n; ++i) {
        const double v = m_values[i];
        const double x = x0 + i * slot + (slot - bar) / 2;
        if (std::isnan(v) || v <= 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xc8, 0xc8, 0xc8));
            p.drawEllipse(QPointF(x + bar / 2, r.bottom() - 1.5), 1.5, 1.5);
            continue;
        }
        const bool reached = v >= m_target;
        p.setPen(Qt::NoPen);
        p.setBrush(reached == m_aboveIsGood ? good : attention);
        const double h = std::max(1.5, r.height() * v / top);
        p.drawRect(QRectF(x, r.bottom() - h, bar, h));
    }
    const double y = r.bottom() - r.height() * m_target / top;
    QPen pen(QColor(0x40, 0x40, 0x40), 1, Qt::DashLine);
    p.setPen(pen);
    p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
}

// --- NightSummaryView --------------------------------------------------------------------

NightSummaryView::NightSummaryView(QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet(QString::fromLatin1(kTileStyle));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(10);

    auto *header = new QHBoxLayout;
    m_icon = new QLabel(this);
    m_icon->setFixedSize(72, 72);
    m_icon->setAlignment(Qt::AlignCenter);
    header->addWidget(m_icon, 0, Qt::AlignTop);
    auto *headText = new QVBoxLayout;
    headText->setSpacing(4);
    m_title = richLabel(QString(), QStringLiteral("nsTitle"));
    QFont tf = m_title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.15);
    m_title->setFont(tf);
    m_verdict = richLabel(QString(), QStringLiteral("nsVerdict"));
    QFont vf = m_verdict->font();
    vf.setPointSizeF(vf.pointSizeF() * 1.4);
    vf.setBold(true);
    m_verdict->setFont(vf);
    m_concerns = richLabel(QString(), QStringLiteral("nsConcerns"));
    headText->addWidget(m_title);
    headText->addWidget(m_verdict);
    headText->addWidget(m_concerns);
    header->addLayout(headText, 1);
    layout->addLayout(header);

    m_tiles = new QGridLayout;
    m_tiles->setHorizontalSpacing(8);
    layout->addLayout(m_tiles);

    m_trendsFrame = new QFrame(this);
    m_trendsFrame->setObjectName(QStringLiteral("nsTrends"));
    auto *trends = new QGridLayout(m_trendsFrame);
    trends->setContentsMargins(12, 8, 12, 8);
    trends->setHorizontalSpacing(24);
    m_usageCaption = richLabel(QString(), QStringLiteral("nsCaption"));
    m_ahiCaption = richLabel(QString(), QStringLiteral("nsCaption"));
    HelpTips::attach(m_usageCaption, QStringLiteral("compliance"));
    HelpTips::attach(m_ahiCaption, QStringLiteral("median"));
    m_usageTrend = new NightTrend(m_trendsFrame);
    m_ahiTrend = new NightTrend(m_trendsFrame);
    trends->addWidget(m_usageCaption, 0, 0);
    trends->addWidget(m_ahiCaption, 0, 1);
    trends->addWidget(m_usageTrend, 1, 0);
    trends->addWidget(m_ahiTrend, 1, 1);
    layout->addWidget(m_trendsFrame);

    m_actions = richLabel(QString(), QStringLiteral("nsActions"));
    layout->addWidget(m_actions);
    layout->addStretch(1);
}

QColor NightSummaryView::levelColor(NightSummary::Level level)
{
    switch (level) {
    case NightSummary::Good: return QColor(0x2e, 0x7d, 0x32);
    case NightSummary::Attention: return QColor(0xc2, 0x6a, 0x00);
    case NightSummary::Unknown: break;
    }
    return QColor(0xb0, 0xb0, 0xb0);
}

QString NightSummaryView::keyFiguresHtml(const NightSummary &s)
{
    struct Tile {
        QString caption, value, note;
        NightSummary::Level level;
    };
    QVector<Tile> tiles;
    if (s.hasCpap) {
        tiles.append({ tr("Usage"), duration(s.hours), tr("target %1 h or more").arg(s.complianceHours), s.usageLevel() });
        if (s.hasLeak) {
            tiles.append({ tr("Leak"), num(s.leak) + QLatin1Char(' ') + s.leakUnits,
                           s.leakRedline > 0 ? tr("average; red line %1").arg(s.leakRedline) : tr("average"), s.leakLevel() });
        }
        if (!s.pressure.isEmpty()) {
            tiles.append({ tr("Pressure"), s.pressure + QLatin1Char(' ') + s.pressureUnits, pressureTileNote(s), NightSummary::Unknown });
        }
    }
    if (s.oxi.valid && !s.oxi.spotChecks && s.oxi.spo2Avg > 0 && s.oxi.hours > 0) {
        const QString note = s.oxi.fromAnalysis
            ? tr("ODI 3%: %1 per hour").arg(num(s.oxi.desaturations / s.oxi.hours))
            : tr("SpO2 drops (classic): %1 per hour").arg(num(s.oxi.desaturations / s.oxi.hours));
        tiles.append({ tr("SpO2 below 90%"), num(s.oxi.percentBelow90) + QStringLiteral(" %"), note, s.spo2Level() });
    }
    if (tiles.isEmpty()) return QString();

    // QTextBrowser HTML: a coloured cell stands in for the tile's left border.
    QString html = QStringLiteral("<table width='100%' cellspacing=3 cellpadding=0 border=0>");
    for (int i = 0; i < tiles.size(); ++i) {
        const Tile &t = tiles[i];
        if (i % 2 == 0) html += QStringLiteral("<tr>");
        const QString color = levelColor(t.level).name();
        QString sign;
        if (t.level == NightSummary::Good) sign = QStringLiteral("<font color='%1'>&#x2713;</font> ").arg(color);
        else if (t.level == NightSummary::Attention) sign = QStringLiteral("<font color='%1'><b>!</b></font> ").arg(color);
        html += QStringLiteral("<td width='50%' valign=top><table width='100%' cellspacing=0 cellpadding=3 border=0 bgcolor='#ffffff'><tr>"
                               "<td width=4 bgcolor='%1'></td><td><font color='#5f5f5f'>%2</font><br/><b>%3</b><br/><small>%4%5</small></td>"
                               "</tr></table></td>")
                    .arg(color, t.caption.toHtmlEscaped(), t.value.toHtmlEscaped(), sign, t.note.toHtmlEscaped());
        if (i % 2 == 1) html += QStringLiteral("</tr>");
    }
    if (tiles.size() % 2) html += QStringLiteral("<td></td></tr>");
    return html + QStringLiteral("</table>");
}

QLabel *NightSummaryView::richLabel(const QString &text, const QString &objectName)
{
    auto *l = new QLabel(text, this);
    l->setObjectName(objectName);
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextBrowserInteraction);
    l->setOpenExternalLinks(false);
    // a "help:" term explains itself; other links go to the main window
    connect(l, &QLabel::linkActivated, this, [this](const QString &link) {
        const QString key = HelpTips::keyOf(QUrl(link));
        if (!key.isEmpty()) HelpTips::instance()->open(key);
        else emit linkActivated(link);
    });
    connect(l, &QLabel::linkHovered, this, [l](const QString &link) {
        // while a term is under the mouse, its own explanation wins over the tile's
        const QString key = HelpTips::keyOf(QUrl(link));
        l->setProperty("helpKey", key);
        HelpTips::instance()->hover(key);
    });
    return l;
}

void NightSummaryView::clearTiles()
{
    while (QLayoutItem *item = m_tiles->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

QFrame *NightSummaryView::addTile(int column, const QString &caption, const QString &value, const QString &note,
                                  NightSummary::Level level, const QString &tooltip)
{
    auto *tile = new QFrame(this);
    tile->setObjectName(QStringLiteral("nsTile"));
    if (level != NightSummary::Unknown) {
        // the level in colour along the left edge, and as a sign in the note for those who
        // can't tell the colours apart
        tile->setStyleSheet(QStringLiteral("#nsTile { border-left: 4px solid %1; }").arg(levelColor(level).name()));
    }
    tile->setToolTip(tooltip);
    auto *v = new QVBoxLayout(tile);
    v->setContentsMargins(12, 8, 12, 10);
    v->setSpacing(2);
    QLabel *c = richLabel(caption, QStringLiteral("nsCaption"));
    QLabel *val = richLabel(value, QStringLiteral("nsValue"));
    val->setWordWrap(false);
    QFont f = val->font();
    f.setPointSizeF(f.pointSizeF() * 1.7);
    f.setBold(true);
    val->setFont(f);
    QString sign;
    if (level == NightSummary::Good) sign = QStringLiteral("<span style='color:%1'>&#x2713;</span> ").arg(levelColor(level).name());
    if (level == NightSummary::Attention) sign = QStringLiteral("<span style='color:%1'><b>!</b></span> ").arg(levelColor(level).name());
    QLabel *n = richLabel(sign + note, QStringLiteral("nsNote"));
    v->addWidget(c);
    v->addWidget(val);
    v->addWidget(n);
    v->addStretch(1);
    m_tiles->addWidget(tile, 0, column);
    m_tiles->setColumnStretch(column, 1);
    return tile;
}

void NightSummaryView::setSummary(const NightSummary &s)
{
    clearTiles();
    for (int c = 0; c < 8; ++c) m_tiles->setColumnStretch(c, 0);

    // --- header: which night, from what, and the verdict
    QString when = s.daysSinceData == 1 ? tr("Last night")
                 : tr("Night of %1").arg(QLocale().toString(s.date, QLocale::ShortFormat));
    QString title = QStringLiteral("<b>%1</b> &middot; %2").arg(when, QLocale().toString(s.date, QLocale::LongFormat).toHtmlEscaped());
    const QString device = s.hasCpap ? s.cpapDevice : s.oxi.device;
    if (!device.isEmpty()) title += QStringLiteral(" &middot; ") + device.toHtmlEscaped();
    title += QStringLiteral(" &middot; <a href='daily=%1'>%2</a>").arg(s.date.toString(Qt::ISODate), tr("Details"));
    m_title->setText(title);
    QPixmap pm(s.cpapPixmap);
    m_icon->setPixmap(pm.isNull() ? QPixmap() : pm.scaled(m_icon->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_icon->setVisible(!pm.isNull());

    const QStringList concerns = s.concerns();
    if (!concerns.isEmpty()) {
        m_verdict->setText(QStringLiteral("<span style='color:%1'>%2</span>")
                               .arg(levelColor(NightSummary::Attention).name(), tr("Worth a look")));
        QStringList items;
        for (const QString &c : concerns) items << QStringLiteral("&bull; ") + c.toHtmlEscaped();
        m_concerns->setText(items.join(QStringLiteral("<br/>")));
    } else if (s.hasCpap) {
        m_verdict->setText(QStringLiteral("<span style='color:%1'>&#x2713; %2</span>")
                               .arg(levelColor(NightSummary::Good).name(), tr("Within your targets")));
        m_concerns->clear();
    } else {
        m_verdict->clear();
        m_concerns->clear();
    }
    m_verdict->setVisible(!m_verdict->text().isEmpty());
    m_concerns->setVisible(!m_concerns->text().isEmpty());

    // --- a tile per figure
    int col = 0;
    if (s.hasCpap) {
        HelpTips::attach(addTile(col++, tr("Usage"), usageFigure(s.hours),
                                 tr("target %1 h or more").arg(s.complianceHours), s.usageLevel()), QStringLiteral("usage"));
        QString ahiNote = tr("target under %1").arg(NightSummary::kAhiTarget);
        if (s.correctedByHand) {
            ahiNote += QStringLiteral("<br/>") + HelpTips::term(tr("corrected by hand (device %1)").arg(num(s.deviceAhi)), QStringLiteral("manual_scoring"));
        }
        if (s.hasAnalysisAhi) {
            ahiNote += QStringLiteral("<br/>") + HelpTips::term(tr("OSCAR's analysis: %1").arg(num(s.analysisAhi)), QStringLiteral("an_ahi"));
        }
        if (s.hasFlowLimitation) {
            const QString fl = QString::number(s.flPercent, 'f', 0), minutes = QString::number(s.flMinutes, 'f', 0);
            ahiNote += QStringLiteral("<br/>") + HelpTips::term(tr("flow limitation %1% (%2 min)").arg(fl, minutes), QStringLiteral("fl_time"));
            if (s.hasGlasgow) {
                ahiNote += QStringLiteral(" · ") + HelpTips::term(tr("Glasgow %1 / %2").arg(num(s.glasgow), num(s.glasgowAdapted)), QStringLiteral("glasgow"))
                         + (s.glasgowLessReliable ? QLatin1Char(' ') + tr("(less reliable on this device)") : QString());
            }
        }
        HelpTips::attach(addTile(col++, tr("AHI"), num(s.ahi), ahiNote, s.ahiLevel(),
                                 tr("Apneas and hypopneas per hour, as the device counted them.")), QStringLiteral("ahi"));
        if (s.hasLeak) {
            const QString note = s.leakRedline > 0 ? tr("average; red line %1").arg(s.leakRedline) : tr("average");
            HelpTips::attach(addTile(col++, tr("Leak"), figure(num(s.leak), s.leakUnits), note, s.leakLevel()), QStringLiteral("leak"));
        }
        if (!s.pressure.isEmpty()) {
            HelpTips::attach(addTile(col++, tr("Pressure"), figure(s.pressure, s.pressureUnits), pressureTileNote(s).toHtmlEscaped(),
                                     NightSummary::Unknown), QStringLiteral("p95"));
        }
    }
    if (s.oxi.valid && !s.oxi.spotChecks && s.oxi.spo2Avg > 0) {
        QString note = s.oxi.fromAnalysis
            ? tr("ODI 3%: %1 per hour").arg(num(s.oxi.desaturations / s.oxi.hours))
            : tr("SpO2 drops (classic): %1 per hour").arg(num(s.oxi.desaturations / s.oxi.hours));
        note += QStringLiteral("<br/>") + tr("lowest %1%, average %2%").arg(num(s.oxi.spo2Min, 0), num(s.oxi.spo2Avg));
        if (s.hasCpap && s.oxi.pulseAvg > 0) {   // no room for a pulse tile next to the CPAP ones
            note += QStringLiteral("<br/>") + tr("pulse %1 (%2 to %3)").arg(num(s.oxi.pulseAvg, 0), num(s.oxi.pulseMin, 0), num(s.oxi.pulseMax, 0));
        }
        HelpTips::attach(addTile(col++, tr("SpO2 below 90%"), figure(num(s.oxi.percentBelow90), QStringLiteral("%")),
                                 note, s.spo2Level(),
                                 tr("Share of the time with valid SpO2 readings spent below 90%: %1 min.").arg(qRound(s.oxi.minutesBelow90))),
                         QStringLiteral("t90"));
    } else if (s.lastOximetry.isValid()) {
        HelpTips::attach(addTile(col++, tr("SpO2"), QStringLiteral("&mdash;"),
                tr("not recorded this night; latest <a href='daily=%1'>%2</a>")
                    .arg(s.lastOximetry.toString(Qt::ISODate), QLocale().toString(s.lastOximetry, QLocale::ShortFormat)),
                NightSummary::Unknown), QStringLiteral("spo2"));
    }
    if (!s.hasCpap && s.oxi.valid && !s.oxi.spotChecks && s.oxi.pulseAvg > 0) {
        HelpTips::attach(addTile(col++, tr("Pulse"), figure(num(s.oxi.pulseAvg, 0), tr("bpm")),
                                 tr("from %1 to %2").arg(num(s.oxi.pulseMin, 0), num(s.oxi.pulseMax, 0)), NightSummary::Unknown),
                         QStringLiteral("pulse"));
    }

    // --- the nights before
    m_trendsFrame->setVisible(s.hasCpap && !s.usage.isEmpty());
    if (s.hasCpap && !s.usage.isEmpty()) {
        const int n = s.usage.size();
        m_usageCaption->setText(tr("Usage: %1 of %n night(s) with %2 h or more", nullptr, n)
                                     .arg(s.compliantNights()).arg(s.complianceHours));
        m_usageTrend->setValues(s.usage, s.complianceHours, true, s.complianceHours * 2);
        int withData = 0;
        for (double a : s.ahiTrend) withData += !std::isnan(a);
        const double median = s.medianAhi();
        m_ahiCaption->setText(std::isnan(median) ? tr("AHI")
                              : tr("AHI: median %1 over %n night(s)", nullptr, withData).arg(num(median)));
        m_ahiTrend->setValues(s.ahiTrend, NightSummary::kAhiTarget, false, NightSummary::kAhiTarget * 2);
    }

    const QStringList actions = s.actions();
    m_actions->setText(actions.join(QStringLiteral("<br/>")));
    m_actions->setVisible(!actions.isEmpty());
}

NightSummary::Level NightSummary::spo2Level() const
{
    if (!oxi.valid || oxi.spotChecks || oxi.spo2Avg <= 0 || oxi.hours <= 0) return Unknown;
    if (oxi.percentBelow90 >= kT90Target) return Attention;
    // The classic drops follow the profile's own thresholds, so only ODI 3 % is judged.
    if (oxi.fromAnalysis && oxi.desaturations / oxi.hours >= kOdiTarget) return Attention;
    return Good;
}

void NightSummary::takeFlowLimitation(const AnalysisDailyData &row)
{
    hasFlowLimitation = row.id && row.hasFlow && row.flowSeconds > 0 && row.flBreaths > 0;   // scored at 10 Hz or more
    if (hasFlowLimitation) {
        flPercent = 100.0 * row.flSeconds / row.flowSeconds;
        flMinutes = row.flSeconds / 60.0;
    }
    hasGlasgow = row.id && row.hasFlow && !row.glasgow.isEmpty() && !row.glasgowAdapted.isEmpty();
    if (hasGlasgow) {
        glasgow = row.glasgow.index();
        glasgowAdapted = row.glasgowAdapted.index();
        glasgowLessReliable = row.flowRateHz > 0 && row.flowRateHz < 20;
    }
}
