# Собственный анализ: второе мнение, SpO₂ и пульс — план реализации

> Рабочий документ форка, в MR не входит. Шаги отмечаются `- [x]` по мере выполнения.

**Goal:** реализовать спецификацию `docs/superpowers/specs/2026-09-28-sleep-analysis-design.md`
(дальше — «спец.»): собственный анализ ночи по потоку CPAP и оксиметрии, сравнение с
событиями аппарата и показ в «Дне», «Обзоре», «Статистике», настройках.

**Architecture:** чистые анализаторы в `oscar/SleepLib/analysis/` (только QtCore, без
`Session`/`Profile`, тестируются на синтетике) → адаптер этапа 1 на сессию (каналы `AN_*` в
`EventList`, штамп) → этап 2 на день (правило гипопноэ, связь с десатурациями, сравнение,
строка `analysis_daily`) → `AnalysisService` (ленивый и пакетный пересчёт, кэш) → интерфейс.

**Tech Stack:** C++17, Qt 6 (сборка здесь — Qt 6.4 из apt; у пользователя — Qt 6.11 Homebrew,
macOS clang), qmake, QtTest (`tests/AutoTest.h`), SQLite.

## Общие правила

- Ветка `claude/gallant-lovelace-h3etsf`; коммит на задачу, каждый собирается и проходит
  тесты. Сообщения коммитов — английские, с трейлерами `Co-Authored-By`/`Claude-Session`.
- Строки интерфейса — английские через `tr()`; русский перевод — в последней задаче.
- Новые файлы — шапка `Copyright (c) 2026 The OSCAR Team` + абзац GPL, как у соседей.
- Предупреждения — ошибки (`-Werror`): новый код без предупреждений в GCC и Clang.
- Анализ **никогда** не меняет события, AHI и статистику аппарата (спец. §1, критерий 4).

### Проверка перед каждым коммитом (в этой среде)
Сборки лежат в scratchpad-каталоге сессии (`$SP`):
- приложение: `$SP/wgcc` (GCC, `qmake6 oscar/oscar.pro`), `make -j4`;
- тесты: `$SP/build-test` (`qmake6 CONFIG+=test`, ASan), `make -j4`, затем
  `QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=0 ./test` — 0 провалов;
- Clang: синтаксическая проверка изменённых файлов командами из `$SP/wclang/cmds.txt`;
- новые файлы в `oscar.pro` → перезапуск `qmake6` в обоих каталогах;
- перед отправкой крупных этапов — проверка по заголовкам Qt 6.10/6.11 (conda-forge,
  `$SP/qt610`, `$SP/qt611`): только предупреждения об устаревших API.

## Уточнения спецификации

1. **Спец. §7.3 устарел:** после исправления `apextests` весь проект и все тесты собираются
   на Qt 6.4 здесь. Отдельный тестовый `.pro` не нужен — тесты идут в общий `CONFIG+=test`.
2. **Задача A1 выполнена** в ходе исправлений ревью: `c8ef807` (глубина SpO2 Drop и
   величина Pulse Change).
3. **Хеши параметров:** SHA-1 (первые 16 hex) от канонической строки `ключ=значение;…`
   параметров этапа, с `kAnalysisAlgoVersion` в начале. Отдельно `flowHash`, `oxiHash`,
   `dayHash` — смена параметров потока не требует пересчёта оксиметрии и наоборот.
4. **Сетка 1 Гц для списков «по изменению»:** последнее значение списка удерживается до
   `EventList::last()` (конец списка), а если конец не известен — одну секунду.
5. **Потоки импорта:** многопоточность импорта выключена по умолчанию, но этап 1 всё равно
   получает снимок `AnalysisParams`, сделанный в главном потоке (`Profile::analysis` не
   читается из рабочих потоков).
6. **Проблемные зоны (§3.2.8):** объединённые проблемные окна сужаются до содержимого —
   десатураций с надиром внутри и секунд ниже «низкого» порога; зона короче `zoneMinSec`
   не отбрасывается, а расширяется до него вокруг содержимого. Иначе 5-минутные окна
   растягивали бы зону на минуты в обе стороны, а «90 с ниже 90 %» (пример спец. §7.1)
   отбрасывалась бы как короткая.
