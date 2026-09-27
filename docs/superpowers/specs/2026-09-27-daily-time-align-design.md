# Выравнивание времени устройства прямо в окне «День» — дизайн

- Дата: 2026-09-27
- Ветка: `feature-daily-time-align` (форк `SemenKeyzer/oscar-sql`, upstream `gitlab.com/CrimsonNape/oscar-sql`)
- Статус: согласовано в обсуждении, ожидает ревью спецификации

> Этот файл — рабочая спецификация форка. В Merge Request в upstream он не включается.

## 1. Цель и контекст

### Проблема
Оксиметр часто не передаёт точное время, и после импорта график SpO₂/пульса смещён
относительно событий CPAP на произвольную величину (10 минут, час…). Синхронизация на шаге
импорта (`oximeterimport.cpp`, `on_syncButton_clicked`) **разрушительна**: выбранное время
навсегда «запекается» в метки событий сессии, и если при импорте ошиблись, график остаётся
кривым. Исправить это во время анализа дня сейчас нельзя удобным способом.

### Что уже есть в upstream (переиспользуем, не дублируем)
- Таблица `device_time_corrections` + `DeviceTimeCorrectionRepository`
  (`oscar/database/device_time_correction_repository.*`): неразрушающие поправки времени
  на устройство и ночь/диапазон ночей, с мягким удалением (`undone_at`) и историей.
- `Machine::correctionMs(QDate night)`, `Machine::rebuildCorrections()`,
  `Machine::reloadCorrectionsFromDb()` (`oscar/SleepLib/machine.cpp`).
- `Session::correctionMs()` применяется при каждой отрисовке графиков
  (`gLineChart`, `gFlagsLine`, `gLineOverlay`, `gSleepStageChart`, `MinutesAtPressure`)
  и в `daily.cpp`. Кэшей отрисовки нет — живой предпросмотр дешёвый.
- Диалог **Data → Time Corrections** (`devicetimecorrectiondialog.*`) и
  **Drift Analysis** (`driftanalysisdialog.*`).

### Цель
Дать возможность **визуально совместить** данные не-CPAP устройства (прежде всего оксиметра)
с событиями CPAP прямо на графиках окна «День»: перетаскиванием мышью и кнопками, с точной
цифрой смещения на экране, с сохранением в существующую таблицу поправок.

### Критерии успеха
1. Сдвиг оксиметра на 10 мин или 1 ч занимает несколько секунд и не требует ухода из окна «День».
2. Во время перетаскивания всегда видна точная цифра смещения (итог и изменение за текущий жест).
3. Сохранённый сдвиг переживает перезапуск, виден в Data → Time Corrections, отменяем.
4. Исходные данные устройства не изменяются.
5. Поведение графиков вне режима выравнивания не меняется.
6. Сборка без предупреждений; новые юнит-тесты проходят.

### Решения пользователя (из обсуждения)
- Смещение оксиметра «бывает по-разному»: по умолчанию — **только текущая ночь**; быстрое
  повторное применение — «Same as last night»; диапазоны дат — через существующий диалог.
- Вариант взаимодействия: **A — режим выравнивания + перетаскивание** (а не только кнопки
  и не кнопки в контекстном меню).
- Во время перетаскивания цифра смещения видна у курсора и крупно в полосе выравнивания.

## 2. Поведение (UX)

Строки интерфейса пишутся на английском через `tr()` (как весь OSCAR); переводы — через
штатный механизм `.ts` проекта.

### 2.1 Вход в режим
- Кнопка **«⇄ Align»** на нижней панели окна «День» (`frame` в `daily.ui`, рядом с `layout`).
  Видна, только если в выбранную ночь есть сессии устройства с
  `Machine::isCorrectableType(type) && type != MT_CPAP`.
- Пункт **«Align device time…»** в контекстном меню графика (`gGraphView::populateMenu`),
  если канал графика присутствует в сессиях такого устройства в этой ночи. Открывает режим
  сразу для этого устройства.

