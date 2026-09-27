# Выравнивание времени устройства в окне «День» — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Дать возможность визуально сдвигать время оксиметра (и других не-CPAP устройств) прямо на графиках окна «День» — перетаскиванием, кнопками и клавишами — с сохранением в существующую таблицу `device_time_corrections`.

**Architecture:** Логика редактирования однодневного смещения — в новом классе `TimeAlignSession` (без виджетов, тестируется юнит-тестами через подменяемое хранилище). `gGraphView` получает «режим выравнивания» (перетаскивание целевых графиков, клавиши, рамки), новая полоса `TimeAlignBar` — кнопки и цифру смещения, `Daily` связывает всё вместе. Хранение и применение поправок (`DeviceTimeCorrectionRepository`, `Machine::correctionMs`) уже есть в upstream и не меняются.

**Tech Stack:** C++17, Qt 6 (локально 6.11.2 из Homebrew), qmake, QtTest (свой раннер `tests/AutoTest.h`), SQLite через QtSql.

**Spec:** `docs/superpowers/specs/2026-09-27-daily-time-align-design.md`

## Global Constraints

- Репозиторий: `/Users/semyk/Downloads/Oscar_Project/oscar-sql`, ветка `feature-daily-time-align` (имя только `[0-9a-zA-Z-]`), remote `origin` = GitHub-форк; в `upstream` (GitLab) не пушить.
- Сборка приложения: `/Users/semyk/Downloads/Oscar_Project/build` (`make` сам перезапускает qmake при изменении `oscar.pro`).
- Сборка тестов: `/Users/semyk/Downloads/Oscar_Project/build-test` (qmake `CONFIG+=test`), бинарник `./test` прогоняет все наборы за ~2 с. **Исходное состояние: 3 падения в `EventsTabTests` (testDefaults, testOptions, testHtmlSummary) — существовали до нас, не чинить и не считать регрессией.**
- Без изменения схемы БД. Однодневный сдвиг = строка `type='offset'`, `date_from = date_to = ночь` через `DeviceTimeCorrectionRepository::upsertOffset`.
- Строки интерфейса — на английском через `tr()`; файлы `.ts` не трогать.
- Новые файлы — заголовок-комментарий как у соседних (`Copyright (c) 2026 The OSCAR Team`, GPL-абзац).
- Коммиты — на английском, в конце трейлер `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- `docs/superpowers/**` игнорируется `.gitignore` upstream — добавлять только `git add -f`, и в MR эти файлы не входят.
- Ручные проверки — **только** на копии данных: `open -n /Users/semyk/Downloads/Oscar_Project/build/OSCAR20.app --args --datadir /Users/semyk/Documents/OSCAR20_Data_dev`. Оригинал `~/Documents/OSCAR20_Data` не открывать нашей сборкой (схема v19 против v17 у установленной 2.0.1).
- Шаги кнопок: ±10 с / ±1 мин / ±10 мин / ±1 ч; клавиши ←/→ = 10 с, Shift = 1 мин; предел ±12 ч; предупреждение при |сдвиг| > 3 ч.
- Округление перетаскивания: `msPerPx > 20000` → 60 000 мс; `msPerPx > 2000` → 10 000 мс; иначе 1 000 мс.
- Выход/смена профиля/импорт/очистка с несохранённым сдвигом — только Save/Discard; смена даты/устройства — Save/Discard/Cancel.

## Review Focus

Случаи, которые подразумевает спецификация, но не покрывают юнит-тесты (GUI), — проверяются чек-листом Задачи 6:

1. Прокрутка списка графиков во время режима: захват перетаскивания должен срабатывать там, где целевой график нарисован сейчас, а не где был раньше (Задача 6, шаг 3, пункт «прокрутка»).
2. Закреплённый (pinned) график поверх прокрученного целевого: перетаскивание по закреплённому не должно двигать скрытый под ним (Задача 6, «pinned»).
3. `Esc` отменяет выравнивание, но не откатывает зум (Задача 6, «Esc»).
4. Смена даты календарём/кнопками ←/→ дня при несохранённом сдвиге: Cancel возвращает календарь на прежний день и сдвиг не теряется (Задача 6, «смена даты»).
5. Ночь только с оксиметром (без CPAP) и ночь без не-CPAP устройств: подсказка в полосе / кнопка скрыта (Задача 6, «без CPAP», «кнопка скрыта»).

Юнит-тестами дополнительно закреплены: NaN/нулевой масштаб в `snapDelta`, переход на другое устройство сбрасывает предпросмотр первого, «Same as last night» не берёт диапазонные строки, commit без изменений не пишет в БД.

---

### Task 0: Инструменты проверки предупреждений (вне репозитория)

Сборки Qt6 в этом проекте идут с `-w` (`CONFIG += warn_off`), поэтому предупреждения не видны. Скрипт перекомпилирует выбранные файлы с `-Wall -Wextra` в режиме `-fsyntax-only` и печатает предупреждения без номеров строк, чтобы сравнивать «до/после».

**Files:**
- Create: `/Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh` (вне репозитория, не коммитится)

- [ ] **Step 1: Создать скрипт**

```bash
mkdir -p /Users/semyk/Downloads/Oscar_Project/tools
cat > /Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh <<'EOF'
#!/bin/bash
# Usage: warncheck.sh <object-basename>...   (e.g. machine daily gGraphView)
# Prints "file: warning text" lines (no line numbers) for the given translation units.
cd /Users/semyk/Downloads/Oscar_Project/build || exit 1
for f in "$@"; do
  cmd=$(make -n -B "${f}.o" 2>/dev/null | grep -m1 'clang++ -c')
  if [ -z "$cmd" ]; then echo "no compile command for $f" >&2; continue; fi
  cmd=${cmd// -w / -Wall -Wextra -Wno-unused-parameter }
  cmd=${cmd// -Werror / }
  eval "$cmd -fsyntax-only" 2>&1 | grep -E ': warning:' | grep -v '/opt/homebrew/' \
    | sed -E 's#^.*/(oscar/[^:]+):[0-9]+:[0-9]+: #\1: #'
done | sort | uniq
EOF
chmod +x /Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh
```

- [ ] **Step 2: Снять базовую линию на текущем коде**

Run:
```bash
/Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh machine devicetimecorrectiondialog gGraphView daily mainwindow > /Users/semyk/Downloads/Oscar_Project/tools/warn-baseline.txt; wc -l /Users/semyk/Downloads/Oscar_Project/tools/warn-baseline.txt
```
Expected: файл создан (число строк любое — это предупреждения upstream).

---

### Task 1: Общий конвертер строки поправки + исправление предпросмотра диалога

**Files:**
- Modify: `oscar/SleepLib/machine.h` (объявление рядом с `correctionMs`, ~строка 228; forward-declare перед `class Machine`, ~строка 91)
- Modify: `oscar/SleepLib/machine.cpp:637-656` (`reloadCorrectionsFromDb`)
- Modify: `oscar/devicetimecorrectiondialog.cpp:413-427` (цикл в `previewStaged`)
- Test: `oscar/tests/machinetests.h`, `oscar/tests/machinetests.cpp`

**Interfaces:**
- Produces: `static TimeCorrectionRow Machine::rowFromData(const DeviceTimeCorrectionData& d);` — используется в Задаче 2.

- [ ] **Step 1: Написать падающие тесты**

В `oscar/tests/machinetests.h` в `private slots:` после `void testEmptyListDoesNotReport();` добавить:

```cpp
    void testRowFromDataCopiesFields();
    void testRowFromDataStripsLegacyDriftSentinel();
    void testCorrectionMsSumsRowsInRange();
    void testCorrectionMsAppliesDriftRow();
```

В `oscar/tests/machinetests.cpp` после `#include "SleepLib/schema.h"` добавить `#include "database/device_time_correction_repository.h"`, а в конец файла:

```cpp
// The shared DB-row -> TimeCorrectionRow conversion keeps every field.
void MachineTests::testRowFromDataCopiesFields()
{
    DeviceTimeCorrectionData d;
    d.dateFrom = QStringLiteral("2026-09-20");
    d.dateTo   = QString();                       // open-ended
    d.type     = QStringLiteral("travel");
    d.offsetMs = 3600000;
    d.c0Ms     = 5;
    d.c1       = 0.0;

    const TimeCorrectionRow r = Machine::rowFromData(d);
    QCOMPARE(r.dateFrom, QDate(2026, 9, 20));
    QVERIFY(r.dateTo.isNull());
    QCOMPARE(r.type, QStringLiteral("travel"));
    QCOMPARE(r.offsetMs, qint64(3600000));
    QCOMPARE(r.c0Ms, qint64(5));
    QCOMPARE(r.c1, 0.0);
}

// Drift rows were once stored with c1 = slope + 1.0; only drift rows carry that sentinel.
void MachineTests::testRowFromDataStripsLegacyDriftSentinel()
{
    DeviceTimeCorrectionData legacy;
    legacy.dateFrom = QStringLiteral("2026-01-01");
    legacy.dateTo   = QStringLiteral("2026-12-31");
    legacy.type     = QStringLiteral("drift");
    legacy.c1       = 1.5;
    QCOMPARE(Machine::rowFromData(legacy).c1, 0.5);

    DeviceTimeCorrectionData current = legacy;
    current.c1 = 0.25;
    QCOMPARE(Machine::rowFromData(current).c1, 0.25);

    DeviceTimeCorrectionData offset = legacy;
    offset.type = QStringLiteral("offset");
    offset.c1   = 1.5;
    QCOMPARE(Machine::rowFromData(offset).c1, 1.5);
}

void MachineTests::testCorrectionMsSumsRowsInRange()
{
    Machine mach(nullptr, 4);

    TimeCorrectionRow range;                     // open-ended range
    range.dateFrom = QDate(2026, 9, 1);
    range.type     = QStringLiteral("travel");
    range.offsetMs = 3600000;

    TimeCorrectionRow night;                     // single night
    night.dateFrom = night.dateTo = QDate(2026, 9, 24);
    night.type     = QStringLiteral("offset");
    night.offsetMs = 600000;

    TimeCorrectionRow august;
    august.dateFrom = QDate(2026, 8, 1);
    august.dateTo   = QDate(2026, 8, 31);
    august.type     = QStringLiteral("offset");
    august.offsetMs = 999;

    mach.rebuildCorrections({ range, night, august });
    QCOMPARE(mach.correctionMs(QDate(2026, 9, 24)), qint64(4200000));
    QCOMPARE(mach.correctionMs(QDate(2026, 9, 25)), qint64(3600000));
    QCOMPARE(mach.correctionMs(QDate(2026, 8, 15)), qint64(999));
    QCOMPARE(mach.correctionMs(QDate(2026, 7, 1)),  qint64(0));
}

// A drift row with no slope subtracts its intercept.
void MachineTests::testCorrectionMsAppliesDriftRow()
{
    Machine mach(nullptr, 5);
    TimeCorrectionRow drift;
    drift.dateFrom = QDate(2026, 9, 1);
    drift.type     = QStringLiteral("drift");
    drift.c0Ms     = 2000;
    drift.c1       = 0.0;
    mach.rebuildCorrections({ drift });
    QCOMPARE(mach.correctionMs(QDate(2026, 9, 24)), qint64(-2000));
}
```