7. **Выбросы SpO₂ (§3.1.4):** «возврат» — значение в пределах 3 п.п. от уровня до скачка.
8. **Этап 1 при импорте** вызывается в `Session::UpdateSummaries()` после `calcSPO2Drop`/
   `calcPulseChange`, только если анализ включён; сбой (исключение, NaN) пишет `qWarning` и
   оставляет сессию без каналов анализа (спец. §6).
9. **Штамп этапа 1 несёт итоги потока** (`a`, `hz`, `fls`, `s`, `u`, `fl`, `flb`: анализирован ли
   поток, частота, оценён ли FL, оцениваемые и неоцениваемые секунды, сумма и число оценок FL),
   чтобы этапу 2 не нужна была волна. `kAnalysisAlgoVersion` = 2 (формат штампа изменился).
   `flow_s` в `analysis_daily` — **оцениваемое** время (знаменатель индексов), `unscoreable_s`
   — отдельно.
10. **Оксиметрия этапа 2 пересчитывается** по 1-Гц SpO₂/пульсу дня (выбранный источник, время
    со сдвигом поправок, опция «только время CPAP»): каналы этапа 1 — для графиков, строка дня
    и связь с событиями — из пересчёта (в каналах нет времени надира). Пульс берётся у того же
    устройства, что и SpO₂, если есть, иначе у устройства с наибольшим временем пульса.
11. **Покрытие SpO₂ (§3.4.1)** — валидные данные на ≥ 75 % секунд `[s, e + 30 с]`.
    `n_unconfirmable` — непокрытые кандидаты при любом правиле, кроме Flow only.
12. **Классификация гипопноэ** — при ≥ 2 оценённых дыханиях в событии, иначе `AN_Hypopnea`.
    **ΔHR** — при ≥ 5 валидных отсчётах пульса до события и после.
13. **Сравнение с аппаратом** — только события аппарата, середина которых попадает в сессию с
    анализированным потоком и не в неоцениваемый участок. ResMed пока с соглашением «метка =
    конец», как рисует OSCAR; этап 2 пишет в лог медиану смещения метки аппарата и от конца, и
    от начала совпавших апноэ анализа.
14. **Необъяснённые десатурации** — только с надиром в оцениваемом времени CPAP.
15. **Подсказка сдвига (§3.4.7)** — окно `[конец, конец + 40 с]` даёт плато равных лагов; берётся
    середина самого длинного плато (при равенстве — ближайшего к 0). Только если SpO₂ не от
    самого CPAP.
16. **Сессии без событий** (только сводка) получают штамп «проанализировано, ничего нет», чтобы
    день не считался устаревшим вечно. `inputs_hash` берёт счётчики событий аппарата из сводки
    (`m_cnt`), а не из загруженных событий.
17. **Когда пересчитывается (§4.4).** `AnalysisService` различает дни «ожидающие» (этап 1 всех
    сессий актуален, этап 2 отсутствует или устарел: новый импорт, поправки времени, вкл./выкл.
    сессии, очистка) и «устаревшие» (нужен этап 1 — данные до появления анализа или новые
    параметры потока/оксиметрии; нужны волны). Ожидающие `MainWindow::updateAnalysis()`
    пересчитывает сразу после импорта (CPAP, прочие устройства, мастер оксиметра вместе с BLE),
    сохранения поправок, переключения сессии и очистки: до 10 дней — без диалога, больше — с
    прогрессом и отменой. Устаревшие ждут открытия дня или *Recalculate Analysis…*
    (`updateAnalysis(true)`), чтобы первый импорт после обновления не запускал анализ всей
    истории.
18. **`Session::LoadEventsFromDatabase(only)`** считает сессию с событиями в памяти загруженной,
    даже если `s_events_loaded` не выставлен (так остаются сессии после импорта), — иначе она
    помечалась частичной, и следующий `OpenEvents()` выбросил бы несохранённые события.
