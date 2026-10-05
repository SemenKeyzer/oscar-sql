# Единый PDF-отчёт «Создать PDF-отчёт» — дизайн

- Дата: 2026-10-05
- Ветка: `master` форка `SemenKeyzer/oscar-sql`
- Статус: дизайн согласован с владельцем в чате 2026-10-05; ждёт просмотра спецификации
- Источник: письмо врача (сомнолога) владельцу, пункт 1, с макетом окна «Create PDF Report»
- Опирается на: `specs/2026-10-03-doctor-report-design.md` (одностраничный отчёт),
  `specs/2026-10-02-settings-comparison-design.md`, наборы графиков «Сводки» (`OverviewPresets`)

> Этот файл — рабочая спецификация форка. В Merge Request в upstream он не включается.

## 1. Цель и контекст

Врач пишет: PDF сейчас делается через «Файл → Печать → виртуальный принтер», отдельно для
«Дня», «Сводки» и «Статистики» — три файла. Нужно одно окно, где выбирается, что включить, и один
файл на выходе. Макет врача: галочки Daily (последний день / 3 / 7 / период), Overview,
Statistics (изменения настроек, оксиметрия, информация об аппарате), личные данные, серийный
номер, «Create Report» → выбор файла.

Владелец согласился с улучшениями к макету: один период на весь отчёт, первая страница —
одностраничный отчёт для врача, готовые наборы, только ночи с данными, запоминание выбора,
оценка числа страниц, один пункт меню вместо двух. Ночь печатается так, как сейчас печатает
вкладка «День». Набор графиков «Сводки» выбирается в окне.

### Что уже есть
- `Report::PrintReport(gGraphView*, name, date)` (`reports.cpp`, ~600 строк): создаёт `QPrinter`,
  показывает `QPrintDialog`, рисует шапку (заголовок, данные пользователя при
  `AppSetting->showPersonalData()`, цифры дня, круговая диаграмма) и видимые графики по 6 на
  страницу через `gGraph::renderPixmap`. Вызывается из «Файл → Печать» для «Дня» и «Сводки».
- `Statistics::GenerateHTML()` строит HTML по `general->statReportMode()` и датам из профиля;
  `printReport()` печатает его через `QTextDocument`. Личные данные — `AppSetting->showPersonalData()`
  (`statistics.cpp:1082`), серийные номера — `AppSetting->includeSerial()`.
- `DoctorReportPage::html()/chart()/writePdf()` и `Statistics::doctorReport(from, to)` —
  одностраничный отчёт; меню «Файл → Отчёт для врача (PDF)…» и `DoctorReportDialog`.
- `Overview::showPreset()`, `OverviewPresets` — наборы графиков «Сводки».
- `Daily::LoadDate(date)`, `Overview::setRange(start, end)`.

## 2. Что видит пользователь

### Меню
«Файл → Создать PDF-отчёт…» заменяет «Отчёт для врача (PDF)…» (действие `actionDoctor_Report`
переименовывается; `DoctorReportDialog` уходит, его логика — в новое окно). Доступно при
открытом профиле.

### Окно «Создать PDF-отчёт»
1. **Период** (один на весь отчёт): «последние 7 ночей / 30 ночей / 90 ночей / с … по …».
   «Последние N ночей» отсчитываются от последней ночи с данными CPAP. По умолчанию 30 ночей.
2. **Готовые наборы** — кнопки «Кратко для врача», «Подробно», «Всё»; ставят галочки ниже:

   | Набор | Первая страница | День | Сводка | Статистика |
   |---|---|---|---|---|
   | Кратко для врача | да | — | — | — |
   | Подробно | да | последние 7 ночей | «Терапия» | да, без информации об аппарате |
   | Всё | да | все ночи периода | «Все (как у вас)» | да, все три подраздела |

3. **Разделы:**
   - ☑ **Первая страница** — одностраничный отчёт для врача за период.
   - ☐ **День** — переключатель «последняя ночь / последние 3 / последние 7 / все ночи периода (N)».
     Считаются только ночи с данными CPAP внутри периода, от конца периода. Каждая ночь — как
     печать вкладки «День» (шапка с цифрами ночи, все видимые на вкладке графики, 1–3 страницы).
   - ☐ **Сводка** — графики за период; выпадающий список набора: «Все (как у вас)», «Терапия»,
     «Маска и утечки», «Кислород», «Анализ». По умолчанию «Терапия».
   - ☐ **Статистика** — таблица за период; подгалочки «Изменения настроек аппарата»,
     «Оксиметрия (если есть данные)», «Информация об аппарате».