- [ ] **Step 2: Убедиться, что сборка тестов падает**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:' | head -5`
Expected: ошибка вида `no member named 'rowFromData' in 'Machine'`.

- [ ] **Step 3: Реализация**

В `oscar/SleepLib/machine.h` перед строкой `class MachineLoader;    // forward` добавить:

```cpp
struct DeviceTimeCorrectionData;
```

И сразу после `static void reloadCorrectionsFromDb(Machine* mach);`:

```cpp
    //! \brief Converts a device_time_corrections row to the in-memory form used by correctionMs().
    //!        Strips the legacy drift sentinel (drift rows were once stored with c1 = slope + 1.0).
    static TimeCorrectionRow rowFromData(const DeviceTimeCorrectionData& d);
```

В `oscar/SleepLib/machine.cpp` заменить целиком функцию `Machine::reloadCorrectionsFromDb` на:

```cpp
TimeCorrectionRow Machine::rowFromData(const DeviceTimeCorrectionData& d)
{
    TimeCorrectionRow r;
    r.dateFrom = QDate::fromString(d.dateFrom, Qt::ISODate);
    r.dateTo   = d.dateTo.isEmpty() ? QDate() : QDate::fromString(d.dateTo, Qt::ISODate);
    r.type     = d.type;
    r.offsetMs = d.offsetMs;
    r.c0Ms     = d.c0Ms;
    // Backward compat: drift rows were previously stored with c1 = slope + 1.0.
    // Detect old format (c1 >= 1.0 on a drift row) and strip the sentinel.
    r.c1 = (d.type == "drift" && d.c1 >= 1.0) ? d.c1 - 1.0 : d.c1;
    return r;
}

void Machine::reloadCorrectionsFromDb(Machine* mach)
{
    DeviceTimeCorrectionRepository repo;
    QList<TimeCorrectionRow> rows;
    for (const auto& d : repo.findActive(mach->getDatabaseId())) {
        rows.append(rowFromData(d));
    }
    mach->rebuildCorrections(rows);
}
```

В `oscar/devicetimecorrectiondialog.cpp` в `previewStaged` заменить блок после `if (exclude) continue;`:

```cpp
        if (exclude) continue;
        TimeCorrectionRow r;
        r.dateFrom = QDate::fromString(d.dateFrom, Qt::ISODate);
        r.dateTo   = d.dateTo.isEmpty() ? QDate() : QDate::fromString(d.dateTo, Qt::ISODate);
        r.offsetMs = d.offsetMs;
        r.c0Ms     = d.c0Ms;
        r.c1       = d.c1;
        rows.append(r);
```

на:

```cpp
        if (exclude) continue;
        // Shared conversion: keeps the row type, so drift rows preview as drift, not as offsets.
        rows.append(Machine::rowFromData(d));
```

- [ ] **Step 4: Прогнать тесты**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; ./test 2>&1 | grep -E 'MachineTests|^FAIL'`
Expected: 4 новых `PASS   : MachineTests::test…`, в `FAIL` — только 3 известных `EventsTabTests`.

- [ ] **Step 5: Собрать приложение**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:|make.*Error'; echo done`
Expected: только `done`.

- [ ] **Step 6: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/SleepLib/machine.h oscar/SleepLib/machine.cpp oscar/devicetimecorrectiondialog.cpp oscar/tests/machinetests.h oscar/tests/machinetests.cpp
git commit -m "Share correction-row conversion and fix drift rows in Time Corrections preview

The Time Corrections dialog built its preview rows without the row type
and without stripping the legacy drift sentinel, so drift corrections
previewed as constant offsets. Move the conversion into
Machine::rowFromData() and use it for both the preview and the reload.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `TimeAlignSession` — логика однодневного сдвига с предпросмотром