19. **Диапазоны SpO₂ (§3.2.7)** следуют смыслу «ниже порога»: для порогов 94, 90, 88, 85, 80 —
    `≥ 94`, `90–<94`, `88–<90`, `85–<88`, `80–<85`, `< 80` (в спецификации пример `≥ 95`, `90–94`
    противоречил «время ниже 94 %»).
20. **«День»** при открытии дня вызывает `AnalysisService::dayResult()` до `GraphView->setDay()`:
    этап 1 устаревших сессий (события дня уже загружены), этап 2 при устаревшей строке, иначе
    только оценка для показа. Ссылка «Align oximeter…» открывает режим выравнивания и сразу
    применяет предложенный сдвиг (предпросмотр, сохранение — как обычно).
21. **«Обзор» (§5.2):** у `gSummaryChart` нет второй серии рядом со столбиком, поэтому «AHI:
    device vs analysis» — столбик AHI анализа по типам событий, а AHI аппарата — в подсказке и в
    итоговой строке над графиком («Device Med./W-Avg»); рядом и так стоит штатный график AHI
    аппарата. ODI — стопка: ODI 4 % снизу, остаток ODI 3 % сверху. Строка «Analysis is outdated
    for N days — Recalculate» — в панели «Обзора» и в разделе «Статистики»; число устаревших
    дней кэшируется в сервисе.
23. **Перевод (C17):** в `Russkiy.ru.ts` добавлены только строки анализа (230); файл отстаёт и по
    другим функциям (BLE, выравнивание и др.) — это отдельная задача, `lupdate` по всему проекту
    не запускался, чтобы не смешивать изменения.
22. **«Статистика» (§5.3):** вместо «время в проблемных зонах» — % времени оксиметрии в зонах,
    «время ниже порога» — % времени; ΔHR — средний прирост пульса на событие.

## Интерфейсы движка (namespace `analysis`)

```cpp
// analysis_params.h
constexpr int kAnalysisAlgoVersion = 1;
enum class HypopneaRule { Auto = 0, Aasm3 = 1, Cms4 = 2, FlowOnly = 3 };
struct OxiParams  { double desatMinDrop = 3, desatMinSec = 10, desatMaxFallSec = 120,
                    desatMaxSec = 180, pulseRise = 6, bradyBpm = 40, tachyBpm = 120,
                    bradyTachyMinSec = 30, zoneLowPct = 90, zoneCriticalPct = 85,
                    zoneWindowSec = 300, zoneStepSec = 30, zoneMinSec = 120,
                    zoneMergeGapSec = 120, zoneLowSec = 60, zoneCriticalSec = 30;
                    int zoneMinDesats = 3; };
struct FlowParams { double apneaReduction = 0.9, hypopneaReduction = 0.3, minEventSec = 10,
                    maxEventSec = 120, baselineWindowSec = 120, baselinePercentile = 70,
                    flThreshold = 0.5; bool classifyApneas = true; };
struct DayParams  { HypopneaRule rule = HypopneaRule::Auto; double flowOnlyReduction = 0.5,
                    linkWindowSec = 30; bool limitOxiToCpap = false,
                    pulseRiseAsArousal = false; };
struct AnalysisParams { bool enabled = true; OxiParams oxi; FlowParams flow; DayParams day;
                        QString oxiHash() const; QString flowHash() const; QString dayHash() const; };

// signal_utils.h — NaN = нет данных
struct TimedSamples { QVector<qint64> t; QVector<float> v; qint64 end = 0; };  // один EventList
struct Grid { qint64 start = 0; qint64 stepMs = 1000; QVector<float> v; /* timeAt(i), size() */ };
Grid toOneHz(const QVector<TimedSamples>&, float minValid, float maxValid);   // спец. §3.1.2–3
QVector<float> medianFilter(const QVector<float>&, int window);
QVector<float> movingAverage(const QVector<float>&, int window);
float percentile(QVector<float> values, double p);                           // без NaN
QVector<float> decimateMean(const QVector<float>&, int factor);

// oxi_analyzer.h — спец. §3.1–3.2
struct Desaturation { qint64 start, nadirTime, end; float peak, nadir; double area; };
struct Span { qint64 start = 0, end = 0; float value = 0; };                // value → data2
struct PulseRise { qint64 start, end; float amplitude; };
struct ProblemZone { qint64 start, end; int severity; float minSpo2; int desats;
                     int lowSec, criticalSec; float meanPulse; int pulseRises; };
struct OxiResult { /* desats, cyclic, pulseRises, brady, tachy, zones,
                      spo2Hist[51] (с по 50…100 %), pulseHist[191] (30…220),
                      суммы/минимумы/максимумы для analysis_daily */ };
OxiResult analyzeOximetry(const Grid& spo2, const Grid& pulse, const OxiParams&);

// flow_analyzer.h — спец. §3.3
struct FlowChunk { qint64 start; double rateMs; QVector<float> samples; };  // один EventList
struct Breath { qint64 start, inspEnd, end; float pif, pef, vi, amplitude, fl; };
enum class ApneaClass { Unclassified, Obstructive, Central };
struct FlowEvent { qint64 start, end; bool apnea; float reduction; float baseline;
                   ApneaClass cls = ApneaClass::Unclassified; };
struct FlowResult { /* analyzed, sampleRateHz, flEnabled, breaths, events, flSpans, reras,
                       periodic, unscoreable, flowMs, unscoreableMs, показатели FL */ };
FlowResult analyzeFlow(const QVector<FlowChunk>&, const QVector<Span>& excluded,
                       const Grid* pulse, const QVector<Span>* obstructLevel, const FlowParams&);

// event_matcher.h — спец. §3.4.6
struct MatchEvent { qint64 start, end; int group; int type; };
struct MatchResult { QVector<QPair<int,int>> matched; QVector<int> deviceOnly, analysisOnly;
                     int typeMismatch = 0; };
MatchResult matchEvents(const QVector<MatchEvent>& device, const QVector<MatchEvent>& analysis,
                        qint64 toleranceMs = 5000);

// day_scorer.h — спец. §3.4: вход — события/сетки уже в скорректированном времени
struct DayResult;   // гипопноэ с классом, счётчики по трём правилам, связь, HB, ΔHR,
                    // сравнение, подсказка сдвига, всё для строки analysis_daily
DayResult scoreDay(const DayInput&, const DayParams&);
```