4. **Личные данные:** ☑ «Имя и дата рождения», ☐ «Серийные номера».
5. **Низ окна:** «примерно N страниц» (пересчитывается при каждом изменении), «Отмена»,
   «Создать отчёт…» (недоступна, если не выбран ни один раздел или «с» позже «по»).

### Создание
1. Выбор файла в «Документах», имя по умолчанию
   `Отчёт CPAP <профиль> <с dd.MM>–<по dd.MM.yyyy>.pdf`.
2. Окно хода работы: «Первая страница…», «Ночь 3 из 7…», «Сводка…», «Статистика…», с кнопкой
   «Отмена». При отмене недописанный файл удаляется.
3. В конце «Отчёт сохранён» с кнопкой «Открыть».
4. Выбор в окне запоминается в профиле и предлагается в следующий раз.
5. После отчёта вкладка «День» показывает тот же день, что до отчёта; «Сводка» — тот же диапазон
   дат и тот же набор графиков.

### Ошибки
- Нет ночей с данными CPAP в периоде — сообщение, файл не создаётся, окно остаётся.
- Не удалось записать файл — сообщение с причиной, окно остаётся.
- Во вкладке «День» не видно ни одного графика — раздел «День» печатает только шапки ночей и
  предупреждает об этом в окне хода работы (не прерывая отчёт).

## 3. Устройство

### `oscar/pdfreportoptions.{h,cpp}` — параметры (без интерфейса)
```cpp
struct PdfReportOptions {
    enum PeriodKind { Last7, Last30, Last90, Custom };
    enum NightsKind { LastNight, Last3, Last7Nights, AllNights };
    enum Preset { Brief, Detailed, Everything };
    PeriodKind period = Last30;
    QDate from, to;                       // for Custom
    bool summary = true;
    bool daily = false;  NightsKind nights = Last7Nights;
    bool overview = false; OverviewPresets::Preset overviewPreset = OverviewPresets::Therapy;
    bool statistics = false; bool statsSettings = true, statsOximetry = true, statsDevices = false;
    bool personalData = true; bool serialNumbers = false;
};
```
- `void apply(PdfReportOptions &, Preset)` — таблица наборов §2.
- `QPair<QDate, QDate> range(const PdfReportOptions &, const QDate &lastNight)` — период.
- `QList<QDate> nightsToPrint(const PdfReportOptions &, const QList<QDate> &cpapNights)` —
  ночи «Дня» внутри периода, от конца.
- `int estimatePages(const PdfReportOptions &, int nights, int dailyGraphs, int overviewGraphs,
  int statisticsPages)` — сумма: 1 за первую страницу; на ночь `1 + ceil(dailyGraphs / 6)`
  без первой полной (шапка занимает место ~1 графика); `ceil(overviewGraphs / 6)` за «Сводку»;
  `statisticsPages` за «Статистику».
- `QVariantMap toMap() / static PdfReportOptions fromMap(const QVariantMap &)` — запоминание;
  ключ профиля `STR_US_PdfReportOptions = "PdfReportOptions"`.

### `Report` — рисование отдельно от выбора принтера
`Report::PrintReport` делится на:
- `static bool paint(QPainter &painter, QPrinter &printer, gGraphView *gv, const QString &name,
  const QDate &date, const PrintTarget &target)` — всё рисование страниц (сегодняшняя логика
  после `QPainter painter(printer)`), начиная с текущей страницы; `PrintTarget` — личные данные
  (bool), закладки (bool), окно хода работы (может быть null).
- `PrintReport()` — как раньше: принтер, `QPrintDialog`, вопрос о закладках, затем `paint()`.
  Поведение «Файл → Печать» не меняется.

### `oscar/htmlpages.{h,cpp}` — HTML на страницы
`int paintHtmlPages(QPainter &, QPrinter &, const QString &html, const QFont &, const QHash<QUrl,
QImage> &resources, bool startOnNewPage)` — раскладывает `QTextDocument` по размеру страницы
принтера (в единицах раскладки с учётом `qt_defaultDpiY()`, как в `DoctorReportPage::writePdf`),
рисует страницы по одной (`drawContents` с отсечением и сдвигом), между ними `printer.newPage()`;
возвращает число страниц. `DoctorReportPage::writePdf` переходит на неё.