### 2.2 Полоса выравнивания (`TimeAlignBar`)
Показывается над графиками (над `graphFrame`), пока режим активен:
- выбор устройства (combo; по умолчанию — оксиметр, если есть; иначе первое подходящее);
- крупная моноширинная цифра смещения этой ночи `±HH:MM:SS`;
- кнопки `−1h −10m −1m −10s | +10s +1m +10m +1h`;
- **Same as last night** — подставить смещение ближайшей предыдущей ночи, где у этого
  устройства есть однодневная строка `offset`;
- **More options…** — открыть существующий диалог Time Corrections на этом устройстве и дате
  (для диапазонов дат); перед открытием несохранённый сдвиг нужно сохранить или отменить;
- **Cancel** и **Save**;
- строка состояния: подсказки/предупреждения (см. §4).

### 2.3 Графики в режиме
- Целевые графики (каналы выбранного устройства в этой ночи) — пунктирная рамка акцентным
  цветом и значок ⇄ у заголовка. Графики CPAP — эталон, не двигаются.
- **Левое перетаскивание по целевому графику сдвигает его** (вместо выделения/зума).
  На остальных графиках мышь работает как обычно.
- Во время перетаскивания у курсора — плашка `+00:10:30  (Δ +00:05:00)`: итоговое смещение
  ночи и изменение за текущий жест. Цифра в полосе обновляется синхронно.
- Округление шага перетаскивания зависит от масштаба (мс на пиксель, `msPerPx`):
  `msPerPx > 20 000` → 1 мин; `msPerPx > 2 000` → 10 с; иначе 1 с.
  (Вся ночь на экране ≈ 30 000 мс/px → шаг 1 мин.)
- Клавиши (только в режиме): `←/→` — ±10 с, `Shift+←/→` — ±1 мин, `Enter` — Save,
  `Esc` — Cancel. Вне режима клавиши работают как раньше (панорама, история зума).

### 2.4 Завершение
- **Save**: сдвиг записывается для этой ночи, режим закрывается, графики/список событий/открытый
  диалог Time Corrections обновляются.
- **Cancel**: предпросмотр откатывается к сохранённому состоянию, режим закрывается.
- Смена даты / профиля / устройства / выход с несохранённым сдвигом → вопрос **Save / Discard / Cancel**.

## 3. Архитектура

### 3.1 Хранение — без изменения схемы
- Сдвиг ночи = строка `type='offset'`, `date_from = date_to = <ночь>` в
  `device_time_corrections`, запись через существующий
  `DeviceTimeCorrectionRepository::upsertOffset(machineId, date, offsetMs)`:
  предыдущая такая строка мягко удаляется (история сохраняется), `offsetMs == 0` —
  только удаление.
- Такие строки уже понимают диалог Time Corrections (история, редактирование, удаление)
  и Drift Analysis (`findManualOffsetRows` — точки для подгонки дрейфа).
- **Редактируемое значение** в полосе — именно однодневная `offset`-строка этой ночи.
  Прочие активные поправки (диапазоны, DST, дрейф) продолжают действовать и складываются
  (как в `Machine::correctionMs`). Если они есть, в полосе показывается примечание
  «Other corrections also apply: ±HH:MM:SS».

### 3.2 Компоненты

**(1) Общий конвертер строки БД → `TimeCorrectionRow`** (`SleepLib/machine.*`)
- `static TimeCorrectionRow Machine::rowFromData(const DeviceTimeCorrectionData&)`:
  перенос `type`, дат, `offsetMs`, `c0Ms`, `c1` и снятие старого сторожевого значения
  дрейфа (`type=="drift" && c1 >= 1.0 → c1 - 1.0`).