## Задачи

### Этап A. Движок

- [x] **A2. Параметры и утилиты сигналов.** Создать `analysis_params.{h,cpp}`,
  `signal_utils.{h,cpp}`; тесты `tests/analysissignaltests.{h,cpp}` (спец. §7.1 п. 1): сетка
  из списков «по изменению» и волны, разрывы между списками, отбраковка вне диапазона,
  медианный фильтр и скользящее среднее с NaN, перцентиль против наивного, прореживание,
  хеши (стабильны, меняются при смене параметра своего этапа и только его).
- [x] **A3. Анализатор оксиметрии** `oxi_analyzer.{h,cpp}` (§3.1.4–3.2.8): фильтр выбросов,
  сегменты, десатурации (автомат пик-надир), циклические эпизоды, подъёмы пульса, бради/тахи,
  гистограммы, проблемные зоны, показатели. Генераторы синтетики `tests/analysis_synth.{h,cpp}`;
  тесты `tests/oxianalyzertests.{h,cpp}` — спец. §7.1 пп. 2, 3, 3a.
- [x] **A4. Анализатор потока: дыхания, огибающая, кандидаты** `flow_analyzer.{h,cpp}`
  (§3.3.1–3.3.4, 3.3.9): прореживание > 25 Гц, снятие смещения, сегментация с гистерезисом,
  огибающая и базовая линия, автомат событий, неоцениваемые участки. Тесты
  `tests/flowanalyzertests.{h,cpp}` — спец. §7.1 п. 4.
- [x] **A5. FL, RERA по потоку, периодическое дыхание** (§3.3.5, 3.3.7, 3.3.8) в
  `flow_analyzer`; тесты п. 5 и 7.
- [x] **A6. Классификация апноэ** `apnea_classifier.{h,cpp}` (§3.3.6); тесты п. 6.

### Этап B. Интеграция и хранение