### `Statistics` — статистика за явный период
`QString Statistics::periodHtml(const QDate &from, const QDate &to, const StatisticsSections &s)`
с `StatisticsSections { bool settingsChanges, oximetry, devices, personalData, serialNumbers; }`.
Строит то же, что режим «Период» (`STAT_MODE_RANGE`), но с явными датами и без чтения/записи
`general->statReport*`; ряды `MT_OXIMETER` пропускаются при `!oximetry`; `GenerateRXChanges` —
только периоды, пересекающие `[from, to]`; `GenerateMachineList` — при `devices`. Личные данные и
серийные номера — из `StatisticsSections`, не из `AppSetting`. `GenerateHTML()` (экран) не
меняется.

`Statistics::doctorReport(from, to)` получает параметр `personalData`/`serialNumbers` так же.

### `oscar/pdfreportwriter.{h,cpp}` — сборщик
`bool PdfReportWriter::write(const PdfReportOptions &, const QString &path, QString *error)`:
1. `QPrinter` PDF A4, поля 10 мм; один `QPainter`.
2. Первая страница: `Statistics().doctorReport(...)` → `DoctorReportPage::html` + график →
   `paintHtmlPages`.
3. «День»: запомнить `daily->getDate()`; для каждой ночи из `nightsToPrint`:
   `daily->LoadDate(date)` → `printer.newPage()` → `Report::paint(..., daily->graphView(), STR_TR_Daily,
   date, target)`; в конце `daily->LoadDate(прежняя дата)`.
4. «Сводка»: запомнить диапазон и набор; `overview->setRange(from, to)`,
   `overview->showPreset(preset)` (для «Все» — собственный выбор) → `Report::paint(...,
   overview->graphView(), STR_TR_Overview, ...)`; вернуть диапазон и набор.
5. «Статистика»: `Statistics().periodHtml(...)` → `paintHtmlPages`.
6. Отмена → `painter.end()`, файл удаляется, `false`.
Шаги 3–4 восстанавливают состояние вкладок и при ошибке/отмене (RAII-страж).

### `oscar/pdfreportdialog.{h,cpp}` — окно
Виджеты §2; изменения пересчитывают оценку страниц (`estimatePages` с числом ночей из профиля,
числом видимых графиков «Дня» и графиков выбранного набора «Сводки», для статистики — 2
страницы, 3 с изменениями настроек); «Создать отчёт…» → `QFileDialog`, `PdfReportWriter`,
`ProgressDialog`, сообщение «Отчёт сохранён» с «Открыть»; запоминание `toMap()`.

### Перевод
Все новые строки — в `Translations/Russkiy.ru.ts` без «unfinished»; `validate_ts.py` → `TOTAL 0`.

## 4. Граничные случаи
- Период без ночей CPAP — сообщение (§2).
- «Все ночи периода» на 90 ночах — длинный отчёт; оценка страниц это показывает.
- Ночь, в которой нет видимых графиков «Дня», — только шапка (§2).
- Профиль без оксиметрии — подгалочка «Оксиметрия» ни на что не влияет.
- Режим RDI — подписи как на экране.
- Отмена посреди «Дня» — вкладка «День» возвращается к прежней дате, файл удалён.
- Старая «Файл → Печать» для «Дня», «Сводки», «Статистики» — без изменений поведения.

## 5. Тесты
- `PdfReportOptionsTests`: наборы §2; `range()` для 7/30/90/своего периода; `nightsToPrint()` —
  только ночи с данными, от конца периода, не больше выбранного числа; `estimatePages()`;
  `toMap()/fromMap()` туда и обратно, неизвестные значения → по умолчанию.
- `HtmlPagesTests`: короткий HTML — 1 страница; длинная таблица — несколько страниц; в PDF столько
  же объектов `/Type /Page`.
- `AnalysisIntegrationTests` (профиль во временной папке):
  - `periodHtml` без оксиметрии и без аппаратов не содержит этих заголовков; с ними — содержит;
  - `personalData = false` — нет имени в шапке;
  - `PdfReportWriter` с первой страницей и статистикой на одной ночи пишет PDF с ≥ 2 страницами.
- Глазами:
  - старая «Печать» «Дня» и «Сводки» как раньше;
  - «Подробно» за 03.09–02.10 на профиле «Папа»: один файл, первая страница, 7 ночей, «Сводка»
    «Терапия», «Статистика»; после — вкладки в прежнем состоянии.

## 6. Вне рамок
- Выбор отдельных графиков «Дня» в окне (берутся видимые на вкладке).
- Отправка отчёта врачу.
- Компактная страница ночи.
- Предпросмотр PDF внутри окна.