- Используется в `Machine::reloadCorrectionsFromDb` и в
  `DeviceTimeCorrectionDialog::previewStaged` — **исправляет существующий баг** предпросмотра
  диалога (там не копировался `type` и не снимался сторож дрейфа, из-за чего строки дрейфа
  в предпросмотре применялись как постоянные смещения). Отдельный коммит.

**(2) `TimeAlignSession`** (новые `oscar/timealignsession.{h,cpp}`, `QObject`, без виджетов)
- `bool begin(Machine* mach, QDate night)` — запоминает сохранённое значение
  однодневной `offset`-строки (`savedMs`, 0 если нет) и остальные активные строки.
- `void setOffsetMs(qint64 ms)` / `void nudge(qint64 deltaMs)` — ограничение ±12 ч;
  строит список: активные строки из БД **кроме** однодневной `offset`-строки этой ночи
  + временная строка с `ms` (если ≠ 0); вызывает `mach->rebuildCorrections(rows)`;
  сигнал `offsetChanged(qint64 ms)`.
- `bool commit()` — `upsertOffset(...)`, затем `Machine::reloadCorrectionsFromDb(mach)`;
  при ошибке возвращает `false`, состояние предпросмотра сохраняется. Сигнал `committed()`.
- `void cancel()` — `Machine::reloadCorrectionsFromDb(mach)`; сигнал `cancelled()`.
- `bool isDirty() const` — `offsetMs() != savedMs()`.
- `std::optional<qint64> previousNightOffset() const` — ближайшая строка
  `findManualOffsetRows` с датой < `night`.
- `qint64 otherCorrectionsMs() const` — `mach->correctionMs(night)` без учёта редактируемой.
- `static qint64 snapStepMs(double msPerPx)` и `static qint64 snapDelta(qint64 rawMs, double msPerPx)` —
  чистые функции округления (§2.3).
- `static QString formatOffset(qint64 ms)` — `±HH:MM:SS`.

**(3) `TimeAlignBar`** (новые `oscar/timealignbar.{h,cpp}`, `QWidget`, UI в коде, как
`TimeAlignmentWelcomeDialog`)
- Только отображение и сигналы: `deviceChosen(Machine*)`, `nudgeRequested(qint64)`,
  `sameAsLastNightRequested()`, `moreOptionsRequested()`, `saveRequested()`,
  `cancelRequested()`.
- Слоты: `setDevices(QList<Machine*>, Machine* current)`, `setOffset(qint64)`,
  `setStatus(QString text, Severity)`, `setSameAsLastNightEnabled(bool)`.

**(4) Режим выравнивания в `gGraphView`** (`Graphs/gGraphView.*`)
- API: `void setAlignMode(bool on, const QSet<QString>& targetGraphNames)`,
  `bool alignMode() const`.
- Сигналы: `alignDragStarted()`, `alignDragDelta(qint64 snappedDeltaMs)` (от начала жеста),
  `alignDragFinished()`, `alignNudge(qint64 deltaMs)`, `alignAccept()`, `alignCancel()`.
- Состояние перетаскивания — на уровне вида (по образцу `m_sizer_dragging`), чтобы жест
  не обрывался при уходе курсора на соседний график. Нажатие ЛКМ по области графика из
  `targetGraphNames` в режиме начинает жест; `mousePressEvent/mouseMoveEvent/mouseReleaseEvent`
  не передают такие события в `gGraph` (нет зума/выделения).
- Перевод пикселей: `msPerPx = (graph->max_x - graph->min_x) / plotWidthPx` для графика,
  где начат жест; округление — `TimeAlignSession::snapDelta`.
- Отрисовка в режиме: пунктирная рамка + ⇄ у целевых графиков; во время жеста — плашка
  у курсора с текстом, который задаёт владелец (`setAlignDragLabel(QString)`), рисуется
  поверх графиков в `renderGraphs`.