- [x] **B7. Каналы анализа.** `schema.cpp`: каналы `0x1A00–0x1A30` (спец. §4.2), группа
  `GRP_ANALYSIS`; `Channel::isComputed()`; исключение вычисленных каналов из
  `Machine::noteReportedChannels`, записи `respiratory_events`, круговой диаграммы, «Event
  Flags» и списков AHI; тест: вычисленные каналы не попадают в `reportedChannels`.
- [x] **B8. Этап 1 на сессию.** `session_analysis.{h,cpp}`: `Session` → анализаторы → `EventList`
  `AN_*` + штамп `AN_Stamp` в `session_settings`; вызов из `UpdateSummaries`; оксиметрические
  каналы — в сессию-источник SpO₂. `Session::StoreChannelEvents(const QList<ChannelID>&)` и
  `Session::LoadEventsFromDatabase(const QSet<ChannelID>& only)` с флагом `m_partialEvents`
  (полная запись частично загруженной сессии запрещена). Тесты — спец. §7.1 п. 11.
- [x] **B9. Таблица `analysis_daily`** (схема v20, миграция v19 → v20) и
  `database/analysis_daily_repository.{h,cpp}`; исключить таблицу из резервной копии;
  очистка дней удаляет строки. Тесты п. 10 (временная БД, upsert/чтение/удаление, миграция).
- [x] **B10. Этап 2 на день.** `event_matcher.{h,cpp}`, `day_scorer.{h,cpp}`,
  `device_event_conventions.{h,cpp}`, `day_analysis.{h,cpp}` (`Day` → вход в скорректированном
  времени → каналы `AN_*Hypopnea` точечной записью + строка `analysis_daily` с `inputs_hash`).
  Тесты пп. 8–9.
- [x] **B11. `AnalysisService`** (`analysis_service.{h,cpp}`, `QObject`, владелец —
  `MainWindow`): `ensureDay`, `invalidate(machine, dates)`, пакетный пересчёт с прогрессом и
  отменой, кэш `analysis_daily` для «Обзора»/«Статистики»; вызовы после импорта (CPAP,
  оксиметр, BLE), при выравнивании, сохранении поправок, включении/выключении сессии, очистке.
  `AnalysisSettings : PrefSettings` (`p_profile->analysis`, ключи `STR_AN_*`).

### Этап C. Интерфейс

- [x] **C12. Графики «Дня»:** «Analysis Flags» (фильтр каналов в `gFlagsGroup`; вычисленные
  каналы убраны из «Event Flags»), «Flow Limitation (analysis)», наложения на SpO₂/пульс.
- [x] **C13. Раздел «Analysis (second opinion)» и вкладка «Analysis»** в «Дне» (спец. §5.1):
  таблица «Device | Analysis», оксиметрия, время ниже порогов, проблемные зоны, правила
  гипопноэ, пульс, согласие, примечания, подсказка сдвига (`align=oximeter`), дерево
  расхождений с переходом по щелчку; ночь только с оксиметром.
- [x] **C14. Графики «Обзора»** (спец. §5.2) на `gSummaryChart` из кэша.
- [x] **C15. Раздел «Статистики»** (`SC_ANALYSIS`, агрегация суммами).
- [x] **C16. Настройки и меню:** вкладка «Analysis» (селектор правила гипопноэ с описаниями,
  пороги, Advanced, Reset), вопрос о пересчёте по изменившимся хешам; *Data → Recalculate
  Analysis…*.
- [x] **C17. Отчёт, документация, перевод:** системный отчёт «Analysis per day»
  (`docs/system_reports.orf` + генератор строк), `Notes/Developer Notes/SLEEP_ANALYSIS.md`,
  русский перевод новых строк в `Translations/Russkiy.ru.ts`.

## Ручная проверка (у пользователя, на копии данных)
Спец. §7.2: соглашения (знак потока, частота Prisma, onset/конец событий ResMed), настройка
порогов на ≥ 30 ночах ResMed и Prisma, чек-лист интерфейса. Запуск только с
`--datadir OSCAR20_Data_dev` (схема v20 необратима).