**Files:**
- Create: `oscar/timealignsession.h`, `oscar/timealignsession.cpp`
- Create: `oscar/tests/timealignsessiontests.h`, `oscar/tests/timealignsessiontests.cpp`
- Modify: `oscar/oscar.pro` (SOURCES после `driftanalysisdialog.cpp \` ~стр. 317; HEADERS после `driftanalysisdialog.h \` ~стр. 504; тестовый блок `test { … }` ~стр. 822-852)

**Interfaces:**
- Consumes: `Machine::rowFromData` (Задача 1), `Machine::rebuildCorrections`, `Machine::correctionMs`, `Machine::getDatabaseId`, `DeviceTimeCorrectionRepository::{findActive, findManualOffsetRows, upsertOffset}`.
- Produces (для Задач 4–5):
  - `class TimeAlignStore` (абстрактный: `findActive`, `findManualOffsetRows`, `bool upsertOffset(qint64, const QString&, qint64)`), `class RepositoryTimeAlignStore : public TimeAlignStore`
  - `class TimeAlignSession : public QObject` c `static constexpr qint64 kMaxOffsetMs, kLargeOffsetMs`; `explicit TimeAlignSession(TimeAlignStore* store = nullptr, QObject* parent = nullptr)`; `bool begin(Machine*, const QDate&)`; `void end()`; `bool isActive() const`; `Machine* machine() const`; `QDate night() const`; `qint64 offsetMs() const`; `qint64 savedMs() const`; `bool isDirty() const`; `qint64 otherCorrectionsMs() const`; `std::optional<qint64> previousNightOffset() const`; `void setOffsetMs(qint64)`; `void nudge(qint64)`; `bool commit()`; `void cancel()`; `static qint64 snapStepMs(double)`; `static qint64 snapDelta(double, double)`; `static QString formatOffset(qint64)`; сигнал `void offsetChanged(qint64 ms)`.

- [ ] **Step 1: Заголовок `oscar/timealignsession.h`** (нужен тестам для компиляции)

```cpp
/* Time Alignment Session Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef TIMEALIGNSESSION_H
#define TIMEALIGNSESSION_H

#include <QObject>
#include <QDate>
#include <QList>
#include <QString>
#include <memory>
#include <optional>

#include "database/device_time_correction_repository.h"

class Machine;

//! \brief Storage for single-night alignment offsets. The default implementation wraps
//!        DeviceTimeCorrectionRepository; unit tests substitute an in-memory store.
class TimeAlignStore
{
public:
    virtual ~TimeAlignStore() = default;
    virtual QList<DeviceTimeCorrectionData> findActive(qint64 machineId) = 0;
    virtual QList<DeviceTimeCorrectionData> findManualOffsetRows(qint64 machineId) = 0;
    //! \brief Replaces the single-night 'offset' row for \a date; 0 removes it.
    //! \return false on a database error.
    virtual bool upsertOffset(qint64 machineId, const QString& date, qint64 offsetMs) = 0;
};

class RepositoryTimeAlignStore : public TimeAlignStore
{
public:
    QList<DeviceTimeCorrectionData> findActive(qint64 machineId) override;
    QList<DeviceTimeCorrectionData> findManualOffsetRows(qint64 machineId) override;
    bool upsertOffset(qint64 machineId, const QString& date, qint64 offsetMs) override;

private:
    DeviceTimeCorrectionRepository m_repo;
};

/*! \class TimeAlignSession
    \brief Edits one device's single-night time offset with a live, in-memory preview.

    The value being edited is the device's 'offset' row whose date_from and date_to are both
    the night. Other active corrections for the device keep applying and add to it, exactly as
    in Machine::correctionMs(). Nothing is written until commit(); cancel() restores the stored
    corrections. */
class TimeAlignSession : public QObject
{
    Q_OBJECT
public:
    static constexpr qint64 kMaxOffsetMs   = 12LL * 3600 * 1000;
    static constexpr qint64 kLargeOffsetMs = 3LL * 3600 * 1000;

    //! \param store Storage to use; nullptr uses the database repository. The session owns it.
    explicit TimeAlignSession(TimeAlignStore* store = nullptr, QObject* parent = nullptr);
    ~TimeAlignSession() override;

    //! \brief Starts editing \a mach on \a night, dropping any unsaved preview first.
    //! \return false if the machine has no database id (its corrections cannot be stored).
    bool begin(Machine* mach, const QDate& night);
    //! \brief Stops editing without touching the machine's corrections (commit or cancel first).
    void end();

    bool     isActive() const { return m_machine != nullptr; }
    Machine* machine() const { return m_machine; }
    QDate    night() const { return m_night; }
    qint64   offsetMs() const { return m_offsetMs; }
    qint64   savedMs() const { return m_savedMs; }
    bool     isDirty() const { return isActive() && (m_offsetMs != m_savedMs); }

    //! \brief Sum of the device's other active corrections on this night.
    qint64 otherCorrectionsMs() const;
    //! \brief The single-night offset of the nearest earlier night, if any.
    std::optional<qint64> previousNightOffset() const;

    //! \brief Previews \a ms (clamped to +/-kMaxOffsetMs) as this night's offset.
    void setOffsetMs(qint64 ms);
    void nudge(qint64 deltaMs) { setOffsetMs(m_offsetMs + deltaMs); }

    //! \brief Stores the previewed offset. On failure the preview stays and false is returned.
    bool commit();
    //! \brief Drops the preview and restores the stored corrections.
    void cancel();

    //! \brief Drag rounding step for the given zoom: 1 min, 10 s or 1 s.
    static qint64 snapStepMs(double msPerPx);
    //! \brief Rounds a raw drag distance to the step for this zoom.
    static qint64 snapDelta(double rawDeltaMs, double msPerPx);
    //! \brief Formats an offset as "+HH:MM:SS" / "-HH:MM:SS".
    static QString formatOffset(qint64 ms);

signals:
    void offsetChanged(qint64 ms);

private:
    bool isThisNightsOffsetRow(const DeviceTimeCorrectionData& d) const;
    void applyPreview();
    void restoreStored(Machine* mach);

    std::unique_ptr<TimeAlignStore> m_store;
    Machine* m_machine = nullptr;
    QDate    m_night;
    qint64   m_savedMs  = 0;
    qint64   m_offsetMs = 0;
    QList<DeviceTimeCorrectionData> m_otherRows;   //!< active rows except this night's own offset row
};

#endif // TIMEALIGNSESSION_H
```

- [ ] **Step 2: Написать тесты `oscar/tests/timealignsessiontests.h`**

```cpp
/* Time Alignment Session Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class QCoreApplication;
class QTemporaryDir;

class TimeAlignSessionTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testBeginReadsSavedAndOtherCorrections();
    void testBeginRejectsMachineWithoutDatabaseId();
    void testPreviewDoesNotWrite();
    void testCommitReplacesNightRow();
    void testCommitWithoutChangeDoesNotWrite();
    void testZeroCommitRemovesNightRow();
    void testCancelRestoresStoredCorrections();
    void testCommitFailureKeepsPreview();
    void testBeginOnAnotherMachineRestoresFirst();
    void testPreviousNightOffset();
    void testOffsetIsClamped();
    void testSnapStep();
    void testSnapDelta();
    void testFormatOffset();
    void testRepositoryStoreRoundTrip();
    void cleanupTestCase();

private:
    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
};
DECLARE_TEST(TimeAlignSessionTests)
```

- [ ] **Step 3: Написать тесты `oscar/tests/timealignsessiontests.cpp`**

```cpp
/* Time Alignment Session Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "timealignsessiontests.h"
#include "timealignsession.h"
#include "SleepLib/machine.h"
#include "database/database_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <cmath>

namespace {

const QDate kNight(2026, 9, 24);
const qint64 kOxiDbId = 10;

DeviceTimeCorrectionData row(qint64 machineId, const QString& from, const QString& to,
                             const QString& type, qint64 offsetMs)
{
    DeviceTimeCorrectionData d;
    d.machineId = machineId;
    d.dateFrom  = from;
    d.dateTo    = to;
    d.type      = type;
    d.offsetMs  = offsetMs;
    return d;
}

// In-memory stand-in for the repository with the same upsert semantics
// (the previous row for the night is marked undone, 0 only removes).
class FakeStore : public TimeAlignStore
{
public:
    QList<DeviceTimeCorrectionData> rows;
    int  writes = 0;
    bool failWrites = false;

    QList<DeviceTimeCorrectionData> findActive(qint64 machineId) override
    {
        QList<DeviceTimeCorrectionData> out;
        for (const auto& d : rows) {
            if (d.machineId == machineId && d.undoneAt.isEmpty()) out.append(d);
        }
        return out;
    }

    QList<DeviceTimeCorrectionData> findManualOffsetRows(qint64 machineId) override
    {
        QList<DeviceTimeCorrectionData> out;
        for (const auto& d : findActive(machineId)) {
            if (d.type == QLatin1String("offset") && d.dateFrom == d.dateTo) out.append(d);
        }
        return out;
    }

    bool upsertOffset(qint64 machineId, const QString& date, qint64 offsetMs) override
    {
        if (failWrites) return false;
        ++writes;
        for (auto& d : rows) {
            if (d.machineId == machineId && d.undoneAt.isEmpty() && d.type == QLatin1String("offset")
                    && d.dateFrom == date && d.dateTo == date) {
                d.undoneAt = QStringLiteral("undone");
            }
        }
        if (offsetMs != 0) rows.append(row(machineId, date, date, QStringLiteral("offset"), offsetMs));
        return true;
    }

    QList<DeviceTimeCorrectionData> nightRows(qint64 machineId, const QString& date)
    {
        QList<DeviceTimeCorrectionData> out;
        for (const auto& d : findManualOffsetRows(machineId)) {
            if (d.dateFrom == date) out.append(d);
        }
        return out;
    }
};

} // namespace

void TimeAlignSessionTests::initTestCase()
{
    // QtSql needs an application object; other suites delete theirs in cleanupTestCase().
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QCoreApplication(argc, argv);
    }
}

void TimeAlignSessionTests::testBeginReadsSavedAndOtherCorrections()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-24", "2026-09-24", "offset", 600000),
                    row(kOxiDbId, "2026-09-01", "", "travel", 3600000),
                    row(99, "2026-09-24", "2026-09-24", "offset", 5) };   // another device
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);

    QVERIFY(s.begin(&mach, kNight));
    QVERIFY(s.isActive());
    QCOMPARE(s.savedMs(), qint64(600000));
    QCOMPARE(s.offsetMs(), qint64(600000));
    QVERIFY(!s.isDirty());
    QCOMPARE(s.otherCorrectionsMs(), qint64(3600000));
    QCOMPARE(mach.correctionMs(kNight), qint64(4200000));
}

void TimeAlignSessionTests::testBeginRejectsMachineWithoutDatabaseId()
{
    TimeAlignSession s(new FakeStore);
    Machine mach(nullptr, 2);                     // never saved: database id 0
    QVERIFY(!s.begin(&mach, kNight));
    QVERIFY(!s.isActive());
}

void TimeAlignSessionTests::testPreviewDoesNotWrite()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-01", "", "travel", 3600000) };
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    QSignalSpy spy(&s, &TimeAlignSession::offsetChanged);
    s.setOffsetMs(900000);

    QCOMPARE(store->writes, 0);
    QVERIFY(s.isDirty());
    QCOMPARE(mach.correctionMs(kNight), qint64(4500000));
    QCOMPARE(mach.correctionMs(kNight.addDays(1)), qint64(3600000));   // other nights untouched
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toLongLong(), qint64(900000));
}

void TimeAlignSessionTests::testCommitReplacesNightRow()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-24", "2026-09-24", "offset", 600000) };
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    s.setOffsetMs(900000);
    QVERIFY(s.commit());

    QCOMPARE(store->writes, 1);
    const auto rows = store->nightRows(kOxiDbId, "2026-09-24");
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().offsetMs, qint64(900000));
    QVERIFY(!s.isDirty());
    QCOMPARE(s.savedMs(), qint64(900000));
    QCOMPARE(mach.correctionMs(kNight), qint64(900000));
}

void TimeAlignSessionTests::testCommitWithoutChangeDoesNotWrite()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-24", "2026-09-24", "offset", 600000) };
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    QVERIFY(s.commit());
    QCOMPARE(store->writes, 0);
}

void TimeAlignSessionTests::testZeroCommitRemovesNightRow()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-24", "2026-09-24", "offset", 600000) };
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    s.setOffsetMs(0);
    QVERIFY(s.commit());
    QVERIFY(store->nightRows(kOxiDbId, "2026-09-24").isEmpty());
    QCOMPARE(mach.correctionMs(kNight), qint64(0));
}

void TimeAlignSessionTests::testCancelRestoresStoredCorrections()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-24", "2026-09-24", "offset", 600000),
                    row(kOxiDbId, "2026-09-01", "", "travel", 3600000) };
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    s.setOffsetMs(-300000);
    QCOMPARE(mach.correctionMs(kNight), qint64(3300000));
    s.cancel();

    QCOMPARE(s.offsetMs(), qint64(600000));
    QVERIFY(!s.isDirty());
    QCOMPARE(mach.correctionMs(kNight), qint64(4200000));
    QCOMPARE(store->writes, 0);
}

void TimeAlignSessionTests::testCommitFailureKeepsPreview()
{
    auto *store = new FakeStore;
    store->failWrites = true;
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    s.setOffsetMs(900000);
    QVERIFY(!s.commit());
    QVERIFY(s.isDirty());
    QCOMPARE(mach.correctionMs(kNight), qint64(900000));
}

void TimeAlignSessionTests::testBeginOnAnotherMachineRestoresFirst()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-24", "2026-09-24", "offset", 600000) };
    TimeAlignSession s(store);
    Machine oxi(nullptr, 1);
    oxi.setDatabaseId(kOxiDbId);
    Machine other(nullptr, 2);
    other.setDatabaseId(11);

    QVERIFY(s.begin(&oxi, kNight));
    s.setOffsetMs(1200000);
    QCOMPARE(oxi.correctionMs(kNight), qint64(1200000));

    QVERIFY(s.begin(&other, kNight));
    QCOMPARE(oxi.correctionMs(kNight), qint64(600000));   // first device's preview dropped
    QCOMPARE(s.machine(), &other);
    QCOMPARE(store->writes, 0);
}

void TimeAlignSessionTests::testPreviousNightOffset()
{
    auto *store = new FakeStore;
    store->rows = { row(kOxiDbId, "2026-09-20", "2026-09-20", "offset", 300000),
                    row(kOxiDbId, "2026-09-22", "2026-09-22", "offset", -120000),
                    row(kOxiDbId, "2026-09-23", "", "travel", 3600000),     // ranges don't count
                    row(kOxiDbId, "2026-09-25", "2026-09-25", "offset", 999) };
    TimeAlignSession s(store);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);

    QVERIFY(s.begin(&mach, kNight));
    QVERIFY(s.previousNightOffset().has_value());
    QCOMPARE(*s.previousNightOffset(), qint64(-120000));

    QVERIFY(s.begin(&mach, QDate(2026, 9, 19)));
    QVERIFY(!s.previousNightOffset().has_value());
}

void TimeAlignSessionTests::testOffsetIsClamped()
{
    TimeAlignSession s(new FakeStore);
    Machine mach(nullptr, 1);
    mach.setDatabaseId(kOxiDbId);
    QVERIFY(s.begin(&mach, kNight));

    s.setOffsetMs(13LL * 3600 * 1000);
    QCOMPARE(s.offsetMs(), TimeAlignSession::kMaxOffsetMs);
    s.nudge(-30LL * 3600 * 1000);
    QCOMPARE(s.offsetMs(), -TimeAlignSession::kMaxOffsetMs);
    s.setOffsetMs(0);
    s.nudge(600000);
    s.nudge(600000);
    QCOMPARE(s.offsetMs(), qint64(1200000));
}

void TimeAlignSessionTests::testSnapStep()
{
    QCOMPARE(TimeAlignSession::snapStepMs(30000.0), qint64(60000));   // whole night on screen
    QCOMPARE(TimeAlignSession::snapStepMs(20001.0), qint64(60000));
    QCOMPARE(TimeAlignSession::snapStepMs(5000.0),  qint64(10000));   // 1-2 hours on screen
    QCOMPARE(TimeAlignSession::snapStepMs(2000.0),  qint64(1000));
    QCOMPARE(TimeAlignSession::snapStepMs(100.0),   qint64(1000));
    QCOMPARE(TimeAlignSession::snapStepMs(0.0),     qint64(1000));
}

void TimeAlignSessionTests::testSnapDelta()
{
    QCOMPARE(TimeAlignSession::snapDelta(95000.0, 30000.0),  qint64(120000));
    QCOMPARE(TimeAlignSession::snapDelta(-95000.0, 30000.0), qint64(-120000));
    QCOMPARE(TimeAlignSession::snapDelta(14999.0, 5000.0),   qint64(10000));
    QCOMPARE(TimeAlignSession::snapDelta(-1499.0, 100.0),    qint64(-1000));
    QCOMPARE(TimeAlignSession::snapDelta(0.0, 0.0),          qint64(0));
    QCOMPARE(TimeAlignSession::snapDelta(std::nan(""), 100.0), qint64(0));
}

void TimeAlignSessionTests::testFormatOffset()
{
    QCOMPARE(TimeAlignSession::formatOffset(0),        QStringLiteral("+00:00:00"));
    QCOMPARE(TimeAlignSession::formatOffset(630000),   QStringLiteral("+00:10:30"));
    QCOMPARE(TimeAlignSession::formatOffset(-3600000), QStringLiteral("-01:00:00"));
    QCOMPARE(TimeAlignSession::formatOffset(TimeAlignSession::kMaxOffsetMs), QStringLiteral("+12:00:00"));
}

// The real repository, against a fresh temporary database.
void TimeAlignSessionTests::testRepositoryStoreRoundTrip()
{
    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }
    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-timealigntests-XXXXXX"));
    QVERIFY(m_tempDir->isValid());
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));

    QSqlQuery q(DatabaseManager::instance().database());
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO profiles (username, data_folder) VALUES ('timealign', '/tmp/timealign')")));
    const qint64 profileId = q.lastInsertId().toLongLong();
    QVERIFY(q.prepare(QStringLiteral(
        "INSERT INTO machines (profile_id, machine_id, loader_name, machine_type) "
        "VALUES (:profile_id, 1, 'MD300W1', :type)")));
    q.bindValue(QStringLiteral(":profile_id"), profileId);
    q.bindValue(QStringLiteral(":type"), int(MT_OXIMETER));
    QVERIFY(q.exec());
    const qint64 machineId = q.lastInsertId().toLongLong();

    RepositoryTimeAlignStore store;
    QVERIFY(store.upsertOffset(machineId, QStringLiteral("2026-09-24"), 600000));
    QVERIFY(store.upsertOffset(machineId, QStringLiteral("2026-09-24"), 900000));
    const auto active = store.findActive(machineId);
    QCOMPARE(active.size(), 1);
    QCOMPARE(active.first().offsetMs, qint64(900000));
    QCOMPARE(store.findManualOffsetRows(machineId).size(), 1);

    QVERIFY(store.upsertOffset(machineId, QStringLiteral("2026-09-24"), 0));
    QVERIFY(store.findActive(machineId).isEmpty());
}

void TimeAlignSessionTests::cleanupTestCase()
{
    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }
    delete m_tempDir;
    m_tempDir = nullptr;
    delete m_app;
    m_app = nullptr;
}
```

- [ ] **Step 4: Подключить файлы в `oscar/oscar.pro`**

В основной `SOURCES` после строки `    driftanalysisdialog.cpp \` добавить `    timealignsession.cpp \`.
В основной `HEADERS` после строки `    driftanalysisdialog.h \` добавить `    timealignsession.h \`.
В блоке `test {`: заменить `        tests/machinetests.cpp` на
```
        tests/machinetests.cpp \
        tests/timealignsessiontests.cpp
```
и `        tests/machinetests.h` на
```
        tests/machinetests.h \
        tests/timealignsessiontests.h
```

- [ ] **Step 5: Убедиться, что сборка тестов падает на линковке**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E 'error|Undefined' | head -5`
Expected: `Undefined symbols … TimeAlignSession::…` (реализации ещё нет; `timealignsession.cpp` пока не существует — make сообщит `No rule to make target 'timealignsession.cpp'`, это тоже ожидаемо).

- [ ] **Step 6: Реализация `oscar/timealignsession.cpp`**

```cpp
/* Time Alignment Session Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "timealignsession.h"
#include "SleepLib/machine.h"
#include "database/database_manager.h"

#include <QSqlDatabase>
#include <QtGlobal>
#include <cmath>

// ---------------------------------------------------------------------------
// RepositoryTimeAlignStore
// ---------------------------------------------------------------------------

QList<DeviceTimeCorrectionData> RepositoryTimeAlignStore::findActive(qint64 machineId)
{
    return m_repo.findActive(machineId);
}

QList<DeviceTimeCorrectionData> RepositoryTimeAlignStore::findManualOffsetRows(qint64 machineId)
{
    return m_repo.findManualOffsetRows(machineId);
}

bool RepositoryTimeAlignStore::upsertOffset(qint64 machineId, const QString& date, qint64 offsetMs)
{
    // upsertOffset() marks the old row undone before inserting the new one: keep both steps
    // atomic so a failed insert cannot lose the previous offset. If a transaction is already
    // open elsewhere, transaction() fails and we simply run inside it.
    QSqlDatabase db = DatabaseManager::instance().database();
    const bool ownTransaction = db.transaction();
    const bool ok = m_repo.upsertOffset(machineId, date, offsetMs) >= 0;
    if (ownTransaction) {
        if (ok && db.commit()) return true;
        db.rollback();
        return false;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// TimeAlignSession
// ---------------------------------------------------------------------------

TimeAlignSession::TimeAlignSession(TimeAlignStore* store, QObject* parent)
    : QObject(parent)
    , m_store(store ? store : new RepositoryTimeAlignStore)
{
}

TimeAlignSession::~TimeAlignSession() = default;

bool TimeAlignSession::isThisNightsOffsetRow(const DeviceTimeCorrectionData& d) const
{
    const QString night = m_night.toString(Qt::ISODate);
    return d.type == QLatin1String("offset") && d.dateFrom == night && d.dateTo == night;
}

bool TimeAlignSession::begin(Machine* mach, const QDate& night)
{
    if (isDirty()) restoreStored(m_machine);
    end();
    if (!mach || !night.isValid() || mach->getDatabaseId() <= 0) return false;

    m_machine = mach;
    m_night   = night;
    for (const auto& d : m_store->findActive(mach->getDatabaseId())) {
        if (isThisNightsOffsetRow(d)) m_savedMs = d.offsetMs;
        else                          m_otherRows.append(d);
    }
    m_offsetMs = m_savedMs;
    restoreStored(mach);   // in-memory corrections must match the store before previewing
    return true;
}

void TimeAlignSession::end()
{
    m_machine  = nullptr;
    m_night    = QDate();
    m_savedMs  = 0;
    m_offsetMs = 0;
    m_otherRows.clear();
}

qint64 TimeAlignSession::otherCorrectionsMs() const
{
    if (!m_machine) return 0;
    // The machine's in-memory corrections are always "other rows + this night's value".
    return m_machine->correctionMs(m_night) - m_offsetMs;
}

std::optional<qint64> TimeAlignSession::previousNightOffset() const
{
    if (!m_machine) return std::nullopt;
    std::optional<qint64> result;
    QDate best;
    for (const auto& d : m_store->findManualOffsetRows(m_machine->getDatabaseId())) {
        const QDate date = QDate::fromString(d.dateFrom, Qt::ISODate);
        if (!date.isValid() || date >= m_night) continue;
        if (!best.isValid() || date > best) {
            best   = date;
            result = d.offsetMs;
        }
    }
    return result;
}

void TimeAlignSession::setOffsetMs(qint64 ms)
{
    if (!m_machine) return;
    ms = qBound(-kMaxOffsetMs, ms, kMaxOffsetMs);
    if (ms == m_offsetMs) return;
    m_offsetMs = ms;
    applyPreview();
    emit offsetChanged(m_offsetMs);
}

bool TimeAlignSession::commit()
{
    if (!m_machine) return false;
    if (!isDirty()) return true;
    if (!m_store->upsertOffset(m_machine->getDatabaseId(), m_night.toString(Qt::ISODate), m_offsetMs)) {
        return false;
    }
    m_savedMs = m_offsetMs;
    restoreStored(m_machine);
    return true;
}

void TimeAlignSession::cancel()
{
    if (!m_machine) return;
    const bool changed = (m_offsetMs != m_savedMs);
    m_offsetMs = m_savedMs;
    restoreStored(m_machine);
    if (changed) emit offsetChanged(m_offsetMs);
}

void TimeAlignSession::applyPreview()
{
    QList<TimeCorrectionRow> rows;
    rows.reserve(m_otherRows.size() + 1);
    for (const auto& d : m_otherRows) {
        rows.append(Machine::rowFromData(d));
    }
    if (m_offsetMs != 0) {
        TimeCorrectionRow r;
        r.dateFrom = r.dateTo = m_night;
        r.type     = QStringLiteral("offset");
        r.offsetMs = m_offsetMs;
        rows.append(r);
    }
    m_machine->rebuildCorrections(rows);
}

void TimeAlignSession::restoreStored(Machine* mach)
{
    QList<TimeCorrectionRow> rows;
    for (const auto& d : m_store->findActive(mach->getDatabaseId())) {
        rows.append(Machine::rowFromData(d));
    }
    mach->rebuildCorrections(rows);
}

qint64 TimeAlignSession::snapStepMs(double msPerPx)
{
    if (msPerPx > 20000.0) return 60000;
    if (msPerPx > 2000.0)  return 10000;
    return 1000;
}

qint64 TimeAlignSession::snapDelta(double rawDeltaMs, double msPerPx)
{
    if (!std::isfinite(rawDeltaMs)) return 0;
    const qint64 step = snapStepMs(msPerPx);
    return qRound64(rawDeltaMs / double(step)) * step;
}

QString TimeAlignSession::formatOffset(qint64 ms)
{
    const QChar sign = (ms < 0) ? QLatin1Char('-') : QLatin1Char('+');
    const qint64 s = qAbs(ms) / 1000;
    return QStringLiteral("%1%2:%3:%4")
        .arg(sign)
        .arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}
```

- [ ] **Step 7: Прогнать тесты**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; ./test 2>&1 | grep -E 'TimeAlignSessionTests|^FAIL'`
Expected: все `PASS   : TimeAlignSessionTests::…` (17 строк вместе с init/cleanup), в `FAIL` — только 3 известных `EventsTabTests`.

- [ ] **Step 8: Собрать приложение и проверить предупреждения**

Run:
```bash
cd /Users/semyk/Downloads/Oscar_Project/build && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:|make.*Error'; /Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh timealignsession
```
Expected: пусто.

- [ ] **Step 9: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/timealignsession.h oscar/timealignsession.cpp oscar/tests/timealignsessiontests.h oscar/tests/timealignsessiontests.cpp oscar/oscar.pro
git commit -m "Add TimeAlignSession for previewing and saving a device's nightly time offset

Edits the single-night 'offset' row of one device with a live in-memory
preview (Machine::rebuildCorrections), writes it through
DeviceTimeCorrectionRepository::upsertOffset on commit and restores the
stored corrections on cancel. Includes drag-snapping and formatting
helpers and unit tests (in-memory store plus a temporary-database
round trip).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Режим выравнивания в `gGraphView`

Движок графиков остаётся общим: он ничего не знает о поправках, только сообщает «на сколько миллисекунд утащили» и рисует рамки. Автотестов для него нет (GUI); проверка — сборка, отсутствие регрессий в тестах и ручной чек-лист Задачи 6.

**Files:**
- Modify: `oscar/Graphs/gGraphView.h` (includes; public API после `void populateMenu(gGraph *);` ~стр. 588; блок `signals:` ~стр. 766; `protected slots:` ~стр. 794; члены после `int m_graph_index;` ~стр. 710)
- Modify: `oscar/Graphs/gGraphView.cpp` (конструктор ~стр. 561; `renderGraphs` ~стр. 1415-1600; `mouseMoveEvent` ~стр. 1884; `populateMenu` ~стр. 2308; `mousePressEvent` ~стр. 2795; `mouseReleaseEvent` ~стр. 2984; `keyReleaseEvent` ~стр. 3108; `keyPressEvent` ~стр. 3440)

**Interfaces:**
- Produces (для Задачи 5):
  - `void gGraphView::setAlignMode(bool on, const QSet<QString>& targetGraphNames = QSet<QString>());`
  - `bool gGraphView::alignMode() const;`
  - `void gGraphView::showAlignLabel(const QString& text);`
  - `void gGraphView::setAlignMenuPredicate(std::function<bool(gGraph*)> predicate);`
  - сигналы: `alignDragStarted()`, `alignDragMoved(double rawDeltaMs, double msPerPx)`, `alignDragFinished()`, `alignNudge(qint64 deltaMs)`, `alignAccept()`, `alignCancel()`, `alignRequestedForGraph(gGraph* graph)`.
  - Имена целевых графиков — `gGraph::name()` (в Daily это `schema::channel[code].code()`).

- [ ] **Step 1: Заголовок**

В `oscar/Graphs/gGraphView.h` к include'ам добавить (рядом с остальными Qt-заголовками):

```cpp
#include <QSet>
#include <functional>
```

После строки `    void populateMenu(gGraph *);` добавить:

```cpp
    //! \brief Turns time-alignment mode on or off. In this mode a left-drag on a graph named in
    //!        \a targetGraphNames reports a horizontal shift (alignDrag* signals) instead of
    //!        selecting/zooming; arrow keys, Enter and Esc report alignNudge/alignAccept/alignCancel.
    void setAlignMode(bool on, const QSet<QString>& targetGraphNames = QSet<QString>());
    bool alignMode() const { return m_alignMode; }
    //! \brief Shows \a text next to the mouse pointer (used while an alignment drag is in progress).
    void showAlignLabel(const QString& text);
    //! \brief Decides for which graphs the context menu offers "Align device time...".
    void setAlignMenuPredicate(std::function<bool(gGraph*)> predicate) { m_alignMenuPredicate = std::move(predicate); }
```

В блок `signals:` (после `void XBoundsChanged(qint64 ,qint64);`) добавить:

```cpp
    void alignDragStarted();
    void alignDragMoved(double rawDeltaMs, double msPerPx);
    void alignDragFinished();
    void alignNudge(qint64 deltaMs);
    void alignAccept();
    void alignCancel();
    void alignRequestedForGraph(gGraph *graph);
```

В `protected slots:` (после `void onSnapshotGraphToggle();`) добавить:

```cpp
    void onAlignAction();
```

После строки `    int m_graph_index;` добавить:

```cpp
    // Time-alignment mode (see setAlignMode())
    gGraph *alignGraphAt(const QPoint &pos) const;
    void noteAlignTarget(gGraph *g);
    void paintAlignFrames(QPainter &painter);
    bool m_alignMode = false;
    QSet<QString> m_alignTargets;
    QList<QPair<gGraph *, QRect>> m_alignPainted;   //!< plot rects of target graphs painted in the last frame
    bool m_alignDragging = false;
    int m_alignDragStartX = 0;
    double m_alignMsPerPx = 0.0;
    std::function<bool(gGraph*)> m_alignMenuPredicate;
    QAction *align_action = nullptr;
    gGraph *m_alignMenuGraph = nullptr;
```

- [ ] **Step 2: Пункт контекстного меню**

В конструкторе `gGraphView.cpp` после строки
`    snap_action = context_menu->addAction(QString(), this, SLOT(onSnapshotGraphToggle()));`
добавить:

```cpp
    align_action = context_menu->addAction(tr("Align device time..."), this, SLOT(onAlignAction()));
    align_action->setToolTip(tr("Shift this device's time for this night to line it up with the CPAP data."));
    align_action->setVisible(false);
```

В `gGraphView::populateMenu` сразу после `    QAction * action;` добавить:

```cpp
    m_alignMenuGraph = graph;
    align_action->setVisible(m_alignMenuPredicate && !graph->isSnapshot() && m_alignMenuPredicate(graph));
```

- [ ] **Step 3: Новые функции** (добавить перед `void gGraphView::populateMenu(gGraph * graph)`)

```cpp
void gGraphView::onAlignAction()
{
    if (m_alignMenuGraph) emit alignRequestedForGraph(m_alignMenuGraph);
}

void gGraphView::setAlignMode(bool on, const QSet<QString>& targetGraphNames)
{
    m_alignMode = on;
    m_alignTargets = on ? targetGraphNames : QSet<QString>();
    m_alignDragging = false;
    m_alignPainted.clear();
    m_tooltip->cancel();
    if (!on) setCursor(Qt::ArrowCursor);
    timedRedraw(0);
}

void gGraphView::showAlignLabel(const QString& text)
{
    m_tooltip->display(text, m_mouse.x() + 16, m_mouse.y() - 28, TT_AlignLeft, 60000, true);
    timedRedraw(0);
}

gGraph *gGraphView::alignGraphAt(const QPoint &pos) const
{
    // Pinned graphs are painted after the scrolling ones, so the last match is the one on top.
    for (int i = m_alignPainted.size() - 1; i >= 0; --i) {
        if (m_alignPainted[i].second.contains(pos)) return m_alignPainted[i].first;
    }
    return nullptr;
}

void gGraphView::noteAlignTarget(gGraph *g)
{
    if (!m_alignMode || !m_alignTargets.contains(g->name())) return;
    // g->left/right hold the plot-area margins computed by the paint() that just ran.
    const QRect &r = g->m_rect;
    m_alignPainted.append(qMakePair(g, QRect(r.left() + g->left, r.top(),
                                             r.width() - g->left - g->right, r.height())));
}

void gGraphView::paintAlignFrames(QPainter &painter)
{
    if (!m_alignMode) return;
    painter.save();
    painter.setPen(QPen(QColor(24, 95, 165), 2, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    for (const auto &painted : m_alignPainted) {
        const QRect &r = painted.second;
        painter.drawRect(r.adjusted(1, 1, -1, -1));
        painter.drawText(r.left() + 6, r.top() + 16, QString(QChar(0x21C4)));   // ⇄
    }
    painter.restore();
}
```

- [ ] **Step 4: `renderGraphs` — запоминать нарисованные целевые графики и рисовать рамки**

В начале `bool gGraphView::renderGraphs(QPainter &painter)`, сразу после строки `    if (height() < 40) return false;`, добавить:

```cpp
    m_alignPainted.clear();
```

Оба цикла «физической» отрисовки имеют вид:

```cpp
    for (const auto & g : m_drawlist) {
        g->paint(painter, QRegion(g->m_rect));
    }
```

(первый — после комментария `// Physically draw the unpinned graphs`, второй — в ветке `#else`/без потоков в конце функции). В обоих заменить тело цикла на:

```cpp
    for (const auto & g : m_drawlist) {
        g->paint(painter, QRegion(g->m_rect));
        noteAlignTarget(g);
    }
```

В конце функции перед последней строкой
`    AppSetting->usePixmapCaching() ? DrawTextQueCached(painter) :DrawTextQue(painter);`
(та, что стоит прямо перед `return numgraphs > 0;`) добавить:

```cpp
    paintAlignFrames(painter);
```

- [ ] **Step 5: Мышь**

В `gGraphView::mousePressEvent` после блока объявления `x`/`y` (после `#endif`, перед `float h, pinned_height = 0, py = 0;`) добавить:

```cpp
    if (m_alignMode && (event->button() == Qt::LeftButton)) {
        if (gGraph *g = alignGraphAt(QPoint(x, y))) {
            const int plotWidth = qMax(1, g->m_rect.width() - g->left - g->right);
            m_alignMsPerPx = double(g->max_x - g->min_x) / double(plotWidth);
            m_alignDragStartX = x;
            m_alignDragging = true;
            m_tooltip->cancel();
            setCursor(Qt::SizeHorCursor);
            emit alignDragStarted();
            return;
        }
    }
```

В `gGraphView::mouseMoveEvent` сразу после строки `    m_mouse = QPoint(x, y);` добавить:

```cpp
    if (m_alignDragging) {
        emit alignDragMoved(double(x - m_alignDragStartX) * m_alignMsPerPx, m_alignMsPerPx);
        return;
    }
```

и в самом конце этой же функции — перед её закрывающей `}` (после цикла по незакреплённым графикам, который заканчивается строками `            py += h + graphSpacer;` / `        }`) — добавить:

```cpp
    if (m_alignMode && !m_button_down && alignGraphAt(m_mouse)) {
        setCursor(Qt::SizeHorCursor);
    }
```

В `gGraphView::mouseReleaseEvent` после блока объявления `x`/`y` (после `#endif`, перед `float h, py = 0, pinned_height = 0;`) добавить:

```cpp
    if (m_alignDragging) {
        m_alignDragging = false;
        m_tooltip->cancel();
        emit alignDragFinished();
        timedRedraw(0);
        return;
    }
```

- [ ] **Step 6: Клавиши**

В начало `void gGraphView::keyPressEvent(QKeyEvent *event)` (перед `m_metaselect = …`) добавить:

```cpp
    if (m_alignMode) {
        const qint64 step = (event->modifiers() & Qt::ShiftModifier) ? 60000 : 10000;
        switch (event->key()) {
        case Qt::Key_Left:   emit alignNudge(-step); event->accept(); return;
        case Qt::Key_Right:  emit alignNudge(step);  event->accept(); return;
        case Qt::Key_Return:
        case Qt::Key_Enter:  emit alignAccept();     event->accept(); return;
        case Qt::Key_Escape: event->accept(); return;   // acted on in keyReleaseEvent
        default: break;
        }
    }
```

В начало `void gGraphView::keyReleaseEvent(QKeyEvent *event)` добавить:

```cpp
    // Esc normally steps back through the zoom history here; in alignment mode it cancels
    // the alignment instead (and must not also change the zoom).
    if (m_alignMode && (event->key() == Qt::Key_Escape)) {
        emit alignCancel();
        event->accept();
        return;
    }
```

- [ ] **Step 7: Сборка, тесты, предупреждения**

Run:
```bash
cd /Users/semyk/Downloads/Oscar_Project/build && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:|make.*Error'
cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; ./test 2>&1 | grep -E '^FAIL'
/Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh gGraphView > /tmp/warn-ggv.txt; comm -13 <(grep gGraphView /Users/semyk/Downloads/Oscar_Project/tools/warn-baseline.txt) /tmp/warn-ggv.txt
```
Expected: ошибок нет; `FAIL` — только 3 известных; последняя команда (новые предупреждения) — пусто.

- [ ] **Step 8: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/Graphs/gGraphView.h oscar/Graphs/gGraphView.cpp
git commit -m "Add a time-alignment mode to gGraphView

While the mode is on, a left-drag on the target graphs reports a
horizontal shift instead of zooming, arrow keys/Enter/Esc report nudge,
accept and cancel, and the target graphs get a dashed frame. The
context menu can offer 'Align device time...' for graphs chosen by the
owner. The view stays generic: it knows nothing about corrections.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Полоса `TimeAlignBar`

**Files:**
- Create: `oscar/timealignbar.h`, `oscar/timealignbar.cpp`
- Modify: `oscar/oscar.pro` (SOURCES после `timealignsession.cpp \`, HEADERS после `timealignsession.h \`)

**Interfaces:**
- Consumes: `TimeAlignSession::formatOffset` (Задача 2); `Machine::brand()/model()/serial()/loaderName()`.
- Produces (для Задачи 5): `class TimeAlignBar : public QFrame` с `enum class Severity { Info, Warning }`; `void setDevices(const QList<Machine*>&, Machine* current)`; `void setOffset(qint64 ms)`; `void setStatus(const QString&, Severity)`; `void setSameAsLastNightEnabled(bool)`; `static QString deviceLabel(Machine*)`; сигналы `deviceChosen(Machine*)`, `nudgeRequested(qint64)`, `sameAsLastNightRequested()`, `moreOptionsRequested()`, `saveRequested()`, `cancelRequested()`.

- [ ] **Step 1: `oscar/timealignbar.h`**

```cpp
/* Time Alignment Bar Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef TIMEALIGNBAR_H
#define TIMEALIGNBAR_H

#include <QFrame>
#include <QList>

class Machine;
class QComboBox;
class QLabel;
class QPushButton;

/*! \class TimeAlignBar
    \brief The strip shown above the Daily graphs while a device's time is being aligned.
    Display and signals only; TimeAlignSession holds the state. */
class TimeAlignBar : public QFrame
{
    Q_OBJECT
public:
    enum class Severity { Info, Warning };

    explicit TimeAlignBar(QWidget *parent = nullptr);

    void setDevices(const QList<Machine *> &devices, Machine *current);
    void setOffset(qint64 ms);
    void setStatus(const QString &text, Severity severity);
    void setSameAsLastNightEnabled(bool enabled);

    //! \brief "Brand Model (serial)", as in the Time Corrections dialog; the serial is omitted when empty.
    static QString deviceLabel(Machine *mach);

signals:
    void deviceChosen(Machine *mach);
    void nudgeRequested(qint64 deltaMs);
    void sameAsLastNightRequested();
    void moreOptionsRequested();
    void saveRequested();
    void cancelRequested();

private:
    QComboBox   *m_deviceCombo = nullptr;
    QLabel      *m_offsetLabel = nullptr;
    QLabel      *m_statusLabel = nullptr;
    QPushButton *m_sameAsLastNight = nullptr;
};

#endif // TIMEALIGNBAR_H
```

- [ ] **Step 2: `oscar/timealignbar.cpp`**

```cpp
/* Time Alignment Bar Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "timealignbar.h"
#include "timealignsession.h"
#include "SleepLib/machine.h"

#include <QComboBox>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

TimeAlignBar::TimeAlignBar(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("TimeAlignBar"));
    setStyleSheet(QStringLiteral(
        "#TimeAlignBar { background: #E6F1FB; border: 1px solid #85B7EB; border-radius: 4px; }"));

    auto *top = new QHBoxLayout;
    top->setSpacing(4);
    top->addWidget(new QLabel(tr("Align device time:"), this));

    m_deviceCombo = new QComboBox(this);
    m_deviceCombo->setToolTip(tr("Device whose time is being aligned to the CPAP data"));
    top->addWidget(m_deviceCombo);

    m_offsetLabel = new QLabel(TimeAlignSession::formatOffset(0), this);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(mono.pointSize() + 3);
    mono.setBold(true);
    m_offsetLabel->setFont(mono);
    m_offsetLabel->setAlignment(Qt::AlignCenter);
    m_offsetLabel->setMinimumWidth(QFontMetrics(mono).horizontalAdvance(QStringLiteral("+00:00:00")) + 12);
    m_offsetLabel->setToolTip(tr("Time shift applied to this device for this night"));
    top->addWidget(m_offsetLabel);

    struct Step { qint64 ms; const char *text; };
    static const Step steps[] = {
        { -3600000, QT_TR_NOOP("-1h")  }, { -600000, QT_TR_NOOP("-10m") },
        {   -60000, QT_TR_NOOP("-1m")  }, {  -10000, QT_TR_NOOP("-10s") },
        {    10000, QT_TR_NOOP("+10s") }, {   60000, QT_TR_NOOP("+1m")  },
        {   600000, QT_TR_NOOP("+10m") }, { 3600000, QT_TR_NOOP("+1h")  },
    };
    for (const Step &step : steps) {
        auto *button = new QPushButton(tr(step.text), this);
        button->setAutoDefault(false);
        const qint64 delta = step.ms;
        connect(button, &QPushButton::clicked, this, [this, delta]() { emit nudgeRequested(delta); });
        top->addWidget(button);
        if (step.ms == -10000) top->addSpacing(8);   // gap between the minus and plus groups
    }
    top->addStretch(1);

    auto *cancel = new QPushButton(tr("Cancel"), this);
    cancel->setToolTip(tr("Discard the change (Esc)"));
    connect(cancel, &QPushButton::clicked, this, &TimeAlignBar::cancelRequested);
    top->addWidget(cancel);

    auto *save = new QPushButton(tr("Save"), this);
    save->setToolTip(tr("Save the shift for this night (Enter)"));
    connect(save, &QPushButton::clicked, this, &TimeAlignBar::saveRequested);
    top->addWidget(save);

    auto *bottom = new QHBoxLayout;
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    bottom->addWidget(m_statusLabel, 1);

    m_sameAsLastNight = new QPushButton(tr("Same as last night"), this);
    m_sameAsLastNight->setToolTip(tr("Use the shift saved for the nearest earlier night"));
    connect(m_sameAsLastNight, &QPushButton::clicked, this, &TimeAlignBar::sameAsLastNightRequested);
    bottom->addWidget(m_sameAsLastNight);

    auto *more = new QPushButton(tr("More options..."), this);
    more->setToolTip(tr("Open Time Corrections for date ranges and other correction types"));
    connect(more, &QPushButton::clicked, this, &TimeAlignBar::moreOptionsRequested);
    bottom->addWidget(more);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(2);
    layout->addLayout(top);
    layout->addLayout(bottom);

    connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0) return;
        emit deviceChosen(reinterpret_cast<Machine *>(m_deviceCombo->itemData(index).value<quintptr>()));
    });
}

void TimeAlignBar::setDevices(const QList<Machine *> &devices, Machine *current)
{
    QSignalBlocker block(m_deviceCombo);
    m_deviceCombo->clear();
    for (Machine *mach : devices) {
        m_deviceCombo->addItem(deviceLabel(mach), QVariant::fromValue(reinterpret_cast<quintptr>(mach)));
        if (mach == current) m_deviceCombo->setCurrentIndex(m_deviceCombo->count() - 1);
    }
}

void TimeAlignBar::setOffset(qint64 ms)
{
    m_offsetLabel->setText(TimeAlignSession::formatOffset(ms));
}

void TimeAlignBar::setStatus(const QString &text, Severity severity)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(severity == Severity::Warning ? QStringLiteral("color: #cc6600;") : QString());
}

void TimeAlignBar::setSameAsLastNightEnabled(bool enabled)
{
    m_sameAsLastNight->setEnabled(enabled);
}

QString TimeAlignBar::deviceLabel(Machine *mach)
{
    if (!mach) return QString();
    QString label = (mach->brand() + " " + mach->model()).trimmed();
    if (label.isEmpty()) label = mach->loaderName();
    if (!mach->serial().isEmpty()) label += " (" + mach->serial() + ")";
    return label;
}
```

- [ ] **Step 3: Подключить в `oscar/oscar.pro`**

`SOURCES`: после `    timealignsession.cpp \` добавить `    timealignbar.cpp \`.
`HEADERS`: после `    timealignsession.h \` добавить `    timealignbar.h \`.

- [ ] **Step 4: Сборка, тесты, предупреждения**

Run:
```bash
cd /Users/semyk/Downloads/Oscar_Project/build && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:|make.*Error'; /Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh timealignbar
cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; ./test 2>&1 | grep -E '^FAIL'
```
Expected: пусто, кроме 3 известных `FAIL` в `EventsTabTests`.

- [ ] **Step 5: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/timealignbar.h oscar/timealignbar.cpp oscar/oscar.pro
git commit -m "Add TimeAlignBar, the strip of alignment controls for the Daily view

Device picker, a large readout of the night's offset, nudge buttons
(+/-10s, 1m, 10m, 1h), Same as last night, More options, Cancel and
Save. Display and signals only.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Интеграция в `Daily`, `MainWindow` и диалог Time Corrections

**Files:**
- Modify: `oscar/daily.h`, `oscar/daily.cpp` (конструктор — конец, ~стр. 640; `ReloadGraphs` ~стр. 792; `on_ReloadDay` ~стр. 1189; `Load` — после `UpdateEventsTree(ui->treeWidget, day);` ~стр. 2238; `Unload` ~стр. 2629)
- Modify: `oscar/mainwindow.h`, `oscar/mainwindow.cpp` (рядом с `on_actionTime_Corrections_triggered` ~стр. 1898)
- Modify: `oscar/devicetimecorrectiondialog.h`, `oscar/devicetimecorrectiondialog.cpp`

**Interfaces:**
- Consumes: всё из Задач 2–4.
- Produces: `bool Daily::finishAlign(bool allowCancel);` `void MainWindow::openTimeCorrections(Machine*)`, `void MainWindow::refreshTimeCorrectionsDialog()`, `bool MainWindow::timeCorrectionsDialogHasStagedChange() const`, `void DeviceTimeCorrectionDialog::selectMachine(Machine*)`, `bool DeviceTimeCorrectionDialog::hasStagedChange() const`.

- [ ] **Step 1: Диалог Time Corrections**

`oscar/devicetimecorrectiondialog.h`, в `public:` после `void setDate(const QDate& date);`:

```cpp
    //! \brief Selects \a mach in the device list if it is listed for the current date.
    void selectMachine(Machine* mach);
    //! \brief True while a correction is being edited but not yet saved.
    bool hasStagedChange() const { return m_hasStagedChange; }
```

`oscar/devicetimecorrectiondialog.cpp`: к include'ам добавить `#include <QTreeWidgetItemIterator>`, после функции `currentMachine()` добавить:

```cpp
void DeviceTimeCorrectionDialog::selectMachine(Machine* mach)
{
    if (!mach) return;
    const quintptr wanted = reinterpret_cast<quintptr>(mach);
    for (QTreeWidgetItemIterator it(ui->deviceSidebar); *it; ++it) {
        const QVariant v = (*it)->data(0, Qt::UserRole);
        if (v.isValid() && v.value<quintptr>() == wanted) {
            ui->deviceSidebar->setCurrentItem(*it);
            return;
        }
    }
}
```

- [ ] **Step 2: MainWindow**

`oscar/mainwindow.h`, в `public:`-секцию класса `MainWindow` (например, рядом с `getDaily()`) добавить:

```cpp
    //! \brief Opens Data > Time Corrections with \a mach selected (nullptr keeps the dialog's choice).
    void openTimeCorrections(Machine *mach);
    //! \brief Re-reads corrections in an open Time Corrections dialog that has no unsaved edit.
    void refreshTimeCorrectionsDialog();
    //! \brief True while the Time Corrections dialog is open with an unsaved change.
    bool timeCorrectionsDialogHasStagedChange() const;
```

`oscar/mainwindow.cpp`, после функции `MainWindow::on_actionTime_Corrections_triggered()`:

```cpp
void MainWindow::openTimeCorrections(Machine *mach)
{
    on_actionTime_Corrections_triggered();
    if (m_correctionDialog && mach) m_correctionDialog->selectMachine(mach);
}

void MainWindow::refreshTimeCorrectionsDialog()
{
    // setDate() would silently drop an unsaved edit on the same date, so leave such a dialog alone.
    if (m_correctionDialog && daily && !m_correctionDialog->hasStagedChange()) {
        m_correctionDialog->setDate(daily->getDate());
    }
}

bool MainWindow::timeCorrectionsDialogHasStagedChange() const
{
    return m_correctionDialog && m_correctionDialog->hasStagedChange();
}
```

- [ ] **Step 3: `daily.h`**

К include'ам после `#include "mytextbrowser.h"` добавить:

```cpp
#include "timealignsession.h"
```

после `class DailySearchTab;` добавить `class TimeAlignBar;`.

В `public:` (например, после `void Unload(QDate date=QDate());`):

```cpp
    /*! \fn finishAlign(bool allowCancel)
        \brief Leaves time-alignment mode, asking whether to save an unsaved shift.
        \param allowCancel offer Cancel (for actions that can be abandoned, like changing the date)
        \return false only when the user chose Cancel */
    bool finishAlign(bool allowCancel);
```

В `private slots:` (после `void on_ReloadDay();`):

```cpp
    void onAlignButtonClicked(bool checked);
    void onAlignRequestedForGraph(gGraph *graph);
    void onAlignDeviceChosen(Machine *mach);
    void onAlignOffsetChanged(qint64 ms);
    void onAlignNudge(qint64 deltaMs);
    void onAlignDragStarted();
    void onAlignDragMoved(double rawDeltaMs, double msPerPx);
    void onAlignDragFinished();
    void onAlignSameAsLastNight();
    void onAlignMoreOptions();
    void onAlignSave();
    void onAlignCancel();
```

В `private:` (после `QHash<QString, gGraph *> graphlist;`):

```cpp
    // Time alignment of non-CPAP devices (Align bar)
    QList<Machine *> alignCandidates(Day *day) const;
    QSet<QString> alignTargetGraphs(Machine *mach, Day *day) const;
    void startAlign(Machine *mach);
    void stopAlign();
    void afterAlignSaved();
    void refreshAlignStatus();
    void updateAlignButton(Day *day);
    QPushButton *alignButton = nullptr;
    TimeAlignBar *m_alignBar = nullptr;
    TimeAlignSession *m_alignSession = nullptr;
    qint64 m_alignDragBaseMs = 0;
```

- [ ] **Step 4: `daily.cpp` — создание и связи**

К include'ам добавить `#include "timealignbar.h"` и `#include <QSignalBlocker>`.

В конце конструктора `Daily::Daily`, перед строкой `    saveGraphLayoutSettings=nullptr;`, добавить:

```cpp
    // Time alignment: the Align bar above the graphs and the Align button on the bottom bar
    m_alignSession = new TimeAlignSession(nullptr, this);
    m_alignBar = new TimeAlignBar(this);
    m_alignBar->hide();
    ui->verticalLayout_3->insertWidget(ui->verticalLayout_3->indexOf(ui->graphMainArea), m_alignBar);

    alignButton = new QPushButton(tr("Align"), this);
    alignButton->setCheckable(true);
    alignButton->setToolTip(tr("Line up an oximeter or other device with the CPAP data for this night"));
    alignButton->hide();
    if (auto *bar = qobject_cast<QBoxLayout *>(ui->frame->layout())) {
        bar->insertWidget(bar->indexOf(ui->graphHelp), alignButton);
    }
    connect(alignButton, &QPushButton::clicked, this, &Daily::onAlignButtonClicked);

    connect(m_alignSession, &TimeAlignSession::offsetChanged, this, &Daily::onAlignOffsetChanged);
    connect(m_alignBar, &TimeAlignBar::deviceChosen, this, &Daily::onAlignDeviceChosen);
    connect(m_alignBar, &TimeAlignBar::nudgeRequested, this, &Daily::onAlignNudge);
    connect(m_alignBar, &TimeAlignBar::sameAsLastNightRequested, this, &Daily::onAlignSameAsLastNight);
    connect(m_alignBar, &TimeAlignBar::moreOptionsRequested, this, &Daily::onAlignMoreOptions);
    connect(m_alignBar, &TimeAlignBar::saveRequested, this, &Daily::onAlignSave);
    connect(m_alignBar, &TimeAlignBar::cancelRequested, this, &Daily::onAlignCancel);
    connect(GraphView, &gGraphView::alignDragStarted, this, &Daily::onAlignDragStarted);
    connect(GraphView, &gGraphView::alignDragMoved, this, &Daily::onAlignDragMoved);
    connect(GraphView, &gGraphView::alignDragFinished, this, &Daily::onAlignDragFinished);
    connect(GraphView, &gGraphView::alignNudge, this, &Daily::onAlignNudge);
    connect(GraphView, &gGraphView::alignAccept, this, &Daily::onAlignSave);
    connect(GraphView, &gGraphView::alignCancel, this, &Daily::onAlignCancel);
    connect(GraphView, &gGraphView::alignRequestedForGraph, this, &Daily::onAlignRequestedForGraph);
    GraphView->setAlignMenuPredicate([this](gGraph *g) {
        Day *day = p_profile ? p_profile->GetDay(previous_date) : nullptr;
        for (Machine *mach : alignCandidates(day)) {
            if (alignTargetGraphs(mach, day).contains(g->name())) return true;
        }
        return false;
    });
```

- [ ] **Step 5: `daily.cpp` — защита и обновление**

В `void Daily::ReloadGraphs()` первой строкой тела (перед `PERF_TIMER_SCOPE`) добавить:

```cpp
    // The graphs are about to be rebuilt; a pending preview cannot survive that.
    if (m_alignSession && m_alignSession->isActive()) {
        m_alignSession->cancel();
        stopAlign();
    }
```

В `void Daily::on_ReloadDay()` сразу после строки `    static volatile bool inReload = false;` добавить:

```cpp
    // Every date change (calendar click, day arrows, LoadDate) arrives here.
    if (m_alignSession && m_alignSession->isActive() && previous_date.isValid()
            && (ui->calendar->selectedDate() != previous_date) && !finishAlign(true)) {
        QSignalBlocker block(ui->calendar);
        ui->calendar->setSelectedDate(previous_date);
        return;
    }
```

В `void Daily::Unload(QDate date)` первой строкой тела добавить:

```cpp
    finishAlign(false);   // profile close, import, purge and app exit cannot be cancelled
```

В `void Daily::Load(QDate date)` сразу после строки `    UpdateEventsTree(ui->treeWidget, day);` добавить:

```cpp
    updateAlignButton(day);
```

- [ ] **Step 6: `daily.cpp` — функции режима** (добавить в конец файла)

```cpp
// ---------------------------------------------------------------------------
// Time alignment (Align bar)
// ---------------------------------------------------------------------------

QList<Machine *> Daily::alignCandidates(Day *day) const
{
    QList<Machine *> result;
    if (!day) return result;
    for (Session *sess : day->sessions) {
        Machine *mach = sess ? sess->machine() : nullptr;
        if (!mach || (mach->type() == MT_CPAP) || !Machine::isCorrectableType(mach->type())) continue;
        if ((mach->getDatabaseId() <= 0) || result.contains(mach)) continue;
        result.append(mach);
    }
    // Oximeters first: they are the usual reason to align.
    std::stable_partition(result.begin(), result.end(), [](Machine *m) { return m->type() == MT_OXIMETER; });
    return result;
}

QSet<QString> Daily::alignTargetGraphs(Machine *mach, Day *day) const
{
    QSet<QString> names;
    if (!mach || !day) return names;
    for (auto it = graphlist.constBegin(); it != graphlist.constEnd(); ++it) {
        if (!it.value()) continue;
        const ChannelID code = schema::channel[it.key()].id();
        if (code == 0) continue;   // not a channel graph (event flags, pie, ...)
        for (Session *sess : day->sessions) {
            if (sess && (sess->machine() == mach) && sess->channelExists(code)) {
                names.insert(it.value()->name());
                break;
            }
        }
    }
    return names;
}

void Daily::updateAlignButton(Day *day)
{
    if (alignButton) alignButton->setVisible(!alignCandidates(day).isEmpty());
}

void Daily::startAlign(Machine *mach)
{
    Day *day = p_profile ? p_profile->GetDay(previous_date) : nullptr;
    if (!day || !mach) {
        stopAlign();
        return;
    }
    if (mainwin && mainwin->timeCorrectionsDialogHasStagedChange()) {
        QMessageBox::information(this, tr("Align Device Time"),
            tr("Save or cancel the change in the Time Corrections window first."));
        stopAlign();
        return;
    }
    if (!m_alignSession->begin(mach, previous_date)) {
        stopAlign();
        return;
    }
    m_alignBar->setDevices(alignCandidates(day), mach);
    m_alignBar->setOffset(m_alignSession->offsetMs());
    m_alignBar->setSameAsLastNightEnabled(m_alignSession->previousNightOffset().has_value());
    refreshAlignStatus();
    m_alignBar->show();
    {
        QSignalBlocker block(alignButton);
        alignButton->setChecked(true);
    }
    GraphView->setAlignMode(true, alignTargetGraphs(mach, day));
}

// Leaves the mode; the session must already be committed or cancelled.
void Daily::stopAlign()
{
    m_alignSession->end();
    GraphView->setAlignMode(false);
    m_alignBar->hide();
    {
        QSignalBlocker block(alignButton);
        alignButton->setChecked(false);
    }
    redrawWithZoom();
}

void Daily::afterAlignSaved()
{
    stopAlign();
    if (Day *day = p_profile ? p_profile->GetDay(previous_date) : nullptr) {
        UpdateEventsTree(ui->treeWidget, day);   // event times include the correction
    }
    if (mainwin) mainwin->refreshTimeCorrectionsDialog();
}

bool Daily::finishAlign(bool allowCancel)
{
    if (!m_alignSession || !m_alignSession->isActive()) return true;
    if (m_alignSession->isDirty()) {
        QMessageBox::StandardButtons buttons = QMessageBox::Save | QMessageBox::Discard;
        if (allowCancel) buttons |= QMessageBox::Cancel;
        const auto answer = QMessageBox::question(this, tr("Align Device Time"),
            tr("Save the time shift of %1 for %2?")
                .arg(TimeAlignBar::deviceLabel(m_alignSession->machine()),
                     QLocale().toString(m_alignSession->night(), QLocale::ShortFormat)),
            buttons, QMessageBox::Save);
        if (answer == QMessageBox::Cancel) return false;
        if (answer == QMessageBox::Save) {
            if (m_alignSession->commit()) {
                afterAlignSaved();
                return true;
            }
            QMessageBox::warning(this, tr("Align Device Time"), tr("Couldn't save the time correction."));
            if (allowCancel) return false;
        }
    }
    m_alignSession->cancel();
    stopAlign();
    return true;
}

void Daily::refreshAlignStatus()
{
    if (!m_alignSession->isActive()) return;
    QStringList notes;
    TimeAlignBar::Severity severity = TimeAlignBar::Severity::Info;
    Day *day = p_profile ? p_profile->GetDay(previous_date) : nullptr;
    if (day && !day->machine(MT_CPAP)) {
        notes << tr("No CPAP data this night to align against.");
    }
    const qint64 other = m_alignSession->otherCorrectionsMs();
    if (other != 0) {
        notes << tr("Other corrections also apply: %1").arg(TimeAlignSession::formatOffset(other));
    }
    if (qAbs(m_alignSession->offsetMs()) > TimeAlignSession::kLargeOffsetMs) {
        notes << tr("Large offset - check the device clock or use a date-range correction.");
        severity = TimeAlignBar::Severity::Warning;
    }
    if (notes.isEmpty()) {
        notes << tr("Drag the framed graphs or use the buttons. Arrow keys: 10 s, Shift+arrow: 1 min.");
    }
    m_alignBar->setStatus(notes.join(QStringLiteral("  ")), severity);
}

void Daily::onAlignButtonClicked(bool checked)
{
    if (!checked) {
        if (!finishAlign(true)) {
            QSignalBlocker block(alignButton);
            alignButton->setChecked(true);
        }
        return;
    }
    const QList<Machine *> candidates = alignCandidates(p_profile ? p_profile->GetDay(previous_date) : nullptr);
    if (candidates.isEmpty()) {
        QSignalBlocker block(alignButton);
        alignButton->setChecked(false);
        return;
    }
    startAlign(candidates.first());
}

void Daily::onAlignRequestedForGraph(gGraph *graph)
{
    Day *day = p_profile ? p_profile->GetDay(previous_date) : nullptr;
    for (Machine *mach : alignCandidates(day)) {
        if (!alignTargetGraphs(mach, day).contains(graph->name())) continue;
        if (m_alignSession->machine() == mach) return;             // already aligning it
        if (m_alignSession->isActive() && !finishAlign(true)) return;
        startAlign(mach);
        return;
    }
}

void Daily::onAlignDeviceChosen(Machine *mach)
{
    Machine *current = m_alignSession->machine();
    if (!mach || (mach == current)) return;
    if (!finishAlign(true)) {
        Day *day = p_profile ? p_profile->GetDay(previous_date) : nullptr;
        m_alignBar->setDevices(alignCandidates(day), current);    // put the picker back
        return;
    }
    startAlign(mach);
}

void Daily::onAlignOffsetChanged(qint64 ms)
{
    m_alignBar->setOffset(ms);
    refreshAlignStatus();
    GraphView->timedRedraw(0);
}

void Daily::onAlignNudge(qint64 deltaMs)
{
    if (!m_alignSession->isActive()) return;
    m_alignSession->nudge(deltaMs);
    redrawWithZoom();
}

void Daily::onAlignDragStarted()
{
    m_alignDragBaseMs = m_alignSession->offsetMs();
}

void Daily::onAlignDragMoved(double rawDeltaMs, double msPerPx)
{
    if (!m_alignSession->isActive()) return;
    m_alignSession->setOffsetMs(m_alignDragBaseMs + TimeAlignSession::snapDelta(rawDeltaMs, msPerPx));
    const qint64 now = m_alignSession->offsetMs();
    GraphView->showAlignLabel(tr("%1  (Δ %2)")
        .arg(TimeAlignSession::formatOffset(now), TimeAlignSession::formatOffset(now - m_alignDragBaseMs)));
}

void Daily::onAlignDragFinished()
{
    redrawWithZoom();   // the device's first/last times moved: recompute the graph bounds
}

void Daily::onAlignSameAsLastNight()
{
    if (const auto previous = m_alignSession->previousNightOffset()) {
        m_alignSession->setOffsetMs(*previous);
        redrawWithZoom();
    }
}

void Daily::onAlignMoreOptions()
{
    Machine *mach = m_alignSession->machine();
    if (!finishAlign(true)) return;
    if (mainwin) mainwin->openTimeCorrections(mach);
}

void Daily::onAlignSave()
{
    if (!m_alignSession->isActive()) return;
    if (!m_alignSession->commit()) {
        QMessageBox::warning(this, tr("Align Device Time"), tr("Couldn't save the time correction."));
        return;
    }
    afterAlignSaved();
}

void Daily::onAlignCancel()
{
    if (m_alignSession->isActive()) m_alignSession->cancel();
    stopAlign();
}
```

- [ ] **Step 7: Сборка, тесты, предупреждения**

Run:
```bash
cd /Users/semyk/Downloads/Oscar_Project/build && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:|make.*Error'
cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; ./test 2>&1 | grep -E '^FAIL'
/Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh daily mainwindow devicetimecorrectiondialog > /tmp/warn-daily.txt; comm -13 <(grep -E 'daily|mainwindow|devicetimecorrection' /Users/semyk/Downloads/Oscar_Project/tools/warn-baseline.txt) /tmp/warn-daily.txt
```
Expected: ошибок нет; `FAIL` — только 3 известных; новых предупреждений нет.

- [ ] **Step 8: Быстрый дымовой запуск**

Run:
```bash
open -n /Users/semyk/Downloads/Oscar_Project/build/OSCAR20.app --args --datadir /Users/semyk/Documents/OSCAR20_Data_dev; sleep 8; pgrep -lf 'build/OSCAR20.app' && tail -5 /Users/semyk/Documents/OSCAR20_Data_dev/logs/debug.txt
```
Expected: процесс жив, в логе нет `Critical`/`ASSERT`. Закрыть приложение (`Cmd+Q`) перед следующей задачей.

- [ ] **Step 9: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/daily.h oscar/daily.cpp oscar/mainwindow.h oscar/mainwindow.cpp oscar/devicetimecorrectiondialog.h oscar/devicetimecorrectiondialog.cpp
git commit -m "Align a device's time directly in the Daily view

An Align button (and 'Align device time...' in the graph menu) opens a
bar above the graphs for oximeters and other non-CPAP devices of the
night: drag the framed graphs, use the nudge buttons or arrow keys, and
save the shift as that night's offset correction. Unsaved shifts are
confirmed on date/device changes, profile close and exit; the events
tree and an open Time Corrections dialog are refreshed after saving.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Ручная проверка на копии данных, финальная сборка, push

**Files:** нет изменений кода (исправления по итогам — отдельными коммитами с повтором соответствующих проверок).

- [ ] **Step 1: Полная пересборка и все тесты**

Run:
```bash
cd /Users/semyk/Downloads/Oscar_Project/build && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:|make.*Error'
cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; ./test 2>&1 | grep -E '^(FAIL|Totals)'
```
Expected: ошибок нет; `FAIL` — только 3 известных `EventsTabTests`.

- [ ] **Step 2: Запуск на копии данных**

Run: `open -n /Users/semyk/Downloads/Oscar_Project/build/OSCAR20.app --args --datadir /Users/semyk/Documents/OSCAR20_Data_dev`
Открыть «День» → 24.09.2026 (Löwenstein + MD300W1).

- [ ] **Step 3: Чек-лист (отмечать каждый пункт; при расхождении — исправить, повторить Задачу 6 с шага 1)**

- [ ] кнопка «Align» видна на нижней панели 24.09; на ночи без оксиметра — скрыта («кнопка скрыта»)
- [ ] «Align» открывает полосу; графики Pulse и SpO2 в пунктирной рамке с ⇄; графики CPAP без рамки
- [ ] правый клик по заголовку SpO2 → «Align device time...» есть; по заголовку «Давление» — нет
- [ ] перетаскивание SpO2 вправо: график едет, у курсора `+00:MM:SS  (Δ …)`, цифра в полосе совпадает; на полной ночи шаг 1 мин, при зуме на ~1 ч — 10 с
- [ ] кнопки ±10s/±1m/±10m/±1h меняют сдвиг на ровно эти величины
- [ ] ←/→ (курсор над графиками) = 10 с, Shift+←/→ = 1 мин
- [ ] +1h на 24.09: провалы SpO2 визуально встают после событий CPAP (гипотеза пользователя) — зафиксировать итоговое значение
- [ ] Enter/«Save» → полоса закрывается; перезапуск приложения → сдвиг на месте; в Data → Time Corrections для MD300W1 есть строка Offset на 2026-09-24
- [ ] «Esc»: сдвиг отменяется, **зум не меняется** («Esc»)
- [ ] «прокрутка»: прокрутить графики колесом так, чтобы SpO2 сместился, — перетаскивание захватывается там, где SpO2 нарисован сейчас
- [ ] «pinned»: закрепить «События», прокрутить так, чтобы SpO2 ушёл под закреплённый график, — перетаскивание по закреплённому ничего не двигает
- [ ] «смена даты»: несохранённый сдвиг → клик по другой дате в календаре → вопрос Save/Discard/Cancel; Cancel — календарь вернулся, сдвиг на месте; Discard — сдвиг пропал; Save — сохранён
- [ ] смена устройства в полосе (если есть второе) — тот же вопрос
- [ ] «Same as last night» на 25.09 подставляет значение, сохранённое для 24.09
- [ ] «More options...» → вопрос (если не сохранено) → открыт Time Corrections с выбранным MD300W1
- [ ] сдвиг > 3 ч — оранжевое предупреждение; больше 12 ч не двигается
- [ ] «без CPAP»: на ночи, где есть только оксиметр (если такая есть в данных), — подсказка «No CPAP data this night…»
- [ ] выход из приложения с несохранённым сдвигом → вопрос Save/Discard
- [ ] вне режима: зум выделением, правый клик-перетаскивание (панорама), Esc (история зума), ←/→ (панорама) работают как раньше
- [ ] оригинал не тронут: `stat -f '%Sm' ~/Documents/OSCAR20_Data/oscar.db` показывает 27 сен 17:49

- [ ] **Step 4: Итоговая проверка предупреждений**

Run:
```bash
/Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh machine devicetimecorrectiondialog gGraphView daily mainwindow timealignsession timealignbar > /tmp/warn-final.txt; comm -13 /Users/semyk/Downloads/Oscar_Project/tools/warn-baseline.txt /tmp/warn-final.txt
```
Expected: пусто.

- [ ] **Step 5: Push в форк**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql && git push origin feature-daily-time-align && git log --oneline upstream/master..HEAD
```
Expected: 7 коммитов: 2 fork-only (спецификация, план) + 5 коммитов задач 1–5 (+ исправления, если были).

- [ ] **Step 6: Подготовка к MR (не выполнять без согласия пользователя)**

Для GitLab-MR понадобится ветка без коммитов `docs/superpowers` (берутся только коммиты, менявшие `oscar/`):

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git checkout -b feature-daily-time-align-mr upstream/master
git cherry-pick $(git log --reverse --format=%h upstream/master..feature-daily-time-align -- oscar)
```

Форк на GitLab и сам MR создаёт пользователь (см. CONTRIBUTING.md).