- Клавиши: в режиме `←/→`, `Shift+←/→`, `Enter/Return` перехватываются в `keyPressEvent`,
  `Esc` — в `keyReleaseEvent` (там gGraphView обрабатывает Esc как «назад по истории зума»,
  иначе отмена выравнивания заодно сбросила бы зум); вне режима — без изменений.
- Контекстное меню: `populateMenu(g)` добавляет «Align device time…», если владелец
  разрешил для графика (`setAlignMenuPredicate(std::function<bool(gGraph*)>)`),
  по выбору — сигнал `alignRequestedForGraph(gGraph*)`.

**(5) Интеграция в `Daily`** (`daily.{h,cpp,ui}`)
- Кнопка `alignButton` на панели `frame`; видимость обновляется в `Load(QDate)`.
- `alignCandidates(Day*)` — машины из `day->sessions` с `isCorrectableType && type != MT_CPAP`,
  без дублей.
- `targetGraphsFor(Machine*, Day*)` — имена графиков из `graphlist`, чьи каналы
  (`sess->channelExists(code)`) есть в сессиях этой машины за день.
- Владеет `TimeAlignSession` и `TimeAlignBar`, связывает сигналы с `gGraphView`.
  Во время жеста: `session.setOffsetMs(startMs + delta)`, подпись у курсора, перерисовка
  `GraphView->timedRedraw(0)`; по окончании жеста и после Save/Cancel —
  `redrawWithZoom()` (пересчёт границ `rmin_x/rmax_x`).
- После Save: `redrawWithZoom()` + обновление дерева событий слева (сейчас не обновляется
  после поправок — чиним в рамках задачи) + сигнал `timeCorrectionsChanged()` в `MainWindow`,
  который вызывает `m_correctionDialog->setDate(...)`, если диалог открыт.
- Защита несохранённого сдвига — метод `bool Daily::confirmAlignExit()` (Save / Discard /
  Cancel; `true` — можно продолжать):
  - смена даты: все пути (календарь мышью, ←/→ дня, `LoadDate`) сходятся в
    `Daily::on_ReloadDay()` — проверка в его начале, до `Unload(previous_date)`; при Cancel
    выделение календаря возвращается на `previous_date` с заблокированными сигналами;
  - смена профиля, закрытие приложения, импорт/очистка данных: все эти пути вызывают
    `Daily::Unload()`, где задаётся вопрос **только Save / Discard** — эти действия
    в `MainWindow` (`closeEvent`, `CloseProfile`) не умеют прерываться, поэтому Cancel там
    не предлагается (уточнение при составлении плана);
  - смена устройства в combo полосы — тот же вопрос.
- «More options…»: требует Save/Discard, затем `MainWindow::on_actionTime_Corrections_triggered()`
  с выбором устройства (нужен метод диалога `selectMachine(Machine*)`).

### 3.3 Поток данных
```
мышь/кнопки/клавиши ─► gGraphView / TimeAlignBar ─(сигналы)─► Daily
Daily ─► TimeAlignSession.setOffsetMs ─► Machine::rebuildCorrections (память) ─► перерисовка
Save  ─► TimeAlignSession.commit ─► DeviceTimeCorrectionRepository::upsertOffset (SQLite)
                                  ─► Machine::reloadCorrectionsFromDb ─► redrawWithZoom + события + диалог
Cancel ─► TimeAlignSession.cancel ─► Machine::reloadCorrectionsFromDb ─► redrawWithZoom
```

## 4. Ошибки и крайние случаи
| Ситуация | Поведение |
|---|---|
| Ошибка записи при Save | Сообщение «Couldn't save the time correction.»; режим открыт, значение сохранено; лог через `qCritical`/`checkQueryError` (как в репозитории) |
| Смена даты/устройства с несохранённым | Save / Discard / Cancel; Discard → `cancel()`; Cancel прерывает действие |
| Выход/смена профиля/импорт/очистка с несохранённым | Save / Discard (прервать эти действия нельзя) |
| \|сдвиг\| > 3 ч | Предупреждение «Large offset — check the device clock or use a date-range correction.» |
| Предел | ±12 ч; кнопки/перетаскивание дальше не двигают |
| Сдвиг вернули в 0 и Save | Однодневная строка мягко удаляется (`upsertOffset(..., 0)`) |
| В ночи нет CPAP-сессий | Режим доступен, подсказка «No CPAP data this night to align against.» |
| Устройство/день пропали при открытом режиме (пересчёт, удаление) | Режим закрывается без сохранения |
| Открыт диалог Time Corrections с несохранённой правкой | Перед входом в режим — предупреждение; после Save — диалог обновляется |

**Известное ограничение (поведение OSCAR в целом):** поправка не переносит сессию в другой
день (`Machine::AddSession` определяет день при загрузке). Для сдвигов в минуты/часы это
не проблема; для больших — предупреждение выше.

Примечание: существующий диалог показывает «Large offset» для типа `offset` уже после
15 мин (`kLargeOffsetThresholdMs`). Для оксиметров сдвиги 10–60 мин — норма, поэтому в полосе
порог 3 ч; порог диалога не меняем.

## 5. Тестирование

### Автотесты (`qmake CONFIG+=test`, `oscar/tests`, регистрация через `DECLARE_TEST`)
1. `Machine::correctionMs` — сумма строк, границы диапазонов, открытый конец, строка дрейфа.
2. `Machine::rowFromData` — перенос полей, снятие сторожа дрейфа (`c1 >= 1.0`) — регресс
   на баг предпросмотра.
3. `TimeAlignSession` на временной БД в `QTemporaryDir` (по образцу `applehealthtests`):
   предпросмотр не пишет в БД; `commit` создаёт одну `offset`-строку; повторный `commit`
   заменяет (старая — `undone_at`); `0` удаляет; `cancel` возвращает исходное;
   `previousNightOffset` берёт ближайшую предыдущую; ограничение ±12 ч.
4. `snapStepMs/snapDelta/formatOffset` — таблица значений.

### Ручное тестирование — только на копии данных
Схема БД в `master` — v19, у установленной 2.0.1 — v17; новая сборка предлагает
необратимое обновление схемы. Поэтому:
1. `cp -R ~/Documents/OSCAR20_Data ~/Documents/OSCAR20_Data_dev` (оригинал не трогаем).
2. Запуск сборки только с `--datadir OSCAR20_Data_dev`.
3. Чек-лист: вход с кнопки и из меню; перетаскивание + плашка; округление при разных
   масштабах; кнопки и клавиши; Save → перезапуск → сдвиг на месте; строка в
   Data → Time Corrections; Cancel/Esc; смена даты с несохранённым; Same as last night;
   More options…; Drift Analysis видит выровненные ночи; поведение графиков вне режима
   не изменилось.
4. Сборка без предупреждений (`CONFIG += warn_on` локально для проверки новых файлов).

## 6. План коммитов и MR
Ветка `feature-daily-time-align` (только `[0-9a-zA-Z-]`, требование CONTRIBUTING).
1. `Share correction-row conversion and fix drift rows in Time Corrections preview`
2. `Add TimeAlignSession with unit tests`
3. `Add alignment mode to gGraphView`
4. `Add Align bar to Daily view`
5. (при необходимости) обновление справки/`docs`.

MR в `CrimsonNape/oscar-sql` → `master` через форк на GitLab; описание со скриншотами/gif;
без «Squash commits». Этот файл спецификации в MR не входит.

## 7. Вне рамок (YAGNI)
- Автоматическое выравнивание по корреляции SpO₂/событий.
- Перетаскивание CPAP как неэталонного устройства.
- Диапазоны дат прямо в полосе (есть в диалоге).
- Изменение порога предупреждения в существующем диалоге.
- Перенос сессии в другой день при больших сдвигах.
