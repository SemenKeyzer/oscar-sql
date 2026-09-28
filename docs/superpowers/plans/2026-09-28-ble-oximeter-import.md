# Импорт с пульсоксиметра по Bluetooth — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Добавить в мастер импорта оксиметра OSCAR импорт по Bluetooth с Contec CMS50…W (вариант A протокола): все новые записи одним проходом, установка часов прибора и опциональное автостирание с запоминанием.

**Architecture:** Чистый протокол (`contec_ble_protocol`, включая шифрование на Botan) и сеанс-автомат (`contec_ble_downloader`), общающийся с прибором через абстрактный `ContecBleLink`, — собираются всегда и тестируются с симулятором прибора. Решения по записям, правило стирания и сохранение (`contec_ble_import`, `oximetry_session_builder`) тестируются на временном профиле. Bluetooth-канал на Qt Bluetooth (`contec_ble_link`) и страница мастера (`bluetoothoximeterpage`) подключаются только при наличии модуля (`HAVE_BLUETOOTH`).

**Tech Stack:** C++17, Qt 6.11 (Homebrew) + Qt Bluetooth (`qtconnectivity`), qmake, QtTest (`tests/AutoTest.h`), Botan (амальгамация в `SleepLib/thirdparty`), SQLite.

**Spec:** `docs/superpowers/specs/2026-09-28-ble-oximeter-import-design.md`

## Global Constraints

- Репозиторий `/Users/semyk/Downloads/Oscar_Project/oscar-sql`, ветка `feature-ble-oximeter-import` (от `upstream/master`); push только в `origin` (GitHub-форк).
- Эталон протокола — `/Users/semyk/Downloads/ContecBTdiscover` (Python). **Только чтение.** Логи/данные пользователя в репозиторий не попадают; в тестах — синтетика и векторы из `tests/data/vectors.txt` (сняты с Java-кода приложения).
- Сборки: приложение `/Users/semyk/Downloads/Oscar_Project/build`, тесты `/Users/semyk/Downloads/Oscar_Project/build-test`. **После добавления новых файлов в `oscar.pro` перезапускать qmake** в обоих каталогах:
  - `cd …/build && /opt/homebrew/bin/qmake ../oscar-sql/oscar/oscar.pro -spec macx-clang`
  - `cd …/build-test && /opt/homebrew/bin/qmake ../oscar-sql/oscar/oscar.pro -spec macx-clang CONFIG+=test CONFIG+=sdk_no_version_check`
- Прогон тестов: `/Users/semyk/Downloads/Oscar_Project/tools/runtests.sh [Класс…]` — успех, если падают только 3 известных `EventsTabTests`.
- Предупреждения: `/Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh <obj…>`; новые не допускаются (сравнение `comm -13` с базовой линией этой ветки `tools/warn-baseline-ble.txt`).
- Строки интерфейса — английские через `tr()`; `.ts` не трогать. Новые файлы — заголовок `Copyright (c) 2026 The OSCAR Team` + GPL-абзац, как у соседей.
- Коммиты — на английском, трейлер `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. `docs/superpowers/**` — только `git add -f`, в MR не входит.
- Протокол: ORIGINAL предпочтителен (затем CODE, затем DIFFERENCE); тайм-аут ответа 5 с; повтор пакета до 5 раз с паузой 0,5 с; F3 отправляется 18 + (0,3 с) + 4 байта; запись в `ff01` порциями ≤ 20 байт; пауза 0,8 с после подключения.
- Разрушительная команда `9D 7F 7F 7F 7F 00 00 19` уходит **только** после `allowDestructive(true)`; порядок: выгрузка → сохранение → часы → стирание.
- Автостирание — только при выполнении всех условий спецификации §5 (галочка; выгрузка завершена; каждая запись Imported/Updated/AlreadyPresent; конец последней записи < начало выгрузки − 5 мин; подтверждение `ED 7F [5]==0`).
- Настройки: часы — существующая `p_profile->oxi->syncOximeterClock()` (по умолчанию true); стирание — новая `bleEraseAfterImport()` (по умолчанию false), изменения сразу пишутся в профиль.
- Ручные проверки — только `open -n …/build/OSCAR20.app --args --datadir /Users/semyk/Documents/OSCAR20_Data_dev`.

## Уточнения спецификации (найдены при составлении плана)

1. `ContecBleLoader` — наследник `SerialOximeter`, а не просто `MachineLoader`: `GetOxiLoaders()` кладёт `nullptr` для оксиметрического загрузчика другого типа, и импорт из файла упал бы на `Detect()`. Дополнительно `GetOxiLoaders()` получает защиту от `nullptr`.
2. Вместо выноса `Machine::dayForSession()` из ядра `Machine::AddSession` (там логика объединения близких сессий по дням самого устройства): до выгрузки — прогноз ночи `predictNight()` по времени разделения суток, после `AddSession` — окончательная проверка, что сессия действительно попала в день (иначе откат и «Conflict»).
3. Построение сессии разделено на `addOximetryEvents()` (без профиля, тестируется) и `finishOximetrySession()` (десатурации и итоги, читает настройки профиля).
4. Страница мастера — отдельный виджет `BluetoothOximeterPage` в своих файлах; `oximeterimport.*` только добавляет кнопку и страницу.
5. `canErase()` различает ещё `NothingToErase` (на приборе нет записей) — стирать нечего, сообщение не выводится.

## Review Focus

Случаи, которые спецификация подразумевает, но юнит-тесты не покрывают (закреплены пунктами ручного чек-листа Задачи 8):

1. Первый доступ к Bluetooth на macOS: системный запрос разрешения; при отказе — понятное сообщение, приложение не падает (ключ `NSBluetoothAlwaysUsageDescription` в Info.plist) — «разрешение».
2. Прибор выключили / вышел из зоны посреди выгрузки: уже сохранённые записи остаются, остальные — «Not downloaded», стирания нет — «обрыв».
3. Выгрузка с надетым датчиком: последняя запись растёт; автостирание не выполняется («still recording»), при следующем импорте запись «Updated» — «растущая запись».
4. Закрытие мастера (Cancel/крестик) во время выгрузки: сеанс останавливается, поздние сигналы не обращаются к удалённым объектам — «отмена».
5. Длинная запись (≥ 6 ч, ~20 тыс. отсчётов на канал) через write-without-response на macOS: без потерь, повторы при сбоях — «длинная ночь».

---

### Task 0: Базовая линия ветки

**Files:** нет изменений в репозитории.

- [ ] **Step 1: Пересобрать обе сборки на этой ветке** (в `build` лежат объекты `master` с выравниванием времени)

Run:
```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql && git status --short && git branch --show-current
cd /Users/semyk/Downloads/Oscar_Project/build && /opt/homebrew/bin/qmake ../oscar-sql/oscar/oscar.pro -spec macx-clang > qmake.log 2>&1 && make -j$(sysctl -n hw.logicalcpu) > make.log 2>&1; echo "app make exit $?"
cd /Users/semyk/Downloads/Oscar_Project/build-test && /opt/homebrew/bin/qmake ../oscar-sql/oscar/oscar.pro -spec macx-clang CONFIG+=test CONFIG+=sdk_no_version_check > qmake.log 2>&1 && make -j$(sysctl -n hw.logicalcpu) > make.log 2>&1; echo "test make exit $?"
/Users/semyk/Downloads/Oscar_Project/tools/runtests.sh
```
Expected: ветка `feature-ble-oximeter-import`, рабочее дерево чистое; `app make exit 0`, `test make exit 0`; `suite: … 3 known pre-existing EventsTabTests failures, 0 unexpected`.

- [ ] **Step 2: Базовая линия предупреждений**

Run:
```bash
/Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh oximeterimport serialoximeter main machine > /Users/semyk/Downloads/Oscar_Project/tools/warn-baseline-ble.txt; wc -l /Users/semyk/Downloads/Oscar_Project/tools/warn-baseline-ble.txt
```
Expected: файл создан.

---

### Task 1: Протокол — кадры, команды, разбор ответов

**Files:**
- Create: `oscar/SleepLib/loader_plugins/contec_ble_protocol.h`, `oscar/SleepLib/loader_plugins/contec_ble_protocol.cpp`
- Create: `oscar/tests/contecbleprotocoltests.h`, `oscar/tests/contecbleprotocoltests.cpp`
- Modify: `oscar/oscar.pro` (SOURCES/HEADERS после `SleepLib/loader_plugins/md300w1_loader.*`; блок `test {}`)

**Interfaces:**
- Produces (namespace `ContecBle`): `ModelInfo{QString model; char variant; bool isValid()}`, `modelForName(QString)`; `checksum(QByteArray)`, `frame(std::initializer_list<int>)`, `pack7(data, hi&, lo&)`, `unpack7(hi, lo)`, `lo7(f, from, count)`; `frameLength(buf, variant)`; `class FrameSplitter{feed(), clear(), skippedBytes()}`; `enum Format{FmtDifference=1, FmtOriginal=2, FmtCode=4}`, `enum Channel{ChSpO2=1, ChPulse=2, ChPI=3}`, `formatCode(int)`; команды `cmdId() cmdInfo() cmdPrepare() cmdStorage() cmdFormats() cmdCountRecords() cmdNextHeader() cmdChannel(fmt,ch,l,m,offset=0) cmdChannelAbort(ch,l,m) cmdSetTime(QDateTime) cmdEraseAllRecords()`, `isDestructive(QByteArray)`, `pickFormat(int)`; разбор `parseF1 → QString`, `parseF2 → DeviceInfo{firmware, protocolVersion}`, `parseEF → StorageStatus{busy, hasData, mask1, mask2, hasRecords()}`, `parseE0Count → int`, `parseFE06Formats → int`, `parseEC → RecordHeader{last, hasPI, l, m, year..second, samples, start()}`, `edPacketNo`, `checksumOk`, `parseEdOriginal → QVector<int>`, `parseEdDifference(f, ver) → QVector<int>`, `class CodeDecoder{feed()}`; `struct Record{RecordHeader header; QVector<int> spo2, pulse, pi;}`.

- [ ] **Step 1: Заголовок `oscar/SleepLib/loader_plugins/contec_ble_protocol.h`**

```cpp
/* Contec BLE Oximeter Protocol Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CONTEC_BLE_PROTOCOL_H
#define CONTEC_BLE_PROTOCOL_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QVector>
#include <initializer_list>

/*! Wire protocol of the Contec CMS50/CMS60 Bluetooth LE oximeters ("variant A":
    CMS50EW/FW/IW/D-BT/S/S+, CMS60D1). Reverse-engineered for interoperability from the vendor
    app com.contec.android.spo2device 3.5.8. Pure functions only: no I/O, no Qt Bluetooth. */
namespace ContecBle {

constexpr quint16 kServiceShortUuid = 0xFF12;   //!< 0000ff12-0000-1000-8000-00805f9b34fb
constexpr quint16 kWriteShortUuid   = 0xFF01;
constexpr quint16 kNotifyShortUuid  = 0xFF02;

struct ModelInfo {
    QString model;       //!< e.g. "CMS50FW"
    char variant = 0;    //!< 'A' or 'K'; 0 when the name is not a known oximeter
    bool isValid() const { return variant != 0; }
};
//! Longest-prefix match of an advertised name ("SpO202…" -> CMS50FW, variant A).
ModelInfo modelForName(const QString &advertisedName);

quint8 checksum(const QByteArray &body);
//! The given bytes followed by their checksum.
QByteArray frame(std::initializer_list<int> body);
//! Splits 8-bit bytes into one "high bits" byte per 7 bytes and the 7-bit low parts.
void pack7(const QByteArray &data, QByteArray &hi, QByteArray &lo);
QByteArray unpack7(const QByteArray &hi, const QByteArray &lo);
//! Little-endian number with 7 bits per byte: f[from] | f[from+1] << 7 | ...
quint32 lo7(const QByteArray &f, int from, int count);

//! Length of the device frame starting at buf[0]: 0 = need more bytes, 1 = skip this byte.
int frameLength(const QByteArray &buf, char variant = 'A');

//! Reassembles device frames from notifications, which may split or merge frames.
class FrameSplitter
{
public:
    explicit FrameSplitter(char variant = 'A') : m_variant(variant) {}
    QList<QByteArray> feed(const QByteArray &data);
    void clear() { m_buf.clear(); }
    int skippedBytes() const { return m_skipped; }
private:
    char m_variant;
    QByteArray m_buf;
    int m_skipped = 0;
};

//! Record formats as reported by FE 06 (bits).
enum Format { FmtDifference = 1, FmtOriginal = 2, FmtCode = 4 };
enum Channel { ChSpO2 = 1, ChPulse = 2, ChPI = 3 };
//! The 9D command / ED answer sub-code of a format: 01, 03 or 04.
quint8 formatCode(int format);

QByteArray cmdId();             //!< 81 01 -> F1
QByteArray cmdInfo();           //!< 82 02 -> F2
QByteArray cmdPrepare();        //!< 8F 04 00 13 -> FF
QByteArray cmdStorage();        //!< 9F 1F -> EF
QByteArray cmdFormats();        //!< 8E 06 14 -> FE 06
QByteArray cmdCountRecords();   //!< 90 06 16 -> E0 06
QByteArray cmdNextHeader();     //!< 9C 01 1D -> EC
QByteArray cmdChannel(int format, int channel, int l, int m, int offset = 0);
QByteArray cmdChannelAbort(int channel, int l, int m);
QByteArray cmdSetTime(const QDateTime &localTime);   //!< 83 YY MM DD hh mi ss msL msH -> F3
QByteArray cmdEraseAllRecords();                      //!< 9D 7F 7F 7F 7F 00 00 19 -> ED 7F
bool isDestructive(const QByteArray &cmd);
//! ORIGINAL (verified on a CMS50FW) if supported, else CODE, else DIFFERENCE.
int pickFormat(int supported);

QString parseF1(const QByteArray &f);
struct DeviceInfo { QString firmware; int protocolVersion = 0; };
DeviceInfo parseF2(const QByteArray &f);
struct StorageStatus {
    bool busy = false;
    bool hasData = false;
    int mask1 = 0;   //!< bit 6: stored records
    int mask2 = 0;
    bool hasRecords() const { return (mask1 & 0x40) != 0; }
};
StorageStatus parseEF(const QByteArray &f);
int parseE0Count(const QByteArray &f);
int parseFE06Formats(const QByteArray &f);

struct RecordHeader {
    bool last = false;
    bool hasPI = false;
    int l = 0;        //!< user number
    int m = 0;        //!< record number
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int samples = 0;  //!< one per second
    //! Start on the oximeter clock (local time); invalid when the device sent impossible fields.
    QDateTime start() const;
};
RecordHeader parseEC(const QByteArray &f);

int edPacketNo(const QByteArray &f);
bool checksumOk(const QByteArray &f);
QVector<int> parseEdOriginal(const QByteArray &f);                        //!< ED 03: 21 samples
QVector<int> parseEdDifference(const QByteArray &f, int protocolVersion); //!< ED 01: 27 samples

//! ED 04 (CODE) decoder; escape pairs may straddle packets, so it keeps state.
class CodeDecoder
{
public:
    QVector<int> feed(const QByteArray &f);
private:
    int m_base = 0;
    int m_pending = -1;
};

//! One stored recording, one sample per second. 127 (SpO2) and 255 (pulse, PI) mean "no data".
struct Record {
    RecordHeader header;
    QVector<int> spo2;
    QVector<int> pulse;
    QVector<int> pi;
};

} // namespace ContecBle

#endif // CONTEC_BLE_PROTOCOL_H
```

- [ ] **Step 2: Тесты `oscar/tests/contecbleprotocoltests.h`**

```cpp
/* Contec BLE Protocol Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class ContecBleProtocolTests : public QObject
{
    Q_OBJECT
private slots:
    void testCommandBytes();
    void testChannelCommands();
    void testSetTimeCommand();
    void testPickFormat();
    void testDestructiveGuard();
    void testModelLookup();
    void testFrameLengths();
    void testSplitterHandlesSplitAndMergedNotifications();
    void testParseDeviceAnswers();
    void testRecordHeaderAndMidnight();
    void testEdVectorsMatchVendorCode();
    void testCodeDecoderAcrossPackets();
};
DECLARE_TEST(ContecBleProtocolTests)
```

- [ ] **Step 3: Тесты `oscar/tests/contecbleprotocoltests.cpp`**

```cpp
/* Contec BLE Protocol Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contecbleprotocoltests.h"
#include "SleepLib/loader_plugins/contec_ble_protocol.h"

#include <vector>

using namespace ContecBle;

namespace {

QByteArray hex(const char *s) { return QByteArray::fromHex(QByteArray(s)); }
QString spaced(const QByteArray &b) { return QString::fromLatin1(b.toHex(' ')); }
QVector<int> ints(const std::vector<int> &v) { return QVector<int>(v.begin(), v.end()); }

// Produced by running the vendor Java parsers on random packets (ContecBTdiscover/tests/data/vectors.txt).
struct EdVector { int version; const char *raw; std::vector<int> difference; std::vector<int> original; };
const EdVector kEdVectors[] = {
    { 11, "67204e312f0e591274251915672450633b54277f45655158100b3f650355",
      { 37, 36, 35, 36, 41, 47, 255, 253, 257, 262, 262, 268, 271, 274, 271, 276, 280, 278, 255, 255, 255, 251, 256, 250, 255, 250, 251 },
      { 116, 165, 153, 149, 103, 36, 80, 227, 59, 84, 167, 255, 69, 229, 81, 216, 16, 11, 191, 101, 3 } },
    { 11, "043b3d3a5322323c78113168527d625972324c247c4e092e632e237d7317",
      { 17, 20, 21, 15, 15, 10, 12, 255, 250, 244, 246, 251, 250, 255, 257, 260, 262, 266, 262, 260, 264, 255, 251, 247, 241, 241, 240 },
      { 120, 145, 49, 104, 82, 253, 98, 89, 242, 50, 76, 164, 252, 78, 9, 46, 227, 174, 163, 253, 115 } },
    { 14, "2f7537232a66286538100c325f711574672511780b260354462024337b7e",
      { 198, 198, 194, 191, 193, 198, 255, 255, 256, 255, 260, 255, 259, 265, 255, 257, 262, 263, 264, 255, 255, 255, 252, 250, 256, 256, 259 },
      { 56, 144, 140, 50, 95, 241, 149, 116, 103, 37, 145, 120, 139, 38, 131, 84, 198, 32, 36, 179, 251 } },
    { 14, "73732c5d0101670242266a474a36586e473d0e274b0972715054305a734b",
      { 112, 106, 104, 108, 255, 259, 257, 260, 266, 271, 271, 277, 271, 275, 255, 252, 247, 247, 241, 243, 255, 259, 256, 256, 255, 255, 257 },
      { 194, 38, 106, 71, 74, 54, 88, 238, 199, 189, 14, 39, 203, 137, 114, 241, 80, 84, 48, 90, 115 } },
    { 14, "6a1221007b51326a49372178697254104e583e1a2e195e7f6d7c2b40610b",
      { 97, 95, 96, 255, 255, 249, 248, 255, 257, 252, 256, 255, 255, 251, 245, 250, 250, 253, 247, 246, 244, 246, 240, 241, 240, 235, 229 },
      { 201, 55, 33, 120, 233, 114, 212, 16, 206, 88, 62, 154, 174, 25, 94, 255, 109, 252, 43, 192, 225 } },
};

} // namespace

void ContecBleProtocolTests::testCommandBytes()
{
    QCOMPARE(spaced(cmdId()), QStringLiteral("81 01"));
    QCOMPARE(spaced(cmdInfo()), QStringLiteral("82 02"));
    QCOMPARE(spaced(cmdPrepare()), QStringLiteral("8f 04 00 13"));
    QCOMPARE(spaced(cmdStorage()), QStringLiteral("9f 1f"));
    QCOMPARE(spaced(cmdFormats()), QStringLiteral("8e 06 14"));
    QCOMPARE(spaced(cmdCountRecords()), QStringLiteral("90 06 16"));
    QCOMPARE(spaced(cmdNextHeader()), QStringLiteral("9c 01 1d"));
    QCOMPARE(spaced(cmdEraseAllRecords()), QStringLiteral("9d 7f 7f 7f 7f 00 00 19"));
}

// FE 06 reports format bits 1/2/4, but 9D uses the codes 01/03/04 (02 aborts a channel).
void ContecBleProtocolTests::testChannelCommands()
{
    const QByteArray diff = cmdChannel(FmtDifference, 1, 5, 6, 300);
    QCOMPARE(diff, frame({0x9D, 0x01, 0x01, 0x05, 0x06, 0x2C, 0x02}));
    const QByteArray orig = cmdChannel(FmtOriginal, 2, 5, 6);
    QCOMPARE(orig.size(), 9);
    QCOMPARE(quint8(orig[1]), quint8(0x03));
    QCOMPARE(quint8(cmdChannel(FmtCode, 3, 5, 6)[1]), quint8(0x04));
    QCOMPARE(cmdChannelAbort(1, 5, 6), frame({0x9D, 0x02, 0x01, 0x05, 0x06, 0x00, 0x00}));
    QCOMPARE(formatCode(FmtDifference), quint8(0x01));
    QCOMPARE(formatCode(FmtOriginal), quint8(0x03));
    QCOMPARE(formatCode(FmtCode), quint8(0x04));
}

void ContecBleProtocolTests::testSetTimeCommand()
{
    const QDateTime t(QDate(2026, 9, 28), QTime(7, 30, 15, 300));
    QCOMPARE(cmdSetTime(t), frame({0x83, 26, 9, 28, 7, 30, 15, 300 & 0x7F, 300 >> 7}));
}

void ContecBleProtocolTests::testPickFormat()
{
    QCOMPARE(pickFormat(0x07), int(FmtOriginal));
    QCOMPARE(pickFormat(0x05), int(FmtCode));
    QCOMPARE(pickFormat(0x01), int(FmtDifference));
}

void ContecBleProtocolTests::testDestructiveGuard()
{
    QVERIFY(isDestructive(cmdEraseAllRecords()));
    QVERIFY(isDestructive(frame({0xA1, 0x00})));
    QVERIFY(isDestructive(frame({0x91, 0x7F})));
    QVERIFY(!isDestructive(frame({0x91, 0x7E})));
    QVERIFY(!isDestructive(cmdChannel(FmtOriginal, 1, 5, 6)));
    QVERIFY(!isDestructive(cmdChannelAbort(1, 5, 6)));
    QVERIFY(!isDestructive(cmdStorage()));
}

void ContecBleProtocolTests::testModelLookup()
{
    QCOMPARE(modelForName(QStringLiteral("SpO202")).model, QStringLiteral("CMS50FW"));
    QCOMPARE(modelForName(QStringLiteral("SpO202")).variant, 'A');
    QCOMPARE(modelForName(QStringLiteral("SpO210xyz")).model, QStringLiteral("CMS50K1"));
    QCOMPARE(modelForName(QStringLiteral("SpO210xyz")).variant, 'K');
    QVERIFY(!modelForName(QStringLiteral("SpO203")).isValid());
    QVERIFY(!modelForName(QString()).isValid());
}

void ContecBleProtocolTests::testFrameLengths()
{
    QCOMPARE(frameLength(hex("f200000206000007")), 0);           // F2 needs 9 bytes before it knows
    QCOMPARE(frameLength(hex("f200000206000d0700")), 9);         // no strings: fixed 9 bytes
    QCOMPARE(frameLength(hex("e006")), 15);
    QCOMPARE(frameLength(hex("e000")), 7);
    QCOMPARE(frameLength(hex("ed03")), 30);
    QCOMPARE(frameLength(hex("ed01")), 24);
    QCOMPARE(frameLength(hex("ed7f")), 7);
    QCOMPARE(frameLength(hex("8415")), 21 + 4 + 6);
    QCOMPARE(frameLength(hex("ec")), 21);
    QCOMPARE(frameLength(hex("11")), 1);
    QCOMPARE(frameLength(hex("ed")), 0);
}

void ContecBleProtocolTests::testSplitterHandlesSplitAndMergedNotifications()
{
    const QByteArray f1 = frame({0xF1, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'});
    const QByteArray waveBody = hex("eb00054003");
    const QByteArray wave = waveBody + char(checksum(waveBody));
    const QByteArray stream = hex("1122") + f1 + wave + wave;

    FrameSplitter sp;
    QList<QByteArray> out = sp.feed(stream.left(5));
    out += sp.feed(stream.mid(5, 12));
    out += sp.feed(stream.mid(17));
    QCOMPARE(out.size(), 3);
    QCOMPARE(out[0], f1);
    QCOMPARE(out[1], wave);
    QCOMPARE(out[2], wave);
    QCOMPARE(sp.skippedBytes(), 2);
    QCOMPARE(parseF1(f1), QStringLiteral("ABCDEFGH"));
}

// Answers captured from a CMS50FW (firmware 2.0.0) on 27.09.2026.
void ContecBleProtocolTests::testParseDeviceAnswers()
{
    const DeviceInfo info = parseF2(hex("f200000206000d0700"));
    QCOMPARE(info.firmware, QStringLiteral("2.0.0"));
    QCOMPARE(info.protocolVersion, 13);

    const StorageStatus st = parseEF(hex("ef00014000000030"));
    QVERIFY(st.hasData);
    QVERIFY(st.hasRecords());
    QCOMPARE(st.mask2, 0);

    QCOMPARE(parseE0Count(hex("e0060600060000521f006c4c1b7026")), 6);
    QCOMPARE(parseFE06Formats(hex("fe060700000000")), 7);
    QCOMPARE(parseF1(frame({0xF1, '5', '0', 'F', ' ', ' ', ' ', ' ', ' '})), QStringLiteral("50F     "));
}

void ContecBleProtocolTests::testRecordHeaderAndMidnight()
{
    const int n = 21900;   // 6 h 05 min at 1 Hz
    QByteArray body;
    for (int v : {0xEC, 0x40, 5, 6, 26, 9, 25, 23, 55, 0, n & 0x7F, (n >> 7) & 0x7F, (n >> 14) & 0x7F, 0, 0, 0, 0, 0, 0, 0})
        body.append(char(v));
    const RecordHeader h = parseEC(body + char(checksum(body)));
    QVERIFY(h.last);
    QVERIFY(!h.hasPI);
    QCOMPARE(h.l, 5);
    QCOMPARE(h.m, 6);
    QCOMPARE(h.samples, n);
    QCOMPARE(h.start(), QDateTime(QDate(2026, 9, 25), QTime(23, 55, 0)));
    QCOMPARE(h.start().addSecs(h.samples - 1).date(), QDate(2026, 9, 26));   // runs past midnight

    QByteArray bad = body;
    bad[7] = char(30);   // hour 30 cannot be a valid start
    QVERIFY(!parseEC(bad + char(checksum(bad))).start().isValid());
}

void ContecBleProtocolTests::testEdVectorsMatchVendorCode()
{
    for (const EdVector &v : kEdVectors) {
        const QByteArray raw = hex(v.raw);
        QCOMPARE(parseEdOriginal(raw), ints(v.original));
        QCOMPARE(parseEdDifference(raw, v.version), ints(v.difference));
    }
}

// Expected values produced by the reference Python CodeDecoder; packet A ends with a lone
// escape byte that packet B completes (base 0x58 = 88).
void ContecBleProtocolTests::testCodeDecoderAcrossPackets()
{
    CodeDecoder d;
    const QVector<int> a = d.feed(hex("ed040100000300407670123f0021100f4405112330010203405060707534"));
    const QVector<int> b = d.feed(hex("ed040101003100007801122f76740013310f22100121123344044011004d"));
    QCOMPARE(a, QVector<int>({95, 94, 93, 96, 96, 94, 95, 95, 96, 96, 92, 92, 96, 91, 95, 95, 94, 93, 93, 96,
                              96, 95, 96, 94, 96, 93, 92, 96, 91, 96, 90, 96, 89, 96}));
    QCOMPARE(b, QVector<int>({88, 87, 87, 86, 86, 100, 100, 99, 97, 97, 99, 100, 98, 98, 99, 100, 100, 99, 98, 99,
                              99, 98, 97, 97, 96, 96, 100, 96, 96, 100, 99, 99, 100, 100}));
}
```

- [ ] **Step 4: Подключить в `oscar/oscar.pro`**

Основные `SOURCES`: после строки `    SleepLib/loader_plugins/md300w1_loader.cpp \` добавить `    SleepLib/loader_plugins/contec_ble_protocol.cpp \`. Основные `HEADERS`: после `    SleepLib/loader_plugins/md300w1_loader.h \` добавить `    SleepLib/loader_plugins/contec_ble_protocol.h \`. В блоке `test {`: в `SOURCES` последнюю строку `        tests/machinetests.cpp` заменить на
```
        tests/machinetests.cpp \
        tests/contecbleprotocoltests.cpp
```
и в `HEADERS` `        tests/machinetests.h` на
```
        tests/machinetests.h \
        tests/contecbleprotocoltests.h
```
(Точные строки проверить `grep -n "md300w1_loader\|tests/machinetests" oscar/oscar.pro`; если строка — последняя в списке без `\`, добавить `\` к ней.)

- [ ] **Step 5: Убедиться, что сборка тестов падает**

Создать пустой `oscar/SleepLib/loader_plugins/contec_ble_protocol.cpp` с одной строкой `#include "contec_ble_protocol.h"`, перезапустить qmake в `build-test` (см. Global Constraints) и собрать:
Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E 'Undefined symbols|error:' | head -3`
Expected: `Undefined symbols … ContecBle::…`.

- [ ] **Step 6: Реализация `oscar/SleepLib/loader_plugins/contec_ble_protocol.cpp`** (заменить заглушку)

```cpp
/* Contec BLE Oximeter Protocol
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contec_ble_protocol.h"

namespace ContecBle {

namespace {

const quint8 kXorMask = 0x56;   // value obfuscation used by protocol versions above 13

struct NamedModel { const char *prefix; const char *model; char variant; };
const NamedModel kModels[] = {
    { "SpO201", "CMS50EW", 'A' }, { "SpO202", "CMS50FW", 'A' }, { "SpO206", "CMS50IW", 'A' },
    { "SpO208", "CMS50D-BT", 'A' }, { "SpO209", "CMS50K", 'K' }, { "SpO210", "CMS50K1", 'K' },
    { "SpO211", "CMS50S", 'A' }, { "SpO212", "CMS60D1", 'A' }, { "SpO213", "CMS50S+", 'A' },
};

int fixedLength(quint8 h)
{
    switch (h) {
    case 0x83: return 22;
    case 0xCF: return 10;
    case 0xD0: return 14;
    case 0xD1: return 4;
    case 0xD2: case 0xD3: case 0xD7: return 20;
    case 0xE1: case 0xE2: return 11;
    case 0xE3: return 9;
    case 0xE4: return 17;
    case 0xE5: return 18;
    case 0xE6: return 17;
    case 0xEA: return 13;
    case 0xEC: return 21;
    case 0xEF: return 8;
    case 0xF0: return 2;
    case 0xF1: return 10;
    case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xFA: case 0xFB: case 0xFF: return 3;
    default: return 0;
    }
}

inline int at(const QByteArray &f, int i) { return quint8(f.at(i)); }

// Puts the high bits kept in hi-bytes back into three groups of seven data bytes (ED 03 / ED 04).
QVector<int> restoreEd(const QByteArray &f)
{
    QVector<int> out;
    out.reserve(21);
    const int groups[3][2] = { { 8, 5 }, { 15, 6 }, { 22, 7 } };
    for (const auto &g : groups) {
        for (int i = 0; i < 7; ++i) {
            out.append((at(f, g[0] + i) & 0x7F) | (((at(f, g[1]) >> i) & 1) << 7));
        }
    }
    return out;
}

} // namespace

ModelInfo modelForName(const QString &advertisedName)
{
    ModelInfo best;
    int bestLength = 0;
    for (const NamedModel &m : kModels) {
        const QString prefix = QString::fromLatin1(m.prefix);
        if (advertisedName.startsWith(prefix) && prefix.size() > bestLength) {
            best.model = QString::fromLatin1(m.model);
            best.variant = m.variant;
            bestLength = prefix.size();
        }
    }
    return best;
}

quint8 checksum(const QByteArray &body)
{
    int sum = 0;
    for (char c : body) sum += quint8(c);
    return quint8(sum & 0x7F);
}

QByteArray frame(std::initializer_list<int> body)
{
    QByteArray b;
    for (int v : body) b.append(char(v & 0xFF));
    b.append(char(checksum(b)));
    return b;
}

void pack7(const QByteArray &data, QByteArray &hi, QByteArray &lo)
{
    hi = QByteArray((data.size() + 6) / 7, '\0');
    lo.resize(data.size());
    for (int i = 0; i < data.size(); ++i) {
        const int b = quint8(data[i]);
        hi[i / 7] = char(quint8(hi[i / 7]) | (((b >> 7) & 1) << (i % 7)));
        lo[i] = char(b & 0x7F);
    }
}

QByteArray unpack7(const QByteArray &hi, const QByteArray &lo)
{
    QByteArray out(lo.size(), '\0');
    for (int i = 0; i < lo.size(); ++i) {
        const int h = (i / 7 < hi.size()) ? at(hi, i / 7) : 0;
        out[i] = char((at(lo, i) & 0x7F) | (((h >> (i % 7)) & 1) << 7));
    }
    return out;
}

quint32 lo7(const QByteArray &f, int from, int count)
{
    quint32 v = 0;
    for (int i = 0; i < count; ++i) v |= quint32(at(f, from + i) & 0x7F) << (7 * i);
    return v;
}

int frameLength(const QByteArray &buf, char variant)
{
    if (buf.isEmpty()) return 0;
    const quint8 h = quint8(buf[0]);
    if (h < 0x80) return 1;
    if (const int n = fixedLength(h)) return n;
    if (buf.size() < 2) return 0;
    const int b1 = at(buf, 1);
    switch (h) {
    case 0x84: { const int n = b1 & 0x7F; return n + (n + 3 + 6) / 7 + 6; }
    case 0xE0: return (variant == 'A' && (b1 & 7) == 6) ? 15 : 7;
    case 0xEB:
        switch (b1) { case 0x00: return 6; case 0x01: case 0x02: return 8; case 0x7F: return 3; default: return 2; }
    case 0xED:
        switch (b1) { case 0x01: return 24; case 0x03: case 0x04: return 30; case 0x7F: return 7; default: return 2; }
    case 0xFE:
        switch (b1) { case 0x09: return 11; case 0x07: return 5; case 0x06: return 7; default: return 2; }
    case 0xF2: {
        if (buf.size() < 9) return 0;
        const int l1 = at(buf, 8) & 0x7F;
        if (l1 == 0) return 9;
        if (buf.size() < l1 + 11) return 0;
        return l1 + (at(buf, l1 + 10) & 0x7F) + 11;
    }
    default:
        return 1;
    }
}

QList<QByteArray> FrameSplitter::feed(const QByteArray &data)
{
    m_buf += data;
    QList<QByteArray> out;
    while (!m_buf.isEmpty()) {
        const int n = frameLength(m_buf, m_variant);
        if (n == 0 || n > m_buf.size()) break;
        if (n == 1) {              // stray byte or unknown header: resynchronise like the SDK
            ++m_skipped;
            m_buf.remove(0, 1);
            continue;
        }
        out.append(m_buf.left(n));
        m_buf.remove(0, n);
    }
    return out;
}

quint8 formatCode(int format)
{
    switch (format) {
    case FmtDifference: return 0x01;
    case FmtCode: return 0x04;
    default: return 0x03;
    }
}

QByteArray cmdId()           { return frame({0x81}); }
QByteArray cmdInfo()         { return frame({0x82}); }
QByteArray cmdPrepare()      { return frame({0x8F, 0x04, 0x00}); }
QByteArray cmdStorage()      { return frame({0x9F}); }
QByteArray cmdFormats()      { return frame({0x8E, 0x06}); }
QByteArray cmdCountRecords() { return frame({0x90, 0x06}); }
QByteArray cmdNextHeader()   { return frame({0x9C, 0x01}); }

QByteArray cmdChannel(int format, int channel, int l, int m, int offset)
{
    if (format == FmtDifference) {   // 14-bit packet offset
        return frame({0x9D, 0x01, channel, l, m, offset & 0x7F, (offset >> 7) & 0x7F});
    }
    return frame({0x9D, formatCode(format), channel, l, m,   // 21-bit packet offset
                  offset & 0x7F, (offset >> 7) & 0x7F, (offset >> 14) & 0x7F});
}

QByteArray cmdChannelAbort(int channel, int l, int m)
{
    return frame({0x9D, 0x02, channel, l, m, 0x00, 0x00});
}

QByteArray cmdSetTime(const QDateTime &localTime)
{
    const QDate d = localTime.date();
    const QTime t = localTime.time();
    const int ms = t.msec();
    return frame({0x83, (d.year() - 2000) & 0x7F, d.month(), d.day(), t.hour(), t.minute(), t.second(),
                  ms & 0x7F, (ms >> 7) & 0x7F});
}

QByteArray cmdEraseAllRecords()
{
    return frame({0x9D, 0x7F, 0x7F, 0x7F, 0x7F, 0x00, 0x00});
}

bool isDestructive(const QByteArray &cmd)
{
    if (cmd.isEmpty()) return false;
    const int h = at(cmd, 0);
    if (h == 0xA1) return true;                                     // erase continuous data
    if (h == 0x9D && cmd.size() >= 2 && at(cmd, 1) == 0x7F) return true;   // erase records
    return (h == 0x91 || h == 0x92 || h == 0x94 || h == 0x96) && cmd.size() == 3 && at(cmd, 1) == 0x7F;
}

int pickFormat(int supported)
{
    for (int f : { int(FmtOriginal), int(FmtCode), int(FmtDifference) }) {
        if (supported & f) return f;
    }
    return FmtDifference;
}

QString parseF1(const QByteArray &f)
{
    QByteArray s;
    for (int i = 1; i < 9 && i < f.size(); ++i) s.append(char(at(f, i) & 0x7F));
    return QString::fromLatin1(s);
}

DeviceInfo parseF2(const QByteArray &f)
{
    DeviceInfo info;
    info.firmware = QStringLiteral("%1.%2.%3").arg(at(f, 3) & 0x7F).arg(at(f, 2) & 0x7F).arg(at(f, 1) & 0x7F);
    info.protocolVersion = at(f, 6) & 0x7F;
    return info;
}

StorageStatus parseEF(const QByteArray &f)
{
    StorageStatus st;
    st.busy = at(f, 1) & 1;
    st.hasData = at(f, 2) & 1;
    st.mask1 = at(f, 3) & 0x7F;
    st.mask2 = at(f, 5) & 0x7F;
    return st;
}

int parseE0Count(const QByteArray &f) { return int(lo7(f, 2, 2)); }
int parseFE06Formats(const QByteArray &f) { return at(f, 2) & 0x7F; }

QDateTime RecordHeader::start() const
{
    const QDate d(year, month, day);
    const QTime t(hour, minute, second);
    if (!d.isValid() || !t.isValid()) return QDateTime();
    return QDateTime(d, t);
}

RecordHeader parseEC(const QByteArray &f)
{
    RecordHeader h;
    h.last = at(f, 1) & 0x40;
    h.hasPI = at(f, 1) & 0x0F;
    h.l = at(f, 2) & 0x7F;
    h.m = at(f, 3) & 0x7F;
    h.year = (at(f, 4) & 0x7F) + 2000;
    h.month = at(f, 5) & 0x0F;
    h.day = at(f, 6) & 0x1F;
    h.hour = at(f, 7) & 0x1F;
    h.minute = at(f, 8) & 0x3F;
    h.second = at(f, 9) & 0x3F;
    h.samples = int(lo7(f, 10, 4));
    return h;
}

int edPacketNo(const QByteArray &f)
{
    return at(f, 1) == 0x01 ? int(lo7(f, 5, 2)) : int(lo7(f, 3, 2));
}

bool checksumOk(const QByteArray &f)
{
    return !f.isEmpty() && checksum(f.left(f.size() - 1)) == quint8(f.back());
}

QVector<int> parseEdOriginal(const QByteArray &f)
{
    return restoreEd(f);
}

// One base value and 13 bytes of nibble deltas -> 27 samples; a delta of 7 means "no data" (255).
QVector<int> parseEdDifference(const QByteArray &f, int protocolVersion)
{
    const int base = 9, hiA = 7, hiB = 8;
    QVector<int> b(f.size());
    for (int i = 0; i < f.size(); ++i) b[i] = at(f, i);
    for (int i = 0; i < 7; ++i) {
        b[base + i] |= ((b[hiA] >> i) & 1) << 7;
        b[base + 7 + i] |= ((b[hiB] >> i) & 1) << 7;
    }
    QVector<int> s;
    s.reserve(27);
    s.append(b[base] ^ (protocolVersion > 13 ? kXorMask : 0));
    for (int k = base + 1; k < base + 14; ++k) {
        const int v = b[k];
        const int deltas[2][2] = { { (v >> 4) & 7, v & 0x80 }, { v & 7, v & 0x08 } };
        for (const auto &d : deltas) {
            s.append(d[0] == 7 ? 255 : (d[1] ? s.last() - d[0] : s.last() + d[0]));
        }
    }
    return s;
}

QVector<int> CodeDecoder::feed(const QByteArray &f)
{
    const QVector<int> b = restoreEd(f);
    QVector<int> out;
    int i = 0;
    if (m_pending >= 0 && !b.isEmpty() && (b[0] & 0xF0) == 0xF0) {
        m_base = ((m_pending & 0x0F) << 4) | (b[0] & 0x0F);
        i = 1;
    }
    m_pending = -1;
    for (; i < b.size(); ++i) {
        const int v = b[i];
        if ((v & 0xF0) == 0xF0) {
            if (i + 1 < b.size()) {
                if ((b[i + 1] & 0xF0) == 0xF0) {
                    m_base = ((v & 0x0F) << 4) | (b[i + 1] & 0x0F);
                    ++i;
                }
            } else {
                m_pending = v;
            }
        } else {
            out.append((m_base - (v >> 4)) & 0xFF);
            if ((v & 0x0F) != 0x0F) out.append((m_base - (v & 0x0F)) & 0xFF);
        }
    }
    return out;
}

} // namespace ContecBle
```

- [ ] **Step 7: Прогнать тесты**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; /Users/semyk/Downloads/Oscar_Project/tools/runtests.sh ContecBleProtocolTests`
Expected: `ContecBleProtocolTests: 14 PASS` (12 тестов + init/cleanup), `0 unexpected`.

- [ ] **Step 8: Сборка приложения и предупреждения**

Run: перезапустить qmake в `build` (Global Constraints), затем
```bash
cd /Users/semyk/Downloads/Oscar_Project/build && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; /Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh contec_ble_protocol | comm -13 /Users/semyk/Downloads/Oscar_Project/tools/warn-baseline-ble.txt -
```
Expected: пусто.

- [ ] **Step 9: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/SleepLib/loader_plugins/contec_ble_protocol.h oscar/SleepLib/loader_plugins/contec_ble_protocol.cpp oscar/tests/contecbleprotocoltests.h oscar/tests/contecbleprotocoltests.cpp oscar/oscar.pro
git commit -m "Add the Contec BLE oximeter protocol: framing, commands and record parsing

Wire format of the Contec CMS50/CMS60 Bluetooth LE oximeters (variant A:
CMS50EW/FW/IW/D-BT/S/S+, CMS60D1), reverse-engineered for interoperability
from the vendor app and checked against a CMS50FW. Pure functions only;
tests use vectors produced by the vendor Java parsers.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Протокол — шифрование (версия протокола > 13)

**Files:**
- Modify: `oscar/SleepLib/loader_plugins/contec_ble_protocol.h` (перед `} // namespace ContecBle`), `…/contec_ble_protocol.cpp`
- Modify: `oscar/tests/contecbleprotocoltests.h`, `oscar/tests/contecbleprotocoltests.cpp`

**Interfaces:**
- Consumes: `pack7/unpack7/checksum` (Task 1).
- Produces: `QByteArray kdf(const QByteArray &seed16, const QByteArray &salt)`, `QByteArray aesCtr(const QByteArray &data, const QByteArray &key16, const QByteArray &iv16)`, `QByteArray appSeed(const QDateTime &now)`, `QByteArray buildF3(const QByteArray &seed16)`, `QByteArray seedFrom83(const QByteArray &f)`, `struct Keys{QByteArray keyTx, ivTx, keyRx, ivRx;}`, `void deriveRx(seedApp, salt, key&, iv&)`, `void deriveTx(seedDev, salt, key&, iv&)`, `QByteArray buildF4(const QByteArray &cmd, const Keys&)`, `QByteArray open84(const QByteArray &f, const Keys&)`.

- [ ] **Step 1: Объявления в заголовке** (перед `} // namespace ContecBle`)

```cpp
// ---- Secure mode (variant A, protocol version > 13): AES-128-CTR with keys derived from open data.
// Implemented from the vendor app; not yet verified on a device with protocol version > 13.

//! The vendor key/IV derivation (liblib_encryptkeyiv.so getEncryptArray).
QByteArray kdf(const QByteArray &seed16, const QByteArray &salt);
//! AES-128 in CTR mode with the whole 16-byte IV as a big-endian counter (Java AES/CTR/NoPadding).
QByteArray aesCtr(const QByteArray &data, const QByteArray &key16, const QByteArray &iv16);
//! 16-byte app seed built from the clock exactly like the vendor app.
QByteArray appSeed(const QDateTime &now);
//! Key-exchange frame F3 10 hi[3] lo[16] cs (22 bytes, sent in clear).
QByteArray buildF3(const QByteArray &seed16);
//! Device seed from its 22-byte 0x83 answer.
QByteArray seedFrom83(const QByteArray &f);

struct Keys {
    QByteArray keyTx, ivTx;   //!< encrypt commands (F4)
    QByteArray keyRx, ivRx;   //!< decrypt answers (84)
};
void deriveRx(const QByteArray &seedApp, const QByteArray &salt, QByteArray &key, QByteArray &iv);
void deriveTx(const QByteArray &seedDev, const QByteArray &salt, QByteArray &key, QByteArray &iv);
//! Wraps a plain command into a secure F4 frame.
QByteArray buildF4(const QByteArray &cmd, const Keys &keys);
//! Decrypts a secure 0x84 frame into a byte stream of ordinary frames.
QByteArray open84(const QByteArray &f, const Keys &keys);
```

- [ ] **Step 2: Падающие тесты**

В `contecbleprotocoltests.h` в `private slots:` добавить:
```cpp
    void testKdfMatchesReference();
    void testSecureFramesMatchVendorCode();
    void testF3FromVendorVector();
    void testAppSeedLayout();
    void testAesCtrCarriesAcrossTheWholeIv();
```
В `contecbleprotocoltests.cpp` внутри анонимного namespace (после `kEdVectors`) добавить:
```cpp
// Secure-frame vectors from the vendor Java code: key, iv, plain command, F4 frame,
// and the payload of the same frame when read back as an 0x84 answer.
struct SecureVector { const char *key, *iv, *plain, *frame, *rx; };
const SecureVector kSecureVectors[] = {
    { "359d41baf78afe0de1bbe7ae28c0450c", "e43c084f4bbb2bf1839dee466d020100", "0b73392f3023583b61647f751337385f7731661613", "f4156177670035453333723935502f3b345a61040c205f36185e1d0001020c", "0b73392f3023583b61647f751337385f7731661613" },
    { "f450279849599b56dd53b3351a572b40", "f27f72d37f347b5d9979162cfa020100", "236052490a4a0b606604282d196801", "f40f7605010b051e3c6931107e072e365400795700010223", "236052490a4a0b606604282d196801" },
    { "1192ed6a754300c523675af9b6c4a514", "cacaa1b6a736738853ee067b87020100", "1c794c691851302f112e3737533a", "f40e6a6f0020581312486a601f593f275b54120001022c", "1c794c691851302f112e3737533a" },
    { "79092bb2831b0279bb5070f331dc1178", "6481868c3667cbd3ed4091091e020100", "6a643a612350192c167b6e3d3e4635", "f40f120c013f6f39372d4c34152f536e236f3b1600010258", "6a643a612350192c167b6e3d3e4635" },
    { "a917160239c103a2db67f4c18eef29b3", "2fa68ba1aa5db9501dfbfb1177020100", "2f256f13", "f40408196a3a2b0001026b", "2f256f13" },
};
const char *const kF3Vector = "f310187e031a091b112a3253051a091b112a32530522";
// Produced by the reference Python port, which was checked 300/300 against the vendor ARM code.
struct KdfVector { const char *seed; const char *salt; const char *out; };
const KdfVector kKdfVectors[] = {
    { "000102030405060708090a0b0c0d0e0f", "50F     ", "007a97ac9fc5bff73417aad07f2eb7ca" },
    { "ffffffffffffffffffffffffffffffff", "SpO202", "7cd6d1d0f1dad5ea6211d6f5a2bafeca" },
    { "1a091b112a325305061a091b112a3253", "ABCDEFGH", "6bd6f7c7dafd82a3d761cb57b6d8e672" },
    { "80017f33009910ee0506070809a0b0c0", "", "c0a11c44c3e1aaae91c682c7cfdb688d" },
};
```
и в конец файла:
```cpp
void ContecBleProtocolTests::testKdfMatchesReference()
{
    for (const KdfVector &v : kKdfVectors) {
        QCOMPARE(kdf(hex(v.seed), QByteArray(v.salt)), hex(v.out));
    }
}

void ContecBleProtocolTests::testSecureFramesMatchVendorCode()
{
    for (const SecureVector &v : kSecureVectors) {
        Keys keys;
        keys.keyTx = keys.keyRx = hex(v.key);
        keys.ivTx = keys.ivRx = hex(v.iv);
        const QByteArray f4 = buildF4(hex(v.plain), keys);
        QCOMPARE(f4, hex(v.frame));
        QCOMPARE(frameLength(f4), f4.size());
        QByteArray as84 = f4;
        as84[0] = char(0x84);
        QCOMPARE(open84(as84, keys), hex(v.rx));
    }
}

void ContecBleProtocolTests::testF3FromVendorVector()
{
    const QByteArray a = hex(kF3Vector);
    const QByteArray seed = unpack7(a.mid(2, 3), a.mid(5, 16));
    QCOMPARE(buildF3(seed), a);
    QCOMPARE(seedFrom83(a), seed);
}

void ContecBleProtocolTests::testAppSeedLayout()
{
    const QByteArray s = appSeed(QDateTime(QDate(2026, 9, 27), QTime(18, 5, 7, 300)));
    QByteArray expected;
    for (int v : {26, 9, 27, 18 | 0x80, 5 | 0x80, 7, 300 & 0x7F, 300 >> 7,
                  26 | 0x80, 9 | 0x80, 27 | 0x80, 18 | 0x80, 5 | 0x80, 7 | 0x80, (300 & 0x7F) | 0x80, (300 >> 7) | 0x80})
        expected.append(char(v));
    QCOMPARE(s, expected);
    const QByteArray f3 = buildF3(s);
    QCOMPARE(f3.size(), 22);
    QCOMPARE(f3.left(2), hex("f310"));
    for (int i = 2; i < f3.size(); ++i) QVERIFY(quint8(f3[i]) < 0x80);
    QCOMPARE(seedFrom83(f3), s);
}

// Java's CTR increments the whole 128-bit IV: the second block of a stream that starts at
// ...00ff must equal a stream that starts at ...0100.
void ContecBleProtocolTests::testAesCtrCarriesAcrossTheWholeIv()
{
    const QByteArray key = hex("000102030405060708090a0b0c0d0e0f");
    const QByteArray zeros(32, '\0');
    const QByteArray a = aesCtr(zeros, key, hex("000000000000000000000000000000ff"));
    const QByteArray b = aesCtr(QByteArray(16, '\0'), key, hex("00000000000000000000000000000100"));
    QCOMPARE(a.size(), 32);
    QCOMPARE(a.mid(16), b);
    QCOMPARE(aesCtr(a, key, hex("000000000000000000000000000000ff")), zeros);   // CTR is its own inverse
}
```

- [ ] **Step 3: Убедиться, что сборка тестов падает**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E 'Undefined symbols|error:' | head -3`
Expected: неопределённые `ContecBle::kdf`/`aesCtr`/… .

- [ ] **Step 4: Реализация** — в `contec_ble_protocol.cpp` добавить к include'ам
```cpp
#include "SleepLib/thirdparty/botan_all.h"
#include <QDebug>
#include <cstring>
#include <memory>
```
и перед `} // namespace ContecBle`:
```cpp
QByteArray kdf(const QByteArray &seed16, const QByteArray &salt)
{
    quint8 k[8] = { 0xC3, 0xED, 0xF3, 0x80, 0x80, 0x95, 0xD2, 0x89 };
    for (int i = 0; i < salt.size(); ++i) {
        const int c = at(salt, i);
        k[i % 8] ^= (i & 1) ? quint8(c) : quint8(~(c << 1) & 0xFF);
    }
    QByteArray out(16, '\0');
    for (int i = 0; i < 16 && i < seed16.size(); ++i) {
        const int a = at(seed16, i);
        const int kk = k[(i >> 1) & 7];
        const int v = (i & 1) == 0 ? ((kk ^ (a >> 1)) ^ ((kk / (i + 1)) + a))
                                   : ((a + (kk >> 1)) ^ kk);
        out[i] = char(v & 0xFF);
    }
    return out;
}

QByteArray aesCtr(const QByteArray &data, const QByteArray &key16, const QByteArray &iv16)
{
    if (key16.size() != 16 || iv16.size() != 16) return QByteArray();
    try {
        std::unique_ptr<Botan::BlockCipher> aes = Botan::BlockCipher::create("AES-128");
        if (!aes) return QByteArray();
        aes->set_key(reinterpret_cast<const uint8_t *>(key16.constData()), size_t(key16.size()));
        uint8_t counter[16];
        std::memcpy(counter, iv16.constData(), 16);
        uint8_t stream[16];
        QByteArray out(data.size(), '\0');
        for (int off = 0; off < data.size(); off += 16) {
            aes->encrypt_n(counter, stream, 1);
            for (int i = 0; i < 16 && off + i < data.size(); ++i) {
                out[off + i] = char(at(data, off + i) ^ stream[i]);
            }
            for (int j = 15; j >= 0; --j) {       // 128-bit big-endian increment
                if (++counter[j] != 0) break;
            }
        }
        return out;
    } catch (const std::exception &e) {
        qWarning() << "ContecBle::aesCtr failed:" << e.what();
        return QByteArray();
    }
}

QByteArray appSeed(const QDateTime &now)
{
    const QDate d = now.date();
    const QTime t = now.time();
    const int yy = (d.year() - 2000) & 0x7F, mo = d.month(), dd = d.day();
    const int hh = t.hour() | 0x80, mi = t.minute() | 0x80, ss = t.second();
    const int ms = t.msec();
    const int msl = ms & 0x7F, msh = (ms >> 7) & 0x7F;
    QByteArray s;
    for (int v : { yy, mo, dd, hh, mi, ss, msl, msh,
                   yy | 0x80, mo | 0x80, dd | 0x80, hh, mi, ss | 0x80, msl | 0x80, msh | 0x80 })
        s.append(char(v));
    return s;
}

QByteArray buildF3(const QByteArray &seed16)
{
    QByteArray hi, lo;
    pack7(seed16, hi, lo);
    QByteArray body;
    body.append(char(0xF3));
    body.append(char(0x10));
    body += hi + lo;
    return body + char(checksum(body));
}

QByteArray seedFrom83(const QByteArray &f)
{
    return unpack7(f.mid(2, 3), f.mid(5, 16));
}

void deriveRx(const QByteArray &seedApp, const QByteArray &salt, QByteArray &key, QByteArray &iv)
{
    QByteArray reversed(salt);
    std::reverse(reversed.begin(), reversed.end());
    key = kdf(seedApp, salt);
    iv = kdf(seedApp, reversed);
}

void deriveTx(const QByteArray &seedDev, const QByteArray &salt, QByteArray &key, QByteArray &iv)
{
    QByteArray reversed(salt);
    std::reverse(reversed.begin(), reversed.end());
    key = kdf(seedDev, salt);
    iv = kdf(seedDev, reversed).left(13) + QByteArray::fromHex("020100");
}

QByteArray buildF4(const QByteArray &cmd, const Keys &keys)
{
    const QByteArray c = aesCtr(cmd, keys.keyTx, keys.ivTx.left(13) + QByteArray::fromHex("020100"));
    QByteArray hi, lo;
    pack7(c + QByteArray::fromHex("000102"), hi, lo);
    QByteArray body;
    body.append(char(0xF4));
    body.append(char(c.size()));
    body += hi + lo;
    return body + char(checksum(body));
}

QByteArray open84(const QByteArray &f, const Keys &keys)
{
    const int n = at(f, 1) & 0x7F;
    const int nh = (n + 3 + 6) / 7;
    const QByteArray raw = unpack7(f.mid(2, nh), f.mid(2 + nh, n + 3));
    if (raw.size() < n + 3) return QByteArray();
    QByteArray iv = keys.ivRx.left(13);
    iv.append(raw[n + 2]);
    iv.append(raw[n + 1]);
    iv.append(raw[n]);
    return aesCtr(raw.left(n), keys.keyRx, iv);
}
```
и добавить `#include <algorithm>` к include'ам (для `std::reverse`).

- [ ] **Step 5: Прогнать тесты**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; /Users/semyk/Downloads/Oscar_Project/tools/runtests.sh ContecBleProtocolTests`
Expected: `ContecBleProtocolTests: 19 PASS`, `0 unexpected`.

- [ ] **Step 6: Приложение и предупреждения**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; /Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh contec_ble_protocol | comm -13 /Users/semyk/Downloads/Oscar_Project/tools/warn-baseline-ble.txt -`
Expected: пусто (Botan-заголовок исключается фильтром `/opt/homebrew/`? — нет, он в `oscar/SleepLib/thirdparty`; если появятся строки `oscar/SleepLib/thirdparty/botan_all.h: warning`, это чужой код — отметить в журнале, в сравнении учитывать только `contec_ble_protocol.*`).

- [ ] **Step 7: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/SleepLib/loader_plugins/contec_ble_protocol.h oscar/SleepLib/loader_plugins/contec_ble_protocol.cpp oscar/tests/contecbleprotocoltests.h oscar/tests/contecbleprotocoltests.cpp
git commit -m "Add Contec BLE secure mode (protocol version above 13)

AES-128-CTR with the vendor key derivation, the F3/83 seed exchange and
F4/84 secure frames, implemented from the vendor app on top of Botan.
Verified against vectors from the vendor code; not yet verified on a
device that uses it.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Сеанс выгрузки `ContecBleDownloader` + симулятор прибора

**Files:**
- Create: `oscar/SleepLib/loader_plugins/contec_ble_downloader.h`, `…/contec_ble_downloader.cpp`
- Create: `oscar/tests/fakecontecdevice.h`, `oscar/tests/fakecontecdevice.cpp`
- Create: `oscar/tests/contecbledownloadertests.h`, `oscar/tests/contecbledownloadertests.cpp`
- Modify: `oscar/oscar.pro`

**Interfaces:**
- Consumes: всё из Task 1–2.
- Produces: `class ContecBleLink : QObject { virtual void write(const QByteArray&) = 0; signals: received(QByteArray), linkLost(); }`; `class ContecBleDownloader : QObject` — `setWantRecord(std::function<bool(const ContecBle::RecordHeader&)>)`, `setResponseTimeout(int)`, `setRetryPause(int)`, `allowDestructive(bool)`, `start(ContecBleLink*, const QString &advertisedName)`, `cancel()`, `setClock(const QDateTime&)`, `eraseAllRecords()`, `isEncrypted()`; сигналы `deviceIdentified(QString model, QString firmware, int version)`, `recordCountKnown(int)`, `recordDownloaded(const ContecBle::Record&)`, `progress(int done, int total)`, `downloadFinished()`, `clockSet(bool)`, `eraseFinished(bool)`, `failed(QString)`.

- [ ] **Step 1: Заголовок `contec_ble_downloader.h`**

```cpp
/* Contec BLE Oximeter Download Session Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CONTEC_BLE_DOWNLOADER_H
#define CONTEC_BLE_DOWNLOADER_H

#include <QObject>
#include <QTimer>
#include <functional>

#include "SleepLib/loader_plugins/contec_ble_protocol.h"

//! Byte pipe to an oximeter: the Bluetooth link in the app, a simulator in the unit tests.
class ContecBleLink : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~ContecBleLink() override = default;
    //! Sends raw bytes (already framed, and encrypted in secure mode).
    virtual void write(const QByteArray &data) = 0;
signals:
    void received(const QByteArray &data);
    void linkLost();
};

/*! \class ContecBleDownloader
    \brief Runs a Contec variant-A session: handshake, optional key exchange, then every stored
    record the owner wants. Asynchronous (QTimer, no blocking waits). Never erases anything
    unless allowDestructive(true) was called first. */
class ContecBleDownloader : public QObject
{
    Q_OBJECT
public:
    explicit ContecBleDownloader(QObject *parent = nullptr);

    void setWantRecord(std::function<bool(const ContecBle::RecordHeader &)> want) { m_wantRecord = std::move(want); }
    void setResponseTimeout(int ms) { m_responseTimeoutMs = ms; }
    void setRetryPause(int ms) { m_retryPauseMs = ms; }
    void allowDestructive(bool allow) { m_allowDestructive = allow; }

    void start(ContecBleLink *link, const QString &advertisedName);
    //! Stops silently; later answers are ignored.
    void cancel();
    //! After downloadFinished(): sets the oximeter clock; answers with clockSet().
    void setClock(const QDateTime &localNow);
    //! After downloadFinished(): erases every stored record; needs allowDestructive(true).
    void eraseAllRecords();

    bool isEncrypted() const { return m_encrypted; }

signals:
    void deviceIdentified(const QString &model, const QString &firmware, int protocolVersion);
    void recordCountKnown(int count);
    void recordDownloaded(const ContecBle::Record &record);
    void progress(int done, int total);
    void downloadFinished();
    void clockSet(bool ok);
    void eraseFinished(bool ok);
    void failed(const QString &message);

private:
    enum class State { Idle, WaitId, WaitInfo, WaitSeed, WaitStorage, WaitPrepare, WaitStorageRetry,
                       WaitCount, WaitFormats, WaitHeader, ReadChannel, RetryPause, Ready,
                       WaitSetTime, WaitErase, Failed, Cancelled };

    void send(const QByteArray &cmd);
    void expect(State next, int timeoutMs = -1);
    void onReceived(const QByteArray &data);
    void onFrame(const QByteArray &f);
    void onTimeout();
    void onLinkLost();
    void fail(const QString &message);
    void startKeyExchange();
    void requestStorage();
    void requestHeader();
    void onHeader(const QByteArray &f);
    void startChannel();
    void onChannelPacket(const QByteArray &f);
    void retryChannel();
    void finishRecord();
    void finishDownload();
    bool stopped() const { return m_state == State::Failed || m_state == State::Cancelled; }

    ContecBleLink *m_link = nullptr;
    ContecBle::ModelInfo m_model;
    State m_state = State::Idle;
    QTimer m_timer;
    int m_generation = 0;
    int m_responseTimeoutMs = 5000;
    int m_retryPauseMs = 500;
    bool m_allowDestructive = false;
    std::function<bool(const ContecBle::RecordHeader &)> m_wantRecord;

    ContecBle::FrameSplitter m_outer{'A'};
    ContecBle::FrameSplitter m_inner{'A'};
    bool m_encrypted = false;
    ContecBle::Keys m_keys;
    QString m_deviceId;
    int m_version = 0;

    int m_total = 0;
    int m_done = 0;
    int m_format = ContecBle::FmtOriginal;
    ContecBle::Record m_record;
    QList<int> m_channels;
    int m_channelIndex = 0;
    int m_packet = 0;
    int m_attempt = 0;
    QVector<int> m_samples;
    ContecBle::CodeDecoder m_decoder;
};

#endif // CONTEC_BLE_DOWNLOADER_H
```

- [ ] **Step 2: Симулятор `oscar/tests/fakecontecdevice.h`**

```cpp
/* Simulated Contec BLE oximeter for unit tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef FAKECONTECDEVICE_H
#define FAKECONTECDEVICE_H

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QVector>

#include "SleepLib/loader_plugins/contec_ble_downloader.h"

/*! A variant-A oximeter that answers from synthetic records (ORIGINAL format only), with
    optional secure mode (protocol version > 13) and fault injection. Answers are delivered
    asynchronously in notification-sized chunks, like the real link. */
class FakeContecDevice : public ContecBleLink
{
    Q_OBJECT
public:
    struct Rec { QDateTime start; QVector<int> spo2; QVector<int> pulse; };

    explicit FakeContecDevice(int protocolVersion = 13, QObject *parent = nullptr);
    void write(const QByteArray &data) override;
    void dropLink() { emit linkLost(); }

    QList<Rec> records;
    QByteArray deviceId = QByteArrayLiteral("50F     ");
    QSet<int> silentCommands;        //!< never answer these command headers
    QSet<int> rejectCommands;        //!< answer F0 70
    int silentOnceCommand = -1;      //!< ignore this command header the first time only
    int corruptChannel = 0;          //!< corrupt packet corruptPacket of this channel once
    int corruptPacket = -1;

    QList<QByteArray> commands;      //!< every command received (decrypted in secure mode)
    bool sawEncryptedCommand = false;
    bool erased = false;
    QDateTime clockSetTo;

private:
    int commandLength(const QByteArray &buf) const;
    void handle(const QByteArray &cmd);
    void reply(const QByteArray &frame);
    void sendPackets(int channel, int m, int offset);
    void keyExchange(const QByteArray &f3);

    QByteArray m_in;
    int m_version;
    int m_nextHeader = 0;
    bool m_encrypted = false;
    ContecBle::Keys m_decrypt;   //!< app tx keys, used to open F4
    ContecBle::Keys m_encrypt;   //!< app rx keys, used to build 84
};

#endif // FAKECONTECDEVICE_H
```

- [ ] **Step 3: Симулятор `oscar/tests/fakecontecdevice.cpp`**

```cpp
/* Simulated Contec BLE oximeter for unit tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "fakecontecdevice.h"

#include <QMetaObject>

using namespace ContecBle;

FakeContecDevice::FakeContecDevice(int protocolVersion, QObject *parent)
    : ContecBleLink(parent), m_version(protocolVersion)
{
}

int FakeContecDevice::commandLength(const QByteArray &buf) const
{
    const int h = quint8(buf[0]);
    switch (h) {
    case 0x81: case 0x82: case 0x9F: case 0x9A: return 2;
    case 0x8E: case 0x90: case 0x9C: case 0x9B: return 3;
    case 0x8F: return 4;
    case 0x83: return 10;
    case 0xF3: return 22;
    case 0x9D:
        if (buf.size() < 2) return 0;
        return (quint8(buf[1]) == 0x03 || quint8(buf[1]) == 0x04) ? 9 : 8;
    case 0xF4: {
        if (buf.size() < 2) return 0;
        const int n = quint8(buf[1]) & 0x7F;
        return n + (n + 9) / 7 + 6;
    }
    default: return 1;
    }
}

void FakeContecDevice::write(const QByteArray &data)
{
    m_in += data;
    while (!m_in.isEmpty()) {
        const int n = commandLength(m_in);
        if (n == 0 || n > m_in.size()) return;
        const QByteArray cmd = m_in.left(n);
        m_in.remove(0, n);
        if (n == 1) continue;
        const int h = quint8(cmd[0]);
        if (h == 0xF3 && cmd.size() == 22) {
            keyExchange(cmd);
        } else if (h == 0xF4 && m_encrypted) {
            QByteArray as84 = cmd;
            as84[0] = char(0x84);
            sawEncryptedCommand = true;
            handle(open84(as84, m_decrypt));
        } else {
            handle(cmd);
        }
    }
}

void FakeContecDevice::keyExchange(const QByteArray &f3)
{
    const QByteArray seedApp = seedFrom83(f3);
    const QByteArray seedDev = QByteArray::fromHex("0102030405060708090a0b0c0d0e0f10");
    deriveRx(seedApp, deviceId, m_encrypt.keyTx, m_encrypt.ivTx);      // the app decrypts 84 with these
    deriveTx(seedDev, deviceId, m_decrypt.keyRx, m_decrypt.ivRx);      // the app encrypts F4 with these
    QByteArray f83 = buildF3(seedDev);
    f83[0] = char(0x83);
    f83[21] = char(checksum(f83.left(21)));
    reply(f83);            // still in clear
    m_encrypted = true;
}

void FakeContecDevice::reply(const QByteArray &frame)
{
    QByteArray out = frame;
    if (m_encrypted) {
        out = buildF4(frame, m_encrypt);
        out[0] = char(0x84);
        out[out.size() - 1] = char(checksum(out.left(out.size() - 1)));
    }
    for (int i = 0; i < out.size(); i += 20) {
        const QByteArray part = out.mid(i, 20);
        QMetaObject::invokeMethod(this, [this, part]() { emit received(part); }, Qt::QueuedConnection);
    }
}

void FakeContecDevice::handle(const QByteArray &cmd)
{
    commands.append(cmd);
    const int h = quint8(cmd[0]);
    if (silentCommands.contains(h)) return;
    if (silentOnceCommand == h) { silentOnceCommand = -1; return; }
    if (rejectCommands.contains(h)) { reply(QByteArray::fromHex("f070")); return; }

    switch (h) {
    case 0x81: {
        QByteArray f1;
        f1.append(char(0xF1));
        f1 += deviceId.left(8);
        reply(f1 + char(checksum(f1)));
        break;
    }
    case 0x82: {
        QByteArray f2;                                   // no strings: exactly 9 bytes
        for (int v : { 0xF2, 0x00, 0x00, 0x02, 0x06, 0x00, m_version, 0x07, 0x00 }) f2.append(char(v));
        reply(f2);
        break;
    }
    case 0x8F: reply(frame({0xFF, 0x00})); break;
    case 0x9F: {
        const bool any = !records.isEmpty();
        reply(frame({0xEF, 0x00, any ? 1 : 0, any ? 0x40 : 0, 0x00, 0x00, 0x00}));
        break;
    }
    case 0x90: {
        const int c = records.size();
        reply(frame({0xE0, 0x06, c & 0x7F, (c >> 7) & 0x7F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
        break;
    }
    case 0x8E: reply(frame({0xFE, 0x06, FmtOriginal, 0, 0, 0})); break;
    case 0x9C: {
        const int i = m_nextHeader++;
        if (i >= records.size()) {
            reply(frame({0xEC, 0x40, 1, i + 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
            break;
        }
        const Rec &r = records[i];
        const int n = r.spo2.size();
        const QDate d = r.start.date();
        const QTime t = r.start.time();
        reply(frame({0xEC, i == records.size() - 1 ? 0x40 : 0x00, 1, i + 1,
                     d.year() - 2000, d.month(), d.day(), t.hour(), t.minute(), t.second(),
                     n & 0x7F, (n >> 7) & 0x7F, (n >> 14) & 0x7F, (n >> 21) & 0x7F,
                     'u', 's', 'e', 'r', 0, 0}));
        break;
    }
    case 0x9D: {
        const int sub = quint8(cmd[1]);
        if (sub == 0x7F) {
            erased = true;
            records.clear();
            reply(frame({0xED, 0x7F, 0x7F, 0x7F, 0x7F, 0x00}));
        } else if (sub == 0x03) {
            sendPackets(quint8(cmd[2]), quint8(cmd[4]), int(lo7(cmd, 5, 3)));
        }                                                // 0x02 = abort: stop quietly
        break;
    }
    case 0x83:
        clockSetTo = QDateTime(QDate(2000 + quint8(cmd[1]), quint8(cmd[2]), quint8(cmd[3])),
                               QTime(quint8(cmd[4]), quint8(cmd[5]), quint8(cmd[6])));
        reply(frame({0xF3, 0x00}));
        break;
    default:
        break;
    }
}

void FakeContecDevice::sendPackets(int channel, int m, int offset)
{
    if (m < 1 || m > records.size()) return;
    const QVector<int> &values = (channel == ChSpO2) ? records[m - 1].spo2 : records[m - 1].pulse;
    const int packets = (values.size() + 20) / 21;
    for (int pkt = offset; pkt < packets; ++pkt) {
        QByteArray p(30, '\0');
        p[0] = char(0xED);
        p[1] = char(0x03);
        p[2] = char(channel);
        p[3] = char(pkt & 0x7F);
        p[4] = char((pkt >> 7) & 0x7F);
        for (int j = 0; j < 21; ++j) {
            const int idx = pkt * 21 + j;
            const int v = idx < values.size() ? values[idx] : 0;
            p[8 + j] = char(v & 0x7F);
            if (v & 0x80) p[5 + j / 7] = char(quint8(p[5 + j / 7]) | (1 << (j % 7)));
        }
        p[29] = char(checksum(p.left(29)));
        if (channel == corruptChannel && pkt == corruptPacket) {
            corruptPacket = -1;
            p[29] = char((quint8(p[29]) + 1) & 0x7F);
        }
        reply(p);
    }
}
```

- [ ] **Step 4: Тесты `oscar/tests/contecbledownloadertests.h`**

```cpp
/* Contec BLE Download Session Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class QCoreApplication;

class ContecBleDownloaderTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testDownloadsAllRecords();
    void testSkipsUnwantedRecord();
    void testRetriesCorruptedPacket();
    void testTimeoutFails();
    void testRejectedCommandFails();
    void testUnsupportedModels();
    void testEraseNeedsPermission();
    void testEraseWithPermission();
    void testSetClock();
    void testEncryptedSession();
    void testStorageFallsBackToPrepare();
    void testNoRecords();
    void testLinkLostFails();
    void cleanupTestCase();
private:
    QCoreApplication *m_app = nullptr;
};
DECLARE_TEST(ContecBleDownloaderTests)
```

- [ ] **Step 5: Тесты `oscar/tests/contecbledownloadertests.cpp`**

```cpp
/* Contec BLE Download Session Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contecbledownloadertests.h"
#include "fakecontecdevice.h"
#include "SleepLib/loader_plugins/contec_ble_downloader.h"

#include <QCoreApplication>

using namespace ContecBle;

namespace {

FakeContecDevice::Rec makeRec(const QDateTime &start, int n, int spo2Base, int pulseBase)
{
    FakeContecDevice::Rec r;
    r.start = start;
    for (int i = 0; i < n; ++i) {
        r.spo2.append(spo2Base + i % 4);
        r.pulse.append(pulseBase + i % 7);
    }
    return r;
}

// Two nights' worth of segments; pulse values cross 127 so the high bits are exercised, and
// one sample of each channel carries the device's "no data" marker.
void addTwoRecords(FakeContecDevice &dev)
{
    FakeContecDevice::Rec a = makeRec(QDateTime(QDate(2026, 9, 26), QTime(23, 59, 13)), 50, 93, 125);
    a.spo2[3] = 127;
    a.pulse[3] = 255;
    dev.records = { a, makeRec(QDateTime(QDate(2026, 9, 27), QTime(5, 41, 54)), 30, 95, 60) };
}

struct Collected {
    QList<Record> records;
    bool finished = false;
    QString error;
    int count = -1;
    QString model, firmware;
    int version = -1;
    int clock = -1;
    int erase = -1;
};

void collect(ContecBleDownloader &d, Collected &c)
{
    QObject::connect(&d, &ContecBleDownloader::recordDownloaded, [&c](const Record &r) { c.records.append(r); });
    QObject::connect(&d, &ContecBleDownloader::downloadFinished, [&c]() { c.finished = true; });
    QObject::connect(&d, &ContecBleDownloader::failed, [&c](const QString &e) { c.error = e; });
    QObject::connect(&d, &ContecBleDownloader::recordCountKnown, [&c](int n) { c.count = n; });
    QObject::connect(&d, &ContecBleDownloader::deviceIdentified, [&c](const QString &m, const QString &f, int v) {
        c.model = m; c.firmware = f; c.version = v;
    });
    QObject::connect(&d, &ContecBleDownloader::clockSet, [&c](bool ok) { c.clock = ok ? 1 : 0; });
    QObject::connect(&d, &ContecBleDownloader::eraseFinished, [&c](bool ok) { c.erase = ok ? 1 : 0; });
}

bool hasCommand(const FakeContecDevice &dev, const QByteArray &prefix)
{
    for (const QByteArray &c : dev.commands) {
        if (c.startsWith(prefix)) return true;
    }
    return false;
}

} // namespace

void ContecBleDownloaderTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QCoreApplication(argc, argv);
    }
}

void ContecBleDownloaderTests::testDownloadsAllRecords()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished || !c.error.isEmpty(), 5000);
    QVERIFY2(c.error.isEmpty(), qPrintable(c.error));
    QCOMPARE(c.model, QStringLiteral("CMS50FW"));
    QCOMPARE(c.firmware, QStringLiteral("2.0.0"));
    QCOMPARE(c.version, 13);
    QCOMPARE(c.count, 2);
    QCOMPARE(c.records.size(), 2);
    QCOMPARE(c.records[0].header.start(), dev.records[0].start);
    QCOMPARE(c.records[0].spo2, dev.records[0].spo2);
    QCOMPARE(c.records[0].pulse, dev.records[0].pulse);
    QCOMPARE(c.records[1].header.start(), dev.records[1].start);
    QCOMPARE(c.records[1].pulse, dev.records[1].pulse);
    QVERIFY(!d.isEncrypted());
    for (const QByteArray &cmd : dev.commands) QVERIFY(!isDestructive(cmd));
}

void ContecBleDownloaderTests::testSkipsUnwantedRecord()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    ContecBleDownloader d;
    d.setWantRecord([](const RecordHeader &h) { return h.m != 1; });
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished || !c.error.isEmpty(), 5000);
    QVERIFY(c.error.isEmpty());
    QCOMPARE(c.records.size(), 1);
    QCOMPARE(c.records[0].header.m, 2);
    QVERIFY(!hasCommand(dev, frame({0x9D, 0x03, 0x01, 0x01, 0x01, 0, 0, 0}).left(5)));
}

void ContecBleDownloaderTests::testRetriesCorruptedPacket()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    dev.corruptChannel = ChSpO2;
    dev.corruptPacket = 1;
    ContecBleDownloader d;
    d.setRetryPause(20);
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished || !c.error.isEmpty(), 5000);
    QVERIFY2(c.error.isEmpty(), qPrintable(c.error));
    QCOMPARE(c.records[0].spo2, dev.records[0].spo2);
    QVERIFY(hasCommand(dev, cmdChannelAbort(ChSpO2, 1, 1)));
    QVERIFY(hasCommand(dev, cmdChannel(FmtOriginal, ChSpO2, 1, 1, 1)));
}

void ContecBleDownloaderTests::testTimeoutFails()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    dev.silentCommands.insert(0x9C);
    ContecBleDownloader d;
    d.setResponseTimeout(100);
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(!c.error.isEmpty(), 3000);
    QVERIFY(!c.finished);
}

void ContecBleDownloaderTests::testRejectedCommandFails()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    dev.rejectCommands.insert(0x90);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(!c.error.isEmpty(), 3000);
    QVERIFY(c.error.contains(QStringLiteral("rejected")));
}

void ContecBleDownloaderTests::testUnsupportedModels()
{
    FakeContecDevice dev;
    ContecBleDownloader k;
    Collected ck;
    collect(k, ck);
    k.start(&dev, QStringLiteral("SpO209"));
    QVERIFY(!ck.error.isEmpty());
    ContecBleDownloader unknown;
    Collected cu;
    collect(unknown, cu);
    unknown.start(&dev, QStringLiteral("Headphones"));
    QVERIFY(!cu.error.isEmpty());
    QVERIFY(dev.commands.isEmpty());
}

void ContecBleDownloaderTests::testEraseNeedsPermission()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished, 5000);
    d.eraseAllRecords();
    QVERIFY(!c.error.isEmpty());
    QTest::qWait(50);
    QVERIFY(!dev.erased);
    QVERIFY(!hasCommand(dev, cmdEraseAllRecords()));
}

void ContecBleDownloaderTests::testEraseWithPermission()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished, 5000);
    d.allowDestructive(true);
    d.eraseAllRecords();
    QTRY_COMPARE_WITH_TIMEOUT(c.erase, 1, 3000);
    QVERIFY(dev.erased);
    QVERIFY(c.error.isEmpty());
}

void ContecBleDownloaderTests::testSetClock()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished, 5000);
    const QDateTime now(QDate(2026, 9, 28), QTime(7, 30, 15));
    d.setClock(now);
    QTRY_COMPARE_WITH_TIMEOUT(c.clock, 1, 3000);
    QCOMPARE(dev.clockSetTo, now);
}

void ContecBleDownloaderTests::testEncryptedSession()
{
    FakeContecDevice dev(14);
    addTwoRecords(dev);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO211"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished || !c.error.isEmpty(), 5000);
    QVERIFY2(c.error.isEmpty(), qPrintable(c.error));
    QVERIFY(d.isEncrypted());
    QVERIFY(dev.sawEncryptedCommand);
    QCOMPARE(c.version, 14);
    QCOMPARE(c.records.size(), 2);
    QCOMPARE(c.records[0].spo2, dev.records[0].spo2);
    QCOMPARE(c.records[1].pulse, dev.records[1].pulse);
}

void ContecBleDownloaderTests::testStorageFallsBackToPrepare()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    dev.silentOnceCommand = 0x9F;
    ContecBleDownloader d;
    d.setResponseTimeout(100);
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished || !c.error.isEmpty(), 5000);
    QVERIFY2(c.error.isEmpty(), qPrintable(c.error));
    QVERIFY(hasCommand(dev, cmdPrepare()));
    QCOMPARE(c.records.size(), 2);
}

void ContecBleDownloaderTests::testNoRecords()
{
    FakeContecDevice dev;
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_VERIFY_WITH_TIMEOUT(c.finished || !c.error.isEmpty(), 5000);
    QVERIFY(c.error.isEmpty());
    QCOMPARE(c.count, 0);
    QVERIFY(c.records.isEmpty());
    QVERIFY(!hasCommand(dev, cmdCountRecords()));
}

void ContecBleDownloaderTests::testLinkLostFails()
{
    FakeContecDevice dev;
    addTwoRecords(dev);
    dev.silentCommands.insert(0x9C);
    ContecBleDownloader d;
    Collected c;
    collect(d, c);
    d.start(&dev, QStringLiteral("SpO202"));
    QTRY_COMPARE_WITH_TIMEOUT(c.count, 2, 3000);
    dev.dropLink();
    QVERIFY(!c.error.isEmpty());
    QVERIFY(!c.finished);
}

void ContecBleDownloaderTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}
```

- [ ] **Step 6: Подключить в `oscar.pro`**

Основные `SOURCES`/`HEADERS`: после `contec_ble_protocol.*` добавить `SleepLib/loader_plugins/contec_ble_downloader.cpp \` / `.h \`. В `test {}`: к `SOURCES` добавить `tests/fakecontecdevice.cpp` и `tests/contecbledownloadertests.cpp`, к `HEADERS` — `tests/fakecontecdevice.h` и `tests/contecbledownloadertests.h` (с `\` у предыдущих строк).

- [ ] **Step 7: Убедиться, что тесты не собираются**

Создать заглушку `contec_ble_downloader.cpp` (`#include "contec_ble_downloader.h"`), перезапустить qmake в `build-test`, собрать:
Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E 'Undefined symbols|error:' | head -3`
Expected: `Undefined symbols … ContecBleDownloader::…`.

- [ ] **Step 8: Реализация `contec_ble_downloader.cpp`** (заменить заглушку)

```cpp
/* Contec BLE Oximeter Download Session
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contec_ble_downloader.h"

#include <QDebug>

using namespace ContecBle;

namespace {
const int kMaxAttempts = 5;
const int kEraseTimeoutMs = 20000;
const int kMaxHeaders = 1000;   // safety net if a device never flags its last record
}

ContecBleDownloader::ContecBleDownloader(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &ContecBleDownloader::onTimeout);
}

void ContecBleDownloader::start(ContecBleLink *link, const QString &advertisedName)
{
    m_link = link;
    m_model = modelForName(advertisedName);
    if (!m_model.isValid()) {
        fail(tr("%1 is not a known Contec oximeter.").arg(advertisedName));
        return;
    }
    if (m_model.variant != 'A') {
        fail(tr("The %1 is not supported yet.").arg(m_model.model));
        return;
    }
    connect(link, &ContecBleLink::received, this, &ContecBleDownloader::onReceived);
    connect(link, &ContecBleLink::linkLost, this, &ContecBleDownloader::onLinkLost);
    send(cmdId());
    expect(State::WaitId);
}

void ContecBleDownloader::cancel()
{
    ++m_generation;
    m_timer.stop();
    m_state = State::Cancelled;
}

void ContecBleDownloader::setClock(const QDateTime &localNow)
{
    if (m_state != State::Ready) return;
    send(cmdSetTime(localNow));
    expect(State::WaitSetTime);
}

void ContecBleDownloader::eraseAllRecords()
{
    if (m_state != State::Ready) return;
    if (!m_allowDestructive) {
        fail(tr("Refusing to erase the oximeter without permission."));
        return;
    }
    send(cmdEraseAllRecords());
    expect(State::WaitErase, kEraseTimeoutMs);
}

void ContecBleDownloader::send(const QByteArray &cmd)
{
    if (isDestructive(cmd) && !m_allowDestructive) {
        fail(tr("Refusing to erase the oximeter without permission."));
        return;
    }
    qDebug() << "ContecBLE tx" << cmd.toHex(' ');     // commands only, never sample data
    m_link->write(m_encrypted ? buildF4(cmd, m_keys) : cmd);
}

void ContecBleDownloader::expect(State next, int timeoutMs)
{
    m_state = next;
    m_timer.start(timeoutMs < 0 ? m_responseTimeoutMs : timeoutMs);
}

void ContecBleDownloader::fail(const QString &message)
{
    if (stopped()) return;
    ++m_generation;
    m_timer.stop();
    m_state = State::Failed;
    qWarning() << "ContecBLE:" << message;
    emit failed(message);
}

void ContecBleDownloader::onLinkLost()
{
    if (stopped() || m_state == State::Ready || m_state == State::Idle) return;
    fail(tr("The connection to the oximeter was lost."));
}

void ContecBleDownloader::onReceived(const QByteArray &data)
{
    if (stopped()) return;
    for (const QByteArray &f : m_outer.feed(data)) {
        if (stopped()) return;
        if (quint8(f[0]) == 0x84 && m_encrypted) {
            for (const QByteArray &g : m_inner.feed(open84(f, m_keys))) {
                if (stopped()) return;
                onFrame(g);
            }
        } else {
            onFrame(f);
        }
    }
}

void ContecBleDownloader::onFrame(const QByteArray &f)
{
    const int h = quint8(f[0]);
    if (h == 0xF0) {
        fail(tr("The oximeter rejected a command (%1).").arg(QString::fromLatin1(f.toHex(' '))));
        return;
    }
    switch (m_state) {
    case State::WaitId:
        if (h != 0xF1) return;
        m_deviceId = parseF1(f);
        send(cmdInfo());
        expect(State::WaitInfo);
        return;
    case State::WaitInfo: {
        if (h != 0xF2) return;
        const DeviceInfo info = parseF2(f);
        m_version = info.protocolVersion;
        emit deviceIdentified(m_model.model, info.firmware, m_version);
        if (stopped()) return;
        if (m_version > 13) startKeyExchange();
        else requestStorage();
        return;
    }
    case State::WaitSeed:
        if (h != 0x83) return;
        deriveTx(seedFrom83(f), m_deviceId.toLatin1(), m_keys.keyTx, m_keys.ivTx);
        m_encrypted = true;
        requestStorage();
        return;
    case State::WaitStorage:
    case State::WaitStorageRetry: {
        if (h != 0xEF) return;
        const StorageStatus st = parseEF(f);
        if (!st.hasData || !st.hasRecords()) {
            m_total = 0;
            emit recordCountKnown(0);
            if (!stopped()) finishDownload();
            return;
        }
        send(cmdCountRecords());
        expect(State::WaitCount);
        return;
    }
    case State::WaitPrepare:
        if (h != 0xFF) return;
        send(cmdStorage());
        expect(State::WaitStorageRetry);
        return;
    case State::WaitCount:
        if (h != 0xE0) return;
        m_total = parseE0Count(f);
        emit recordCountKnown(m_total);
        if (stopped()) return;
        if (m_total == 0) { finishDownload(); return; }
        send(cmdFormats());
        expect(State::WaitFormats);
        return;
    case State::WaitFormats:
        if (h != 0xFE || f.size() < 3 || quint8(f[1]) != 0x06) return;
        m_format = pickFormat(parseFE06Formats(f));
        requestHeader();
        return;
    case State::WaitHeader:
        if (h == 0xEC) onHeader(f);
        return;
    case State::ReadChannel:
        if (h == 0xED) onChannelPacket(f);
        return;
    case State::WaitSetTime:
        if (h != 0xF3) return;
        m_timer.stop();
        m_state = State::Ready;
        emit clockSet(true);
        return;
    case State::WaitErase:
        if (h != 0xED || f.size() < 6 || quint8(f[1]) != 0x7F) return;
        m_timer.stop();
        m_state = State::Ready;
        emit eraseFinished((quint8(f[5]) & 0x7F) == 0);
        return;
    default:
        return;   // includes RetryPause: frames of the aborted stream are dropped
    }
}

void ContecBleDownloader::onTimeout()
{
    switch (m_state) {
    case State::WaitStorage:        // the vendor app always prepares first; do the same on silence
        send(cmdPrepare());
        expect(State::WaitPrepare);
        return;
    case State::ReadChannel:
        retryChannel();
        return;
    case State::WaitSetTime:
        m_state = State::Ready;
        emit clockSet(false);
        return;
    case State::WaitErase:
        m_state = State::Ready;
        emit eraseFinished(false);
        return;
    case State::Idle: case State::Ready: case State::RetryPause: case State::Failed: case State::Cancelled:
        return;
    default:
        fail(tr("The oximeter didn't answer in time."));
        return;
    }
}

void ContecBleDownloader::startKeyExchange()
{
    const QByteArray seedApp = appSeed(QDateTime::currentDateTime());
    deriveRx(seedApp, m_deviceId.toLatin1(), m_keys.keyRx, m_keys.ivRx);
    const QByteArray f3 = buildF3(seedApp);
    m_link->write(f3.left(18));                 // the vendor app sends 18 bytes, pauses, then 4
    const int generation = m_generation;
    QTimer::singleShot(300, this, [this, f3, generation]() {
        if (generation == m_generation && m_state == State::WaitSeed) m_link->write(f3.mid(18));
    });
    expect(State::WaitSeed);
}

void ContecBleDownloader::requestStorage()
{
    send(cmdStorage());
    expect(State::WaitStorage);
}

void ContecBleDownloader::requestHeader()
{
    if (m_done >= kMaxHeaders) { finishDownload(); return; }
    send(cmdNextHeader());
    expect(State::WaitHeader);
}

void ContecBleDownloader::onHeader(const QByteArray &f)
{
    m_timer.stop();
    const RecordHeader hdr = parseEC(f);
    if (hdr.samples == 0) { finishDownload(); return; }
    const bool want = m_wantRecord ? m_wantRecord(hdr) : true;
    if (m_state != State::WaitHeader) return;   // the callback cancelled us
    m_record = Record();
    m_record.header = hdr;
    if (!want) {
        ++m_done;
        emit progress(m_done, m_total);
        if (stopped()) return;
        if (hdr.last) finishDownload();
        else requestHeader();
        return;
    }
    m_channels = { ChSpO2, ChPulse };
    if (hdr.hasPI) m_channels.append(ChPI);
    m_channelIndex = 0;
    startChannel();
}

void ContecBleDownloader::startChannel()
{
    m_packet = 0;
    m_attempt = 0;
    m_samples.clear();
    m_decoder = CodeDecoder();
    send(cmdChannel(m_format, m_channels.at(m_channelIndex), m_record.header.l, m_record.header.m, 0));
    expect(State::ReadChannel);
}

void ContecBleDownloader::onChannelPacket(const QByteArray &f)
{
    if (f.size() < 2 || quint8(f[1]) != formatCode(m_format)) return;
    const int ch = m_channels.at(m_channelIndex);
    if (!checksumOk(f) || quint8(f[2]) != ch || edPacketNo(f) != m_packet) {
        retryChannel();
        return;
    }
    m_attempt = 0;
    if (m_format == FmtDifference) m_samples += parseEdDifference(f, m_version);
    else if (m_format == FmtOriginal) m_samples += parseEdOriginal(f);
    else m_samples += m_decoder.feed(f);
    ++m_packet;
    if (m_samples.size() < m_record.header.samples) {
        m_timer.start(m_responseTimeoutMs);
        return;
    }
    m_samples.resize(m_record.header.samples);
    if (ch == ChSpO2) m_record.spo2 = m_samples;
    else if (ch == ChPulse) m_record.pulse = m_samples;
    else m_record.pi = m_samples;
    if (++m_channelIndex < m_channels.size()) {
        startChannel();
        return;
    }
    finishRecord();
}

void ContecBleDownloader::retryChannel()
{
    const int ch = m_channels.at(m_channelIndex);
    if (++m_attempt > kMaxAttempts) {
        fail(tr("Downloading stopped: packet %1 of channel %2 kept failing.").arg(m_packet).arg(ch));
        return;
    }
    qDebug() << "ContecBLE retry: channel" << ch << "packet" << m_packet << "attempt" << m_attempt;
    send(cmdChannelAbort(ch, m_record.header.l, m_record.header.m));
    if (stopped()) return;
    m_timer.stop();
    m_state = State::RetryPause;
    const int generation = m_generation;
    QTimer::singleShot(m_retryPauseMs, this, [this, ch, generation]() {
        if (generation != m_generation || m_state != State::RetryPause) return;
        m_outer.clear();
        m_inner.clear();
        if (m_format == FmtCode) {             // CODE state cannot resume mid-stream
            m_samples.clear();
            m_decoder = CodeDecoder();
            m_packet = 0;
        }
        send(cmdChannel(m_format, ch, m_record.header.l, m_record.header.m, m_packet));
        expect(State::ReadChannel);
    });
}

void ContecBleDownloader::finishRecord()
{
    m_timer.stop();
    ++m_done;
    const Record done = m_record;
    emit recordDownloaded(done);
    if (stopped()) return;
    emit progress(m_done, m_total);
    if (stopped()) return;
    if (done.header.last) finishDownload();
    else requestHeader();
}

void ContecBleDownloader::finishDownload()
{
    m_timer.stop();
    m_state = State::Ready;
    emit downloadFinished();
}
```

- [ ] **Step 9: Прогнать тесты**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; /Users/semyk/Downloads/Oscar_Project/tools/runtests.sh ContecBleDownloaderTests ContecBleProtocolTests`
Expected: `ContecBleDownloaderTests: 15 PASS`, `0 unexpected`.

- [ ] **Step 10: Приложение и предупреждения**

Run: перезапустить qmake в `build`, затем `make … | grep -E ' error:'; …/warncheck.sh contec_ble_downloader | comm -13 …/warn-baseline-ble.txt -`
Expected: пусто.

- [ ] **Step 11: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/SleepLib/loader_plugins/contec_ble_downloader.h oscar/SleepLib/loader_plugins/contec_ble_downloader.cpp oscar/tests/fakecontecdevice.h oscar/tests/fakecontecdevice.cpp oscar/tests/contecbledownloadertests.h oscar/tests/contecbledownloadertests.cpp oscar/oscar.pro
git commit -m "Add the Contec BLE download session

Asynchronous state machine over an abstract byte link: handshake, secure
mode, storage status, record headers and channel download with the vendor
app's retry scheme, plus clock setting and an erase that is refused unless
explicitly allowed. Tested against a simulated oximeter.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Общая функция построения оксиметрической сессии

**Files:**
- Create: `oscar/SleepLib/oximetry_session_builder.h`, `oscar/SleepLib/oximetry_session_builder.cpp`
- Create: `oscar/tests/contecbleimporttests.h`, `oscar/tests/contecbleimporttests.cpp`
- Modify: `oscar/oximeterimport.cpp:888-1085` (`on_saveButton_clicked`)
- Modify: `oscar/oscar.pro`

**Interfaces:**
- Produces: `qint64 addOximetryEvents(Session *session, qint64 startMs, const QVector<OxiRecord> &records, qint64 stepMs, bool havePerfIndex)` (возвращает время последней записи); `void finishOximetrySession(Session *session, qint64 lastMs, bool havePerfIndex)` (десатурации/изменения пульса, итоги, `really_set_last`, `SetChanged(true)`, `setOpened(true)`; нужен `p_profile`).

- [ ] **Step 1: Заголовок `oximetry_session_builder.h`**

```cpp
/* Oximetry Session Builder Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OXIMETRY_SESSION_BUILDER_H
#define OXIMETRY_SESSION_BUILDER_H

#include <QVector>
#include "SleepLib/serialoximeter.h"

class Session;

//! Adds pulse, SpO2 and (optionally) perfusion event lists for records spaced stepMs apart,
//! starting at startMs. A zero value is a gap. Returns the time of the last record.
qint64 addOximetryEvents(Session *session, qint64 startMs, const QVector<OxiRecord> &records,
                         qint64 stepMs, bool havePerfIndex);

//! Flags drops and pulse changes, computes the summary values and marks the session changed.
//! Uses the current profile's oximetry settings.
void finishOximetrySession(Session *session, qint64 lastMs, bool havePerfIndex);

#endif // OXIMETRY_SESSION_BUILDER_H
```

- [ ] **Step 2: Тест `contecbleimporttests.h`** (класс расширяется в Task 5)

```cpp
/* Contec BLE Import Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class ContecBleImportTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testOximetryEventsSplitAtGaps();
};
DECLARE_TEST(ContecBleImportTests)
```

- [ ] **Step 3: Тест `contecbleimporttests.cpp`**

```cpp
/* Contec BLE Import Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contecbleimporttests.h"
#include "SleepLib/machine.h"
#include "SleepLib/oximetry_session_builder.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"

void ContecBleImportTests::initTestCase()
{
    if (CPAP_Obstructive == 0) { schema::init(); }
}

// A run of zeros ends the current event list; the next valid value starts a new one.
void ContecBleImportTests::testOximetryEventsSplitAtGaps()
{
    Machine mach(nullptr, 7);
    Session sess(&mach, 1000);
    const qint64 start = 1000000;
    const QVector<OxiRecord> recs = { OxiRecord(60, 95), OxiRecord(61, 96), OxiRecord(0, 0),
                                      OxiRecord(0, 0), OxiRecord(62, 97) };
    const qint64 last = addOximetryEvents(&sess, start, recs, 1000, false);
    QCOMPARE(last, start + 4000);
    const QVector<EventList *> &pulse = sess.eventlist[OXI_Pulse];
    QCOMPARE(pulse.size(), 2);
    QCOMPARE(pulse[0]->first(), start);
    QCOMPARE(pulse[0]->last(), start + 2000);
    QCOMPARE(pulse[1]->first(), start + 4000);
    QCOMPARE(sess.eventlist[OXI_SPO2].size(), 2);
    QVERIFY(!sess.eventlist.contains(OXI_Perf));
}
```

- [ ] **Step 4: Подключить в `oscar.pro`**

Основные `SOURCES`/`HEADERS`: после строки `    SleepLib/serialoximeter.cpp \` (`.h \`) добавить `    SleepLib/oximetry_session_builder.cpp \` (`.h \`). В `test {}` добавить `tests/contecbleimporttests.cpp` / `.h`.

- [ ] **Step 5: Убедиться, что тест не собирается**

Заглушка `oximetry_session_builder.cpp` с `#include "oximetry_session_builder.h"`; qmake в `build-test`; сборка.
Expected: `Undefined symbols … addOximetryEvents`.

- [ ] **Step 6: Реализация `oximetry_session_builder.cpp`** — перенос кода из `on_saveButton_clicked` без изменения логики

```cpp
/* Oximetry Session Builder
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "oximetry_session_builder.h"
#include "SleepLib/calcs.h"
#include "SleepLib/session.h"

qint64 addOximetryEvents(Session *session, qint64 startMs, const QVector<OxiRecord> &records,
                         qint64 stepMs, bool havePerfIndex)
{
    EventList *ELpulse = nullptr;
    EventList *ELspo2 = nullptr;
    EventList *ELperf = nullptr;
    quint16 lastpulse = 0, lastspo2 = 0, lastperf = 0;
    quint16 lastgoodpulse = 0, lastgoodspo2 = 0, lastgoodperf = 0;
    qint64 ti = startMs;

    for (const OxiRecord &rec : records) {
        if (rec.pulse > 0) {
            if (lastpulse == 0) ELpulse = session->AddEventList(OXI_Pulse, EVL_Event);
            if (lastpulse != rec.pulse) {
                if (lastpulse > 0) ELpulse->AddEvent(ti, lastpulse);
                ELpulse->AddEvent(ti, rec.pulse);
            }
            lastgoodpulse = rec.pulse;
        } else if (lastgoodpulse > 0) {          // end section properly
            ELpulse->AddEvent(ti, lastpulse);
            session->setLast(OXI_Pulse, ti);
            lastgoodpulse = 0;
        }
        lastpulse = rec.pulse;

        if (rec.spo2 > 0) {
            if (lastspo2 == 0) ELspo2 = session->AddEventList(OXI_SPO2, EVL_Event);
            if (lastspo2 != rec.spo2) {
                if (lastspo2 > 0) ELspo2->AddEvent(ti, lastspo2);
                ELspo2->AddEvent(ti, rec.spo2);
            }
            lastgoodspo2 = rec.spo2;
        } else if (lastgoodspo2 > 0) {
            ELspo2->AddEvent(ti, lastspo2);
            session->setLast(OXI_SPO2, ti);
            lastgoodspo2 = 0;
        }
        lastspo2 = rec.spo2;

        if (havePerfIndex) {                      // Perfusion Index
            if (rec.perf > 0) {
                if (lastperf == 0) ELperf = session->AddEventList(OXI_Perf, EVL_Event, 0.01f);
                if (lastperf != rec.perf) {
                    if (lastperf > 0) ELperf->AddEvent(ti, lastperf);
                    ELperf->AddEvent(ti, rec.perf);
                }
                lastgoodperf = rec.perf;
            } else if (lastgoodperf > 0) {
                ELperf->AddEvent(ti, lastperf);
                session->setLast(OXI_Perf, ti);
                lastgoodperf = 0;
            }
            lastperf = rec.perf;
        }
        ti += stepMs;
    }
    ti -= stepMs;
    if (ELpulse && lastpulse > 0) {
        ELpulse->AddEvent(ti, lastpulse);
        session->setLast(OXI_Pulse, ti);
    }
    if (ELspo2 && lastspo2 > 0) {
        ELspo2->AddEvent(ti, lastspo2);
        session->setLast(OXI_SPO2, ti);
    }
    if (havePerfIndex && ELperf && lastperf > 0) {
        ELperf->AddEvent(ti, lastperf);
        session->setLast(OXI_Perf, ti);
    }
    return ti;
}

void finishOximetrySession(Session *session, qint64 lastMs, bool havePerfIndex)
{
    if (havePerfIndex) {
        session->first(OXI_Perf);
        session->last(OXI_Perf);
        session->count(OXI_Perf);
        session->Min(OXI_Perf);
        session->Max(OXI_Perf);
    }

    calcSPO2Drop(session);
    calcPulseChange(session);

    session->first(OXI_Pulse);
    session->first(OXI_SPO2);
    session->last(OXI_Pulse);
    session->last(OXI_SPO2);

    session->first(OXI_PulseChange);
    session->first(OXI_SPO2Drop);
    session->last(OXI_PulseChange);
    session->last(OXI_SPO2Drop);

    session->cph(OXI_PulseChange);
    session->sph(OXI_PulseChange);
    session->cph(OXI_SPO2Drop);
    session->sph(OXI_SPO2Drop);

    session->count(OXI_Pulse);
    session->count(OXI_SPO2);
    session->count(OXI_PulseChange);
    session->count(OXI_SPO2Drop);
    session->Min(OXI_Pulse);
    session->Min(OXI_SPO2);
    session->Max(OXI_Pulse);
    session->Max(OXI_SPO2);
    // avg/wavg are needed so daily_summaries.spo2_avg and pulse_avg are correct
    session->avg(OXI_Pulse);
    session->avg(OXI_SPO2);
    session->wavg(OXI_Pulse);
    session->wavg(OXI_SPO2);

    session->really_set_last(lastMs);
    session->SetChanged(true);
    session->setOpened(true);
}
```

- [ ] **Step 7: `on_saveButton_clicked` переходит на общие функции**

В `oscar/oximeterimport.cpp` добавить `#include "SleepLib/oximetry_session_builder.h"` к include'ам. В `on_saveButton_clicked` заменить весь блок от строки `    EventList * ELpulse = nullptr;` до строки `    session->setOpened(true);` включительно на:

```cpp
    bool haveperf = oximodule->havePerfIndex();
    qint64 step = (importMode == IM_LIVE) ? oximodule->liveResolution() : oximodule->importResolution();

    qDebug() << "oximod = Creating event list for pulse and O2 saturation";
    const qint64 ti = addOximetryEvents(session, qint64(start), *oxirec, step, haveperf);

    qDebug() << "oximod - Setting up device and session";
    mach->setModel(oximodule->getModel());
    mach->setBrand(oximodule->getVendor());

    finishOximetrySession(session, ti, haveperf);
```
(Проверить `git diff`: пропали только перенесённые строки; `mach->AddSession(session)` и всё после него — без изменений.)

- [ ] **Step 8: Прогнать тесты**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; /Users/semyk/Downloads/Oscar_Project/tools/runtests.sh ContecBleImportTests`
Expected: `ContecBleImportTests: 3 PASS` (init + тест + cleanup), `0 unexpected`.

- [ ] **Step 9: Приложение и предупреждения**

Run: qmake в `build`, `make`, затем `…/warncheck.sh oximetry_session_builder oximeterimport | comm -13 …/warn-baseline-ble.txt -`
Expected: пусто.

- [ ] **Step 10: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/SleepLib/oximetry_session_builder.h oscar/SleepLib/oximetry_session_builder.cpp oscar/tests/contecbleimporttests.h oscar/tests/contecbleimporttests.cpp oscar/oximeterimport.cpp oscar/oscar.pro
git commit -m "Move oximetry session building out of the import wizard

addOximetryEvents() and finishOximetrySession() hold the code that turned
the wizard's pulse/SpO2 records into a session, so other importers can
reuse it. The wizard behaves as before.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Решения по записям, правило стирания, сохранение, загрузчик, настройка

**Files:**
- Create: `oscar/SleepLib/loader_plugins/contec_ble_import.h`, `…/contec_ble_import.cpp`
- Create: `oscar/SleepLib/loader_plugins/contec_ble_loader.h`, `…/contec_ble_loader.cpp`
- Modify: `oscar/SleepLib/profiles.h` (`OxiSettings`: константа ~стр. 347, `initPref` ~стр. 544, геттер ~стр. 577, сеттер ~стр. 592)
- Modify: `oscar/SleepLib/serialoximeter.cpp:14-25` (`GetOxiLoaders` — пропускать `nullptr`)
- Modify: `oscar/main.cpp:1275` (регистрация)
- Modify: `oscar/tests/contecbleimporttests.{h,cpp}`, `oscar/oscar.pro`

**Interfaces:**
- Consumes: `RecordHeader`, `Record` (Task 1); `addOximetryEvents`, `finishOximetrySession` (Task 4).
- Produces (namespace `ContecBle`): `enum class Decision { Import, AlreadyPresent, ReplaceShorter, ConflictOtherOximeter, InvalidStart }`; `enum class Outcome { Imported, Updated, AlreadyPresent, ConflictOtherOximeter, InvalidStart, NotDownloaded, SaveFailed }`; `struct ExistingSession { bool exists; int samples; }`; `Decision decideRecord(const RecordHeader&, const ExistingSession&, bool nightHasOtherOximeter)`; `QDate predictNight(const QDateTime &startLocal, const QTime &daySplit)`; `QVector<OxiRecord> toOxiRecords(const Record&)`; `struct EraseInput { bool eraseEnabled; bool downloadCompleted; QList<Outcome> outcomes; int headersOnDevice; QDateTime lastRecordEnd; QDateTime downloadStarted; }`; `enum class EraseVerdict { Erase, Disabled, NothingToErase, DownloadIncomplete, NotAllSaved, StillRecording }`; `EraseVerdict canErase(const EraseInput&)`. Класс `ContecBleImporter(Machine*)` — `ExistingSession existing(const RecordHeader&) const`, `bool nightHasOtherOximeter(const RecordHeader&) const`, `QString otherOximeterName(const RecordHeader&) const`, `Decision decide(const RecordHeader&) const`, `Outcome save(const Record&, Decision)`, `void finish()`. `const QString contecble_class_name = "ContecBLE"`; `class ContecBleLoader : SerialOximeter` с `static MachineInfo infoForModel(const QString &model)`, `static void Register()`. `OxiSettings::bleEraseAfterImport()/setBleEraseAfterImport(bool)`.

- [ ] **Step 1: Заголовок `contec_ble_import.h`**

```cpp
/* Contec BLE Oximeter Import Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CONTEC_BLE_IMPORT_H
#define CONTEC_BLE_IMPORT_H

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QTime>
#include <QVector>

#include "SleepLib/loader_plugins/contec_ble_protocol.h"
#include "SleepLib/serialoximeter.h"

class Machine;

namespace ContecBle {

enum class Decision { Import, AlreadyPresent, ReplaceShorter, ConflictOtherOximeter, InvalidStart };
enum class Outcome { Imported, Updated, AlreadyPresent, ConflictOtherOximeter, InvalidStart, NotDownloaded, SaveFailed };

//! This device's session that starts at the same second, if any.
struct ExistingSession { bool exists = false; int samples = 0; };

Decision decideRecord(const RecordHeader &h, const ExistingSession &same, bool nightHasOtherOximeter);
//! The night OSCAR files a session under, by the day-split time alone.
QDate predictNight(const QDateTime &startLocal, const QTime &daySplit);
//! Samples as OSCAR oximetry records: "no data" markers become 0 (a gap); PI 0.1 % -> 0.01 %.
QVector<OxiRecord> toOxiRecords(const Record &r);

struct EraseInput {
    bool eraseEnabled = false;
    bool downloadCompleted = false;
    QList<Outcome> outcomes;        //!< one per record header the oximeter reported
    int headersOnDevice = 0;
    QDateTime lastRecordEnd;        //!< latest start + samples seconds (oximeter clock)
    QDateTime downloadStarted;
};
enum class EraseVerdict { Erase, Disabled, NothingToErase, DownloadIncomplete, NotAllSaved, StillRecording };
EraseVerdict canErase(const EraseInput &in);

} // namespace ContecBle

/*! \class ContecBleImporter
    \brief Stores downloaded Contec records as oximetry sessions of one OSCAR device. */
class ContecBleImporter
{
public:
    explicit ContecBleImporter(Machine *mach) : m_mach(mach) {}

    ContecBle::ExistingSession existing(const ContecBle::RecordHeader &h) const;
    bool nightHasOtherOximeter(const ContecBle::RecordHeader &h) const;
    QString otherOximeterName(const ContecBle::RecordHeader &h) const;
    ContecBle::Decision decide(const ContecBle::RecordHeader &h) const;
    //! Adds the record as a session (replacing a shorter one when told to) and saves it.
    ContecBle::Outcome save(const ContecBle::Record &r, ContecBle::Decision d);
    //! Updates the daily summaries after the last save.
    void finish();

private:
    Machine *m_mach;
};

#endif // CONTEC_BLE_IMPORT_H
```

- [ ] **Step 2: Заголовок `contec_ble_loader.h`**

```cpp
/* Contec BLE Oximeter Loader Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CONTEC_BLE_LOADER_H
#define CONTEC_BLE_LOADER_H

#include "SleepLib/serialoximeter.h"

const QString contecble_class_name = "ContecBLE";
const int contecble_data_version = 1;

/*! \class ContecBleLoader
    \brief Identity of devices imported over Bluetooth (the import itself runs from the wizard).
    A SerialOximeter so GetOxiLoaders() lists it like the other oximeter loaders; it claims no files. */
class ContecBleLoader : public SerialOximeter
{
    Q_OBJECT
public:
    ContecBleLoader();
    ~ContecBleLoader() override = default;

    bool Detect(const QString &) override { return false; }
    int Open(const QString &) override { return 0; }
    static void Register();

    int Version() override { return contecble_data_version; }
    const QString &loaderName() override { return contecble_class_name; }
    MachineInfo newInfo() override { return infoForModel(QString()); }
    static MachineInfo infoForModel(const QString &model);
};

#endif // CONTEC_BLE_LOADER_H
```

- [ ] **Step 3: Настройка в `oscar/SleepLib/profiles.h`**

После строки `const QString STR_OS_SkipOxiIntroScreen = "SkipOxiIntroScreen";` добавить
```cpp
const QString STR_OS_BleEraseAfterImport = "BleEraseAfterImport";
```
после `        initPref(STR_OS_SkipOxiIntroScreen, false);` добавить
```cpp
        initPref(STR_OS_BleEraseAfterImport, false);
```
после `    bool skipOxiIntroScreen() const { return getPref(STR_OS_SkipOxiIntroScreen).toBool(); }` добавить
```cpp
    bool bleEraseAfterImport() const { return getPref(STR_OS_BleEraseAfterImport).toBool(); }
```
после `    void setSkipOxiIntroScreen(bool skip) { setPref(STR_OS_SkipOxiIntroScreen, skip); }` добавить
```cpp
    void setBleEraseAfterImport(bool erase) { setPref(STR_OS_BleEraseAfterImport, erase); }
```

- [ ] **Step 4: Падающие тесты** — `contecbleimporttests.h`: заменить объявления на

```cpp
class QCoreApplication;
class QTemporaryDir;

class ContecBleImportTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testOximetryEventsSplitAtGaps();
    void testDecideRecord();
    void testPredictNight();
    void testToOxiRecords();
    void testCanErase();
    void testEraseSettingDefaultsOff();
    void testImporterImportsNewRecord();
    void testImporterReplacesShorterRecord();
    void testImporterRefusesSecondOximeterOnANight();
    void cleanupTestCase();
private:
    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
};
DECLARE_TEST(ContecBleImportTests)
```

В `contecbleimporttests.cpp` дополнить include'ы:
```cpp
#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/day.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/loader_plugins/contec_ble_import.h"
#include "SleepLib/loader_plugins/contec_ble_loader.h"
#include "database/database_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

using namespace ContecBle;

namespace {
const QString kProfileName = QStringLiteral("ContecBleUnitTest");

RecordHeader header(const QDateTime &start, int samples)
{
    RecordHeader h;
    h.l = 1;
    h.m = 1;
    h.year = start.date().year();
    h.month = start.date().month();
    h.day = start.date().day();
    h.hour = start.time().hour();
    h.minute = start.time().minute();
    h.second = start.time().second();
    h.samples = samples;
    return h;
}

Record record(const QDateTime &start, int samples)
{
    Record r;
    r.header = header(start, samples);
    for (int i = 0; i < samples; ++i) {
        r.spo2.append(95 + i % 3);
        r.pulse.append(60 + i % 5);
    }
    return r;
}
} // namespace
```
заменить `initTestCase` на полную настройку профиля (как в `applehealthtests.cpp`) и добавить `cleanupTestCase`:
```cpp
void ContecBleImportTests::initTestCase()
{
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    if (QCoreApplication::instance() == nullptr) m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) DatabaseManager::instance().close();
    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-contecbletests-XXXXXX"));
    QVERIFY(m_tempDir->isValid());
    m_previousAppData = GetAppData();
    SetAppData(m_tempDir->path());

    p_profile = nullptr;
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));

    if (CPAP_Obstructive == 0) { schema::init(); }
    ContecBleLoader::Register();
    Profiles::Scan();
    const QString profileDir = m_tempDir->path() + QStringLiteral("/Profiles/") + kProfileName;
    p_profile = Profiles::Get(kProfileName);
    if (p_profile == nullptr) p_profile = Profiles::Create(kProfileName, &profileDir);
    QVERIFY(p_profile != nullptr);
}

void ContecBleImportTests::cleanupTestCase()
{
    Profiles::profiles.clear();
    delete p_profile;
    p_profile = nullptr;
    delete AppSetting;
    AppSetting = nullptr;
    delete p_pref;
    p_pref = nullptr;
    DatabaseManager::instance().close();
    SetAppData(m_previousAppData);
    delete m_tempDir;
    m_tempDir = nullptr;
    delete m_app;
    m_app = nullptr;
}
```
и в конец файла:
```cpp
void ContecBleImportTests::testDecideRecord()
{
    const RecordHeader h = header(QDateTime(QDate(2026, 9, 26), QTime(23, 59, 13)), 600);
    QCOMPARE(decideRecord(h, ExistingSession(), false), Decision::Import);
    QCOMPARE(decideRecord(h, ExistingSession(), true), Decision::ConflictOtherOximeter);
    QCOMPARE(decideRecord(h, ExistingSession{ true, 600 }, false), Decision::AlreadyPresent);
    QCOMPARE(decideRecord(h, ExistingSession{ true, 400 }, false), Decision::ReplaceShorter);
    RecordHeader bad = h;
    bad.hour = 30;
    QCOMPARE(decideRecord(bad, ExistingSession(), false), Decision::InvalidStart);
}

void ContecBleImportTests::testPredictNight()
{
    const QTime noon(12, 0, 0);
    QCOMPARE(predictNight(QDateTime(QDate(2026, 9, 26), QTime(23, 59, 13)), noon), QDate(2026, 9, 26));
    QCOMPARE(predictNight(QDateTime(QDate(2026, 9, 27), QTime(5, 41, 54)), noon), QDate(2026, 9, 26));
    QCOMPARE(predictNight(QDateTime(QDate(2026, 9, 27), QTime(12, 0, 0)), noon), QDate(2026, 9, 27));
}

void ContecBleImportTests::testToOxiRecords()
{
    Record r;
    r.header = header(QDateTime(QDate(2026, 9, 26), QTime(23, 0, 0)), 4);
    r.header.hasPI = true;
    r.spo2 = { 97, 127, 0, 101 };
    r.pulse = { 64, 255, 0, 70 };
    r.pi = { 23, 255, 0, 5 };
    const QVector<OxiRecord> o = toOxiRecords(r);
    QCOMPARE(o.size(), 4);
    QCOMPARE(int(o[0].spo2), 97);
    QCOMPARE(int(o[0].pulse), 64);
    QCOMPARE(int(o[0].perf), 230);
    for (int i : { 1, 2 }) {
        QCOMPARE(int(o[i].spo2), 0);
        QCOMPARE(int(o[i].pulse), 0);
        QCOMPARE(int(o[i].perf), 0);
    }
    QCOMPARE(int(o[3].spo2), 0);        // > 100 % is not a reading
    QCOMPARE(int(o[3].pulse), 70);
}

void ContecBleImportTests::testCanErase()
{
    EraseInput ok;
    ok.eraseEnabled = true;
    ok.downloadCompleted = true;
    ok.outcomes = { Outcome::Imported, Outcome::AlreadyPresent, Outcome::Updated };
    ok.headersOnDevice = 3;
    ok.downloadStarted = QDateTime(QDate(2026, 9, 27), QTime(9, 0, 0));
    ok.lastRecordEnd = ok.downloadStarted.addSecs(-301);
    QCOMPARE(canErase(ok), EraseVerdict::Erase);

    EraseInput e = ok; e.eraseEnabled = false;
    QCOMPARE(canErase(e), EraseVerdict::Disabled);
    e = ok; e.downloadCompleted = false;
    QCOMPARE(canErase(e), EraseVerdict::DownloadIncomplete);
    e = ok; e.headersOnDevice = 0; e.outcomes.clear();
    QCOMPARE(canErase(e), EraseVerdict::NothingToErase);
    e = ok; e.outcomes[1] = Outcome::ConflictOtherOximeter;
    QCOMPARE(canErase(e), EraseVerdict::NotAllSaved);
    e = ok; e.outcomes.removeLast();
    QCOMPARE(canErase(e), EraseVerdict::NotAllSaved);
    for (Outcome bad : { Outcome::InvalidStart, Outcome::NotDownloaded, Outcome::SaveFailed }) {
        e = ok; e.outcomes[0] = bad;
        QCOMPARE(canErase(e), EraseVerdict::NotAllSaved);
    }
    e = ok; e.lastRecordEnd = ok.downloadStarted.addSecs(-120);
    QCOMPARE(canErase(e), EraseVerdict::StillRecording);
}

void ContecBleImportTests::testEraseSettingDefaultsOff()
{
    QVERIFY(!p_profile->oxi->bleEraseAfterImport());
    p_profile->oxi->setBleEraseAfterImport(true);
    QVERIFY(p_profile->oxi->bleEraseAfterImport());
    p_profile->oxi->setBleEraseAfterImport(false);
}

void ContecBleImportTests::testImporterImportsNewRecord()
{
    Machine *mach = p_profile->CreateMachine(ContecBleLoader::infoForModel(QStringLiteral("CMS50FW")));
    QVERIFY(mach != nullptr);
    ContecBleImporter imp(mach);
    const QDateTime start(QDate(2026, 9, 26), QTime(23, 59, 13));
    const Record r = record(start, 600);
    QCOMPARE(imp.decide(r.header), Decision::Import);
    QCOMPARE(imp.save(r, Decision::Import), Outcome::Imported);

    const SessionID sid = SessionID(start.toUTC().toSecsSinceEpoch());
    QVERIFY(mach->sessionlist.contains(sid));
    Session *s = mach->sessionlist[sid];
    QCOMPARE(s->realFirst(), qint64(sid) * 1000);
    QCOMPARE(s->realLast(), qint64(sid) * 1000 + 599 * 1000);
    QCOMPARE(s->night(), QDate(2026, 9, 26));
    QCOMPARE(imp.decide(r.header), Decision::AlreadyPresent);
}

void ContecBleImportTests::testImporterReplacesShorterRecord()
{
    Machine *mach = p_profile->CreateMachine(ContecBleLoader::infoForModel(QStringLiteral("CMS50FW")));
    ContecBleImporter imp(mach);
    const QDateTime start(QDate(2026, 9, 20), QTime(22, 0, 0));
    QCOMPARE(imp.save(record(start, 600), Decision::Import), Outcome::Imported);
    const Record longer = record(start, 900);
    QCOMPARE(imp.decide(longer.header), Decision::ReplaceShorter);
    QCOMPARE(imp.save(longer, Decision::ReplaceShorter), Outcome::Updated);
    const SessionID sid = SessionID(start.toUTC().toSecsSinceEpoch());
    QCOMPARE(mach->sessionlist[sid]->realLast(), qint64(sid) * 1000 + 899 * 1000);
    QCOMPARE(imp.decide(longer.header), Decision::AlreadyPresent);
}

void ContecBleImportTests::testImporterRefusesSecondOximeterOnANight()
{
    const MachineInfo otherInfo(MT_OXIMETER, 0, QStringLiteral("DummyOximeter"), QStringLiteral("Test"),
                                QStringLiteral("Other Oximeter"), QString(), QStringLiteral("dummy-oxi"),
                                QStringLiteral("Test"), QDateTime::currentDateTime(), 1);
    Machine *other = p_profile->CreateMachine(otherInfo);
    const QDateTime otherStart(QDate(2026, 9, 10), QTime(23, 0, 0));
    const SessionID otherSid = SessionID(otherStart.toUTC().toSecsSinceEpoch());
    Session *os = new Session(other, otherSid);
    os->really_set_first(qint64(otherSid) * 1000);
    const qint64 last = addOximetryEvents(os, qint64(otherSid) * 1000, toOxiRecords(record(otherStart, 600)), 1000, false);
    finishOximetrySession(os, last, false);
    QVERIFY(other->AddSession(os));

    Machine *mach = p_profile->CreateMachine(ContecBleLoader::infoForModel(QStringLiteral("CMS50FW")));
    ContecBleImporter imp(mach);
    const QDateTime start(QDate(2026, 9, 11), QTime(1, 0, 0));    // same night (before the noon split)
    const Record r = record(start, 600);
    QVERIFY(imp.nightHasOtherOximeter(r.header));
    QCOMPARE(imp.otherOximeterName(r.header), QStringLiteral("Other Oximeter"));
    QCOMPARE(imp.decide(r.header), Decision::ConflictOtherOximeter);
    // Even if asked to import, the session must not be left behind in the device.
    QCOMPARE(imp.save(r, Decision::Import), Outcome::ConflictOtherOximeter);
    QVERIFY(!mach->sessionlist.contains(SessionID(start.toUTC().toSecsSinceEpoch())));
}
```

- [ ] **Step 5: Подключить в `oscar.pro`**

Основные `SOURCES`/`HEADERS`: после `contec_ble_downloader.*` добавить `SleepLib/loader_plugins/contec_ble_import.cpp \`, `SleepLib/loader_plugins/contec_ble_loader.cpp \` (и `.h`).

- [ ] **Step 6: Убедиться, что тесты не собираются** (заглушки `.cpp` с include'ом, qmake, сборка)
Expected: неопределённые `ContecBle::decideRecord`, `ContecBleLoader::…`, `ContecBleImporter::…`.

- [ ] **Step 7: Реализация `contec_ble_loader.cpp`**

```cpp
/* Contec BLE Oximeter Loader
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contec_ble_loader.h"

#include <QDebug>

static bool contecble_initialized = false;

ContecBleLoader::ContecBleLoader()
{
    m_type = MT_OXIMETER;
}

MachineInfo ContecBleLoader::infoForModel(const QString &model)
{
    return MachineInfo(MT_OXIMETER, 0, contecble_class_name, QObject::tr("Contec"),
                       model.isEmpty() ? QObject::tr("Bluetooth oximeter") : model,
                       QString(), QString(), model, QDateTime::currentDateTime(), contecble_data_version);
}

void ContecBleLoader::Register()
{
    if (contecble_initialized) return;
    qDebug() << "Registering ContecBleLoader";
    RegisterLoader(new ContecBleLoader());
    contecble_initialized = true;
}
```

- [ ] **Step 8: Реализация `contec_ble_import.cpp`**

```cpp
/* Contec BLE Oximeter Import
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contec_ble_import.h"

#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/oximetry_session_builder.h"
#include "SleepLib/profiles.h"
#include "SleepLib/session.h"

namespace ContecBle {

Decision decideRecord(const RecordHeader &h, const ExistingSession &same, bool nightHasOtherOximeter)
{
    if (!h.start().isValid()) return Decision::InvalidStart;
    if (same.exists) return same.samples >= h.samples ? Decision::AlreadyPresent : Decision::ReplaceShorter;
    if (nightHasOtherOximeter) return Decision::ConflictOtherOximeter;
    return Decision::Import;
}

QDate predictNight(const QDateTime &startLocal, const QTime &daySplit)
{
    const QDate d = startLocal.date();
    return startLocal.time() < daySplit ? d.addDays(-1) : d;
}

QVector<OxiRecord> toOxiRecords(const Record &r)
{
    const int n = r.header.samples;
    QVector<OxiRecord> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const int s = i < r.spo2.size() ? r.spo2[i] : 0;
        const int p = i < r.pulse.size() ? r.pulse[i] : 0;
        const int pi = (r.header.hasPI && i < r.pi.size()) ? r.pi[i] : 0;
        const quint8 spo2 = (s > 0 && s <= 100) ? quint8(s) : 0;
        const quint8 pulse = (p > 0 && p < 255) ? quint8(p) : 0;
        const quint16 perf = (pi > 0 && pi < 255) ? quint16(pi * 10) : 0;
        out.append(OxiRecord(pulse, spo2, perf));
    }
    return out;
}

EraseVerdict canErase(const EraseInput &in)
{
    if (!in.eraseEnabled) return EraseVerdict::Disabled;
    if (!in.downloadCompleted) return EraseVerdict::DownloadIncomplete;
    if (in.headersOnDevice <= 0) return EraseVerdict::NothingToErase;
    if (in.outcomes.size() < in.headersOnDevice) return EraseVerdict::NotAllSaved;
    for (Outcome o : in.outcomes) {
        if (o != Outcome::Imported && o != Outcome::Updated && o != Outcome::AlreadyPresent)
            return EraseVerdict::NotAllSaved;
    }
    // The oximeter keeps writing while the sensor is on; erasing now would lose the tail.
    if (in.lastRecordEnd.isValid() && in.lastRecordEnd > in.downloadStarted.addSecs(-300))
        return EraseVerdict::StillRecording;
    return EraseVerdict::Erase;
}

} // namespace ContecBle

using namespace ContecBle;

namespace {
Machine *otherOximeter(Machine *mine, const RecordHeader &h)
{
    const QDateTime start = h.start();
    if (!start.isValid() || !p_profile) return nullptr;
    Day *day = p_profile->GetDay(predictNight(start, p_profile->session->daySplitTime()), MT_OXIMETER);
    Machine *oxi = day ? day->machine(MT_OXIMETER) : nullptr;
    return (oxi && oxi != mine) ? oxi : nullptr;
}

QString deviceName(Machine *m)
{
    const QString model = m->model().trimmed();
    return model.isEmpty() ? m->loaderName() : model;
}
} // namespace

ExistingSession ContecBleImporter::existing(const RecordHeader &h) const
{
    ExistingSession e;
    const QDateTime start = h.start();
    if (!start.isValid()) return e;
    Session *s = m_mach->sessionlist.value(SessionID(start.toUTC().toSecsSinceEpoch()), nullptr);
    if (s) {
        e.exists = true;
        e.samples = int((s->realLast() - s->realFirst()) / 1000) + 1;
    }
    return e;
}

bool ContecBleImporter::nightHasOtherOximeter(const RecordHeader &h) const
{
    return otherOximeter(m_mach, h) != nullptr;
}

QString ContecBleImporter::otherOximeterName(const RecordHeader &h) const
{
    Machine *other = otherOximeter(m_mach, h);
    return other ? deviceName(other) : QString();
}

Decision ContecBleImporter::decide(const RecordHeader &h) const
{
    return decideRecord(h, existing(h), nightHasOtherOximeter(h));
}

Outcome ContecBleImporter::save(const Record &r, Decision d)
{
    const QDateTime start = r.header.start();
    if (!start.isValid()) return Outcome::InvalidStart;
    const QVector<OxiRecord> recs = toOxiRecords(r);
    if (recs.isEmpty()) return Outcome::SaveFailed;
    const SessionID sid = SessionID(start.toUTC().toSecsSinceEpoch());
    const qint64 startMs = qint64(sid) * 1000;

    if (d == Decision::ReplaceShorter) {
        if (Session *old = m_mach->sessionlist.value(sid, nullptr)) {
            old->Destroy();
            delete old;
        }
    }

    Session *sess = new Session(m_mach, sid);
    sess->really_set_first(startMs);
    const qint64 lastMs = addOximetryEvents(sess, startMs, recs, 1000, r.header.hasPI);
    finishOximetrySession(sess, lastMs, r.header.hasPI);
    if (!m_mach->AddSession(sess)) {
        delete sess;
        return Outcome::SaveFailed;
    }
    // A day holds one oximeter: Day::addSession refuses a second one without telling the caller.
    if (sess->night().isValid()) {
        Day *day = p_profile->GetDay(sess->night());
        if (!day || !day->sessions.contains(sess)) {
            m_mach->unlinkSession(sess);
            delete sess;
            return Outcome::ConflictOtherOximeter;
        }
    }
    m_mach->Save();
    m_mach->SaveSummaryCache();
    p_profile->StoreMachines();
    return d == Decision::ReplaceShorter ? Outcome::Updated : Outcome::Imported;
}

void ContecBleImporter::finish()
{
    if (p_profile) p_profile->calculateDailySummaries();
}
```

- [ ] **Step 9: Регистрация и защита `GetOxiLoaders`**

`oscar/main.cpp`: после строки `    MD300W1Loader::Register();` добавить `    ContecBleLoader::Register();` и к include'ам (рядом с `#include "SleepLib/loader_plugins/md300w1_loader.h"`) — `#include "SleepLib/loader_plugins/contec_ble_loader.h"`.

`oscar/SleepLib/serialoximeter.cpp`, в `GetOxiLoaders()` заменить
```cpp
        SerialOximeter * oxi = qobject_cast<SerialOximeter *>(loader);
        oxiloaders.push_back(oxi);
```
на
```cpp
        SerialOximeter * oxi = qobject_cast<SerialOximeter *>(loader);
        if (oxi) oxiloaders.push_back(oxi);    // an oximeter loader that is not a SerialOximeter would crash callers
```

- [ ] **Step 10: Прогнать тесты**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; /Users/semyk/Downloads/Oscar_Project/tools/runtests.sh ContecBleImportTests ContecBleDownloaderTests ContecBleProtocolTests`
Expected: `ContecBleImportTests: 11 PASS`, `0 unexpected`.

- [ ] **Step 11: Приложение и предупреждения**

Run: qmake в `build`; `make`; `…/warncheck.sh contec_ble_import contec_ble_loader serialoximeter main | comm -13 …/warn-baseline-ble.txt -`
Expected: пусто.

- [ ] **Step 12: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/SleepLib/loader_plugins/contec_ble_import.h oscar/SleepLib/loader_plugins/contec_ble_import.cpp oscar/SleepLib/loader_plugins/contec_ble_loader.h oscar/SleepLib/loader_plugins/contec_ble_loader.cpp oscar/SleepLib/profiles.h oscar/SleepLib/serialoximeter.cpp oscar/main.cpp oscar/tests/contecbleimporttests.h oscar/tests/contecbleimporttests.cpp oscar/oscar.pro
git commit -m "Decide, store and erase-guard Contec Bluetooth records

Each record becomes a session of a 'Contec <model>' oximeter: new records
are imported, identical ones skipped, longer ones replace the shorter
copy, and a night that already has another oximeter is reported instead
of being silently dropped by Day::addSession. canErase() allows the
optional erase only when every record is safely in OSCAR and the
oximeter is not still recording. Adds the BleEraseAfterImport setting,
registers the loader and makes GetOxiLoaders() skip non-serial loaders.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Bluetooth-канал, сборка с/без Bluetooth, Info.plist

**Files:**
- Create: `oscar/SleepLib/loader_plugins/contec_ble_link.h`, `…/contec_ble_link.cpp`
- Modify: `oscar/oscar.pro` (после строки `QT += core gui network xml printsupport serialport sql widgets help concurrent`)
- Modify: `Building/MacOS/Info.plist.in`

**Interfaces:**
- Consumes: `ContecBleLink` (Task 3), `modelForName`, UUID-константы (Task 1).
- Produces: `struct ContecBleFoundDevice { QBluetoothDeviceInfo info; QString name; QString model; int rssi; }`; `class ContecBleScanner : QObject { void start(int timeoutMs = 15000); void stop(); signals: finished(const QList<ContecBleFoundDevice>&) /*по убыванию RSSI*/, failed(QString) }`; `class QtContecBleLink : ContecBleLink { void connectTo(const QBluetoothDeviceInfo&); void disconnectFromDevice(); void write(const QByteArray&) override; signals: ready(), failed(QString) }`.

- [ ] **Step 1: `contec_ble_link.h`**

```cpp
/* Contec BLE Oximeter Bluetooth Link Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CONTEC_BLE_LINK_H
#define CONTEC_BLE_LINK_H

#include <QBluetoothDeviceInfo>
#include <QList>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyService>
#include <QTimer>

#include "SleepLib/loader_plugins/contec_ble_downloader.h"

class QBluetoothDeviceDiscoveryAgent;
class QLowEnergyController;

struct ContecBleFoundDevice {
    QBluetoothDeviceInfo info;
    QString name;
    QString model;
    int rssi = 0;
};

//! Finds Contec oximeters that advertise over Bluetooth LE.
class ContecBleScanner : public QObject
{
    Q_OBJECT
public:
    explicit ContecBleScanner(QObject *parent = nullptr);
    void start(int timeoutMs = 15000);
    void stop();
signals:
    void finished(const QList<ContecBleFoundDevice> &devices);   //!< strongest signal first
    void failed(const QString &message);
private:
    void onDiscovered(const QBluetoothDeviceInfo &info);
    void onFinished();
    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    QList<ContecBleFoundDevice> m_found;
};

//! ContecBleLink over Qt Bluetooth LE: service ff12, notifications from ff02, writes to ff01.
class QtContecBleLink : public ContecBleLink
{
    Q_OBJECT
public:
    explicit QtContecBleLink(QObject *parent = nullptr);
    ~QtContecBleLink() override;
    void connectTo(const QBluetoothDeviceInfo &device);
    void disconnectFromDevice();
    void write(const QByteArray &data) override;
signals:
    void ready();
    void failed(const QString &message);
private:
    void setupService();
    void onServiceState(QLowEnergyService::ServiceState state);
    void failOnce(const QString &message);

    QLowEnergyController *m_controller = nullptr;
    QLowEnergyService *m_service = nullptr;
    QLowEnergyCharacteristic m_writeChar;
    QLowEnergyService::WriteMode m_writeMode = QLowEnergyService::WriteWithResponse;
    QTimer m_connectTimer;
    bool m_ready = false;
    bool m_failed = false;
};

#endif // CONTEC_BLE_LINK_H
```

- [ ] **Step 2: `contec_ble_link.cpp`**

```cpp
/* Contec BLE Oximeter Bluetooth Link
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contec_ble_link.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothUuid>
#include <QDebug>
#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <algorithm>

using namespace ContecBle;

ContecBleScanner::ContecBleScanner(QObject *parent)
    : QObject(parent)
{
}

void ContecBleScanner::start(int timeoutMs)
{
    m_found.clear();
    if (!m_agent) {
        m_agent = new QBluetoothDeviceDiscoveryAgent(this);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &ContecBleScanner::onDiscovered);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this, &ContecBleScanner::onFinished);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
                [this](QBluetoothDeviceDiscoveryAgent::Error error) {
            switch (error) {
            case QBluetoothDeviceDiscoveryAgent::PoweredOffError:
                emit failed(tr("Bluetooth is turned off on this computer."));
                break;
            case QBluetoothDeviceDiscoveryAgent::MissingPermissionsError:
                emit failed(tr("OSCAR isn't allowed to use Bluetooth. Allow it in System Settings > Privacy & Security > Bluetooth."));
                break;
            default:
                emit failed(m_agent->errorString());
                break;
            }
        });
    }
    m_agent->setLowEnergyDiscoveryTimeout(timeoutMs);
    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void ContecBleScanner::stop()
{
    if (m_agent && m_agent->isActive()) m_agent->stop();
}

void ContecBleScanner::onDiscovered(const QBluetoothDeviceInfo &info)
{
    if (!(info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)) return;
    const ModelInfo model = modelForName(info.name());
    if (!model.isValid()) return;
    for (ContecBleFoundDevice &f : m_found) {
        if (f.info.address() == info.address() && f.info.deviceUuid() == info.deviceUuid()) {
            f.info = info;
            f.rssi = info.rssi();
            return;
        }
    }
    ContecBleFoundDevice f;
    f.info = info;
    f.name = info.name();
    f.model = model.model;
    f.rssi = info.rssi();
    m_found.append(f);
}

void ContecBleScanner::onFinished()
{
    std::sort(m_found.begin(), m_found.end(),
              [](const ContecBleFoundDevice &a, const ContecBleFoundDevice &b) { return a.rssi > b.rssi; });
    emit finished(m_found);
}

QtContecBleLink::QtContecBleLink(QObject *parent)
    : ContecBleLink(parent)
{
    m_connectTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, [this]() {
        if (!m_ready) failOnce(tr("Couldn't connect to the oximeter."));
    });
}

QtContecBleLink::~QtContecBleLink()
{
    disconnectFromDevice();
}

void QtContecBleLink::failOnce(const QString &message)
{
    if (m_failed) return;
    m_failed = true;
    m_connectTimer.stop();
    qWarning() << "ContecBLE link:" << message;
    emit failed(message);
}

void QtContecBleLink::connectTo(const QBluetoothDeviceInfo &device)
{
    m_controller = QLowEnergyController::createCentral(device, this);
    connect(m_controller, &QLowEnergyController::connected, this, [this]() {
        QTimer::singleShot(800, this, [this]() {       // the vendor SDK waits before discovery
            if (m_controller) m_controller->discoverServices();
        });
    });
    connect(m_controller, &QLowEnergyController::discoveryFinished, this, &QtContecBleLink::setupService);
    connect(m_controller, &QLowEnergyController::errorOccurred, this, [this](QLowEnergyController::Error) {
        failOnce(m_controller->errorString());
    });
    connect(m_controller, &QLowEnergyController::disconnected, this, [this]() {
        if (m_ready) emit linkLost();
        else failOnce(tr("The oximeter disconnected."));
    });
    m_connectTimer.start(20000);
    m_controller->connectToDevice();
}

void QtContecBleLink::setupService()
{
    m_service = m_controller->createServiceObject(QBluetoothUuid(kServiceShortUuid), this);
    if (!m_service) {
        failOnce(tr("This device doesn't offer the Contec oximeter service."));
        return;
    }
    connect(m_service, &QLowEnergyService::stateChanged, this, &QtContecBleLink::onServiceState);
    connect(m_service, &QLowEnergyService::characteristicChanged, this,
            [this](const QLowEnergyCharacteristic &c, const QByteArray &value) {
        if (c.uuid() == QBluetoothUuid(kNotifyShortUuid)) emit received(value);
    });
    connect(m_service, &QLowEnergyService::descriptorWritten, this,
            [this](const QLowEnergyDescriptor &, const QByteArray &value) {
        if (value == QLowEnergyCharacteristic::CCCDEnableNotification && !m_ready) {
            m_ready = true;
            m_connectTimer.stop();
            emit ready();
        }
    });
    connect(m_service, &QLowEnergyService::errorOccurred, this, [this](QLowEnergyService::ServiceError) {
        failOnce(tr("Bluetooth communication with the oximeter failed."));
    });
    m_service->discoverDetails();
}

void QtContecBleLink::onServiceState(QLowEnergyService::ServiceState state)
{
    if (state != QLowEnergyService::RemoteServiceDiscovered) return;
    m_writeChar = m_service->characteristic(QBluetoothUuid(kWriteShortUuid));
    const QLowEnergyCharacteristic notify = m_service->characteristic(QBluetoothUuid(kNotifyShortUuid));
    if (!m_writeChar.isValid() || !notify.isValid()) {
        failOnce(tr("This device doesn't offer the Contec oximeter service."));
        return;
    }
    m_writeMode = (m_writeChar.properties() & QLowEnergyCharacteristic::WriteNoResponse)
                      ? QLowEnergyService::WriteWithoutResponse : QLowEnergyService::WriteWithResponse;
    const QLowEnergyDescriptor cccd = notify.clientCharacteristicConfiguration();
    if (!cccd.isValid()) {
        failOnce(tr("Couldn't turn on notifications from the oximeter."));
        return;
    }
    m_service->writeDescriptor(cccd, QLowEnergyCharacteristic::CCCDEnableNotification);
}

void QtContecBleLink::write(const QByteArray &data)
{
    if (!m_service || !m_writeChar.isValid()) return;
    for (int i = 0; i < data.size(); i += 20) {      // default ATT MTU: 20 bytes per write
        m_service->writeCharacteristic(m_writeChar, data.mid(i, 20), m_writeMode);
    }
}

void QtContecBleLink::disconnectFromDevice()
{
    m_connectTimer.stop();
    if (m_controller && m_controller->state() != QLowEnergyController::UnconnectedState) {
        m_ready = false;                 // a deliberate disconnect is not a lost link
        m_controller->disconnectFromDevice();
    }
}
```

- [ ] **Step 3: `oscar.pro` — необязательный модуль Bluetooth**

После строки `QT += core gui network xml printsupport serialport sql widgets help concurrent` добавить:
```
# Bluetooth oximeter import. Optional: OSCAR still builds without the Qt Bluetooth module
# (or with CONFIG+=no_bluetooth), just without the Bluetooth import button.
qtHaveModule(bluetooth):!no_bluetooth {
    QT += bluetooth
    DEFINES += HAVE_BLUETOOTH
    SOURCES += SleepLib/loader_plugins/contec_ble_link.cpp
    HEADERS += SleepLib/loader_plugins/contec_ble_link.h
} else {
    message("Building without Bluetooth oximeter import")
}
```

- [ ] **Step 4: `Building/MacOS/Info.plist.in`** — перед `</dict>` добавить

```xml
  <key>NSBluetoothAlwaysUsageDescription</key>
  <string>OSCAR uses Bluetooth to import recordings from your pulse oximeter.</string>
```

- [ ] **Step 5: Сборка с Bluetooth + проверки**

Run:
```bash
cd /Users/semyk/Downloads/Oscar_Project/build && /opt/homebrew/bin/qmake ../oscar-sql/oscar/oscar.pro -spec macx-clang > qmake.log 2>&1; grep -c "without Bluetooth" qmake.log; make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; plutil -p OSCAR20.app/Contents/Info.plist | grep -i bluetooth; otool -L OSCAR20.app/Contents/MacOS/OSCAR20 | grep -i bluetooth
/Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh contec_ble_link | comm -13 /Users/semyk/Downloads/Oscar_Project/tools/warn-baseline-ble.txt -
```
Expected: `0` (сообщения «without Bluetooth» нет); ошибок нет; строка с `NSBluetoothAlwaysUsageDescription`; строка с `QtBluetooth`; новых предупреждений нет. (Если имя бандла в `build` иное — взять `ls -d build/*.app`.)

- [ ] **Step 6: Сборка без Bluetooth**

Run:
```bash
mkdir -p /Users/semyk/Downloads/Oscar_Project/build-nobt && cd /Users/semyk/Downloads/Oscar_Project/build-nobt && /opt/homebrew/bin/qmake ../oscar-sql/oscar/oscar.pro -spec macx-clang CONFIG+=no_bluetooth > qmake.log 2>&1; grep -c "without Bluetooth" qmake.log; make -j$(sysctl -n hw.logicalcpu) > make.log 2>&1; echo "nobt make exit $?"; grep -c HAVE_BLUETOOTH Makefile
```
Expected: `1`, `nobt make exit 0`, `0`.

- [ ] **Step 7: Тесты** (тестовая сборка тоже подхватывает модуль)

Run: перезапустить qmake в `build-test`, `make`, `…/runtests.sh ContecBleImportTests ContecBleDownloaderTests ContecBleProtocolTests`
Expected: `0 unexpected`.

- [ ] **Step 8: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/SleepLib/loader_plugins/contec_ble_link.h oscar/SleepLib/loader_plugins/contec_ble_link.cpp oscar/oscar.pro Building/MacOS/Info.plist.in
git commit -m "Add the Qt Bluetooth link for Contec oximeters

LE scanning for SpO2xx names, connection with the vendor app's timing,
notifications from ff02 and writes to ff01 in 20-byte pieces. Built only
when the Qt Bluetooth module is available (CONFIG+=no_bluetooth turns it
off); macOS gets the Bluetooth usage description it requires.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Страница мастера «Bluetooth import»

**Files:**
- Create: `oscar/bluetoothoximeterpage.h`, `oscar/bluetoothoximeterpage.cpp`
- Modify: `oscar/oximeterimport.h`, `oscar/oximeterimport.cpp` (конструктор ~стр. 158–165, `on_cancelButton_clicked`)
- Modify: `oscar/oscar.pro` (блок `qtHaveModule(bluetooth)` из Task 6)

**Interfaces:**
- Consumes: `ContecBleScanner`, `QtContecBleLink`, `ContecBleFoundDevice` (Task 6); `ContecBleDownloader` (Task 3); `ContecBleImporter`, `Decision`, `Outcome`, `EraseInput`, `canErase`, `EraseVerdict` (Task 5); `ContecBleLoader::infoForModel` (Task 5); `p_profile->oxi->syncOximeterClock()/setSyncOximeterClock()`, `bleEraseAfterImport()/setBleEraseAfterImport()`.
- Produces: `class BluetoothOximeterPage : QWidget { void start(); bool isBusy() const; void cancel(); signals: finished(bool importedSomething); }`; `OximeterImport::onBluetoothImportClicked()`, `OximeterImport::onBluetoothFinished(bool)`.

- [ ] **Step 1: `bluetoothoximeterpage.h`**

```cpp
/* Bluetooth Oximeter Import Page Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BLUETOOTHOXIMETERPAGE_H
#define BLUETOOTHOXIMETERPAGE_H

#include <QDateTime>
#include <QList>
#include <QPointer>
#include <QWidget>
#include <memory>

#include "SleepLib/loader_plugins/contec_ble_import.h"
#include "SleepLib/loader_plugins/contec_ble_link.h"

class QCheckBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTableWidget;

/*! \class BluetoothOximeterPage
    \brief Oximeter wizard page that imports every new record from a Contec oximeter over
    Bluetooth, optionally sets its clock and (only when it is safe) erases it. */
class BluetoothOximeterPage : public QWidget
{
    Q_OBJECT
public:
    explicit BluetoothOximeterPage(QWidget *parent = nullptr);
    ~BluetoothOximeterPage() override;

    void start();
    bool isBusy() const { return m_busy; }
    void cancel();

signals:
    void finished(bool importedSomething);

private:
    enum Step { StepScan = 0, StepRead, StepDownload, StepSave, StepCount };
    struct Row {
        ContecBle::RecordHeader header;
        ContecBle::Decision decision = ContecBle::Decision::Import;
        bool pending = false;
        ContecBle::Outcome outcome = ContecBle::Outcome::NotDownloaded;
        QString otherDevice;
    };

    void setStep(int step, bool allDone = false);
    void connectTo(const ContecBleFoundDevice &device);
    void onScanFinished(const QList<ContecBleFoundDevice> &devices);
    void onLinkReady();
    bool wantRecord(const ContecBle::RecordHeader &h);
    void onRecordCount(int count);
    void onRecordDownloaded(const ContecBle::Record &r);
    void onProgress(int done, int total);
    void onDownloadFinished();
    void onClockSet(bool ok);
    void afterClock();
    void onEraseFinished(bool ok);
    void onFailed(const QString &message);
    void finish(const QString &error);
    void stopDevice();
    void addTableRow(const Row &row);
    void updateTableRow(int index);
    QString outcomeText(const Row &row) const;
    QString summaryText(const QString &error) const;

    QLabel *m_stepLabels[StepCount] = {};
    QString m_stepTexts[StepCount];
    QListWidget *m_deviceList = nullptr;
    QPushButton *m_connectButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QCheckBox *m_syncClock = nullptr;
    QCheckBox *m_eraseAfter = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_retryButton = nullptr;
    QPushButton *m_doneButton = nullptr;

    ContecBleScanner *m_scanner = nullptr;
    QPointer<QtContecBleLink> m_link;
    QPointer<ContecBleDownloader> m_downloader;
    std::unique_ptr<ContecBleImporter> m_importer;
    QList<ContecBleFoundDevice> m_found;
    QList<Row> m_rows;

    QString m_deviceName;
    QString m_model;
    QDateTime m_downloadStarted;
    QDateTime m_lastRecordEnd;
    int m_headersOnDevice = 0;
    bool m_downloadCompleted = false;
    bool m_importedAny = false;
    bool m_busy = false;
    QString m_clockText;
    QString m_eraseText;
};

#endif // BLUETOOTHOXIMETERPAGE_H
```

- [ ] **Step 2: `bluetoothoximeterpage.cpp`**

```cpp
/* Bluetooth Oximeter Import Page
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "bluetoothoximeterpage.h"

#include "SleepLib/loader_plugins/contec_ble_loader.h"
#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace ContecBle;

BluetoothOximeterPage::BluetoothOximeterPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("bluetoothImportPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("#bluetoothImportPage { background-color: #f0f0f0; }"));

    m_stepTexts[StepScan] = tr("Searching for oximeters...");
    m_stepTexts[StepRead] = tr("Reading the record list...");
    m_stepTexts[StepDownload] = tr("Downloading new records...");
    m_stepTexts[StepSave] = tr("Saving to OSCAR...");

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("<h2>%1</h2>").arg(tr("Bluetooth import")), this));
    for (int i = 0; i < StepCount; ++i) {
        m_stepLabels[i] = new QLabel(this);
        layout->addWidget(m_stepLabels[i]);
    }

    m_deviceList = new QListWidget(this);
    m_connectButton = new QPushButton(tr("Connect"), this);
    layout->addWidget(m_deviceList);
    layout->addWidget(m_connectButton, 0, Qt::AlignLeft);

    m_progress = new QProgressBar(this);
    layout->addWidget(m_progress);

    m_syncClock = new QCheckBox(tr("Set the oximeter clock to this computer's time"), this);
    m_syncClock->setChecked(p_profile->oxi->syncOximeterClock());
    connect(m_syncClock, &QCheckBox::toggled, this, [](bool on) { p_profile->oxi->setSyncOximeterClock(on); });
    layout->addWidget(m_syncClock);

    m_eraseAfter = new QCheckBox(tr("Automatically erase the records on the oximeter after a successful import"), this);
    m_eraseAfter->setToolTip(tr("The oximeter can only erase all of its records at once. OSCAR erases only when "
                                "every record on it is safely in OSCAR and it isn't still recording."));
    m_eraseAfter->setChecked(p_profile->oxi->bleEraseAfterImport());
    connect(m_eraseAfter, &QCheckBox::toggled, this, [](bool on) { p_profile->oxi->setBleEraseAfterImport(on); });
    layout->addWidget(m_eraseAfter);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({ tr("Start"), tr("Length"), tr("Result") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_table, 1);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_retryButton = new QPushButton(tr("Retry"), this);
    m_doneButton = new QPushButton(tr("Done"), this);
    buttons->addWidget(m_retryButton);
    buttons->addWidget(m_doneButton);
    layout->addLayout(buttons);

    m_scanner = new ContecBleScanner(this);
    connect(m_scanner, &ContecBleScanner::finished, this, &BluetoothOximeterPage::onScanFinished);
    connect(m_scanner, &ContecBleScanner::failed, this, &BluetoothOximeterPage::onFailed);
    connect(m_connectButton, &QPushButton::clicked, this, [this]() {
        const int row = m_deviceList->currentRow();
        if (row >= 0 && row < m_found.size()) connectTo(m_found.at(row));
    });
    connect(m_retryButton, &QPushButton::clicked, this, &BluetoothOximeterPage::start);
    connect(m_doneButton, &QPushButton::clicked, this, [this]() { emit finished(m_importedAny); });
}

BluetoothOximeterPage::~BluetoothOximeterPage()
{
    m_busy = false;
    stopDevice();
}

void BluetoothOximeterPage::setStep(int step, bool allDone)
{
    for (int i = 0; i < StepCount; ++i) {
        const QString mark = (allDone || i < step) ? QString(QChar(0x2713))           // done
                           : (i == step ? QString(QChar(0x25B6)) : QStringLiteral(" "));
        m_stepLabels[i]->setText(mark + QStringLiteral("  ") + m_stepTexts[i]);
        m_stepLabels[i]->setEnabled(allDone || i <= step);
    }
}

void BluetoothOximeterPage::start()
{
    stopDevice();
    m_importer.reset();
    m_found.clear();
    m_rows.clear();
    m_table->setRowCount(0);
    m_deviceList->clear();
    m_deviceList->hide();
    m_connectButton->hide();
    m_progress->hide();
    m_retryButton->hide();
    m_doneButton->hide();
    m_headersOnDevice = 0;
    m_downloadCompleted = false;
    m_importedAny = false;
    m_lastRecordEnd = QDateTime();
    m_clockText.clear();
    m_eraseText.clear();
    m_stepTexts[StepScan] = tr("Searching for oximeters...");
    m_stepTexts[StepDownload] = tr("Downloading new records...");
    m_busy = true;
    setStep(StepScan);
    m_summary->setText(tr("Turn on Bluetooth in the oximeter's menu and close the Contec phone app first."));
    m_scanner->start(15000);
}

void BluetoothOximeterPage::onScanFinished(const QList<ContecBleFoundDevice> &devices)
{
    if (!m_busy) return;
    m_found = devices;
    if (devices.isEmpty()) {
        onFailed(tr("No oximeter found. Turn on Bluetooth in the oximeter's menu, close the Contec phone app and try again."));
        return;
    }
    if (devices.size() == 1) {
        connectTo(devices.first());
        return;
    }
    for (const ContecBleFoundDevice &d : devices) {
        m_deviceList->addItem(tr("%1 - %2 (signal %3 dBm)").arg(d.name, d.model).arg(d.rssi));
    }
    m_deviceList->setCurrentRow(0);
    m_deviceList->show();
    m_connectButton->show();
    m_summary->setText(tr("Several oximeters are nearby. Choose yours."));
}

void BluetoothOximeterPage::connectTo(const ContecBleFoundDevice &device)
{
    m_deviceList->hide();
    m_connectButton->hide();
    m_deviceName = device.name;
    m_model = device.model;
    m_stepTexts[StepScan] = tr("Connecting to %1 (%2)...").arg(device.name, device.model);
    setStep(StepScan);
    m_link = new QtContecBleLink(this);
    connect(m_link, &QtContecBleLink::ready, this, &BluetoothOximeterPage::onLinkReady);
    connect(m_link, &QtContecBleLink::failed, this, &BluetoothOximeterPage::onFailed);
    m_link->connectTo(device.info);
}

void BluetoothOximeterPage::onLinkReady()
{
    if (!m_busy) return;
    m_stepTexts[StepScan] = tr("Connected to %1 (%2)").arg(m_deviceName, m_model);
    setStep(StepRead);
    Machine *mach = p_profile->CreateMachine(ContecBleLoader::infoForModel(m_model));
    mach->setBrand(QObject::tr("Contec"));
    mach->setModel(m_model);
    m_importer.reset(new ContecBleImporter(mach));
    m_downloadStarted = QDateTime::currentDateTime();

    m_downloader = new ContecBleDownloader(this);
    m_downloader->setWantRecord([this](const RecordHeader &h) { return wantRecord(h); });
    connect(m_downloader, &ContecBleDownloader::recordCountKnown, this, &BluetoothOximeterPage::onRecordCount);
    connect(m_downloader, &ContecBleDownloader::recordDownloaded, this, &BluetoothOximeterPage::onRecordDownloaded);
    connect(m_downloader, &ContecBleDownloader::progress, this, &BluetoothOximeterPage::onProgress);
    connect(m_downloader, &ContecBleDownloader::downloadFinished, this, &BluetoothOximeterPage::onDownloadFinished);
    connect(m_downloader, &ContecBleDownloader::clockSet, this, &BluetoothOximeterPage::onClockSet);
    connect(m_downloader, &ContecBleDownloader::eraseFinished, this, &BluetoothOximeterPage::onEraseFinished);
    connect(m_downloader, &ContecBleDownloader::failed, this, &BluetoothOximeterPage::onFailed);
    connect(m_downloader, &ContecBleDownloader::deviceIdentified, this,
            [this](const QString &model, const QString &firmware, int) {
        m_stepTexts[StepScan] = tr("Connected to %1 (%2, firmware %3)").arg(m_deviceName, model, firmware);
        setStep(StepRead);
    });
    m_downloader->start(m_link, m_deviceName);
}

bool BluetoothOximeterPage::wantRecord(const RecordHeader &h)
{
    Row row;
    row.header = h;
    row.decision = m_importer->decide(h);
    switch (row.decision) {
    case Decision::Import:
    case Decision::ReplaceShorter:
        row.pending = true;
        break;
    case Decision::AlreadyPresent:
        row.outcome = Outcome::AlreadyPresent;
        break;
    case Decision::ConflictOtherOximeter:
        row.outcome = Outcome::ConflictOtherOximeter;
        row.otherDevice = m_importer->otherOximeterName(h);
        break;
    case Decision::InvalidStart:
        row.outcome = Outcome::InvalidStart;
        break;
    }
    const QDateTime start = h.start();
    if (start.isValid()) {
        const QDateTime end = start.addSecs(h.samples);
        if (!m_lastRecordEnd.isValid() || end > m_lastRecordEnd) m_lastRecordEnd = end;
    }
    m_rows.append(row);
    addTableRow(row);
    return row.pending;
}

void BluetoothOximeterPage::onRecordCount(int count)
{
    m_headersOnDevice = count;
    setStep(StepDownload);
    m_progress->setRange(0, qMax(count, 1));
    m_progress->setValue(0);
    m_progress->show();
}

void BluetoothOximeterPage::onRecordDownloaded(const Record &r)
{
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        Row &row = m_rows[i];
        if (!row.pending || row.header.l != r.header.l || row.header.m != r.header.m) continue;
        row.pending = false;
        row.outcome = m_importer->save(r, row.decision);
        if (row.outcome == Outcome::ConflictOtherOximeter) row.otherDevice = m_importer->otherOximeterName(r.header);
        if (row.outcome == Outcome::Imported || row.outcome == Outcome::Updated) m_importedAny = true;
        updateTableRow(i);
        return;
    }
}

void BluetoothOximeterPage::onProgress(int done, int total)
{
    m_progress->setValue(done);
    m_stepTexts[StepDownload] = tr("Downloading new records... (%1 of %2)").arg(done).arg(total);
    setStep(StepDownload);
}

void BluetoothOximeterPage::onDownloadFinished()
{
    if (!m_busy) return;
    setStep(StepSave);
    m_downloadCompleted = true;
    m_importer->finish();
    if (m_syncClock->isChecked()) m_downloader->setClock(QDateTime::currentDateTime());
    else afterClock();
}

void BluetoothOximeterPage::onClockSet(bool ok)
{
    m_clockText = ok ? tr("The oximeter clock was set to this computer's time.")
                     : tr("The oximeter didn't confirm the new clock time.");
    afterClock();
}

void BluetoothOximeterPage::afterClock()
{
    EraseInput in;
    in.eraseEnabled = m_eraseAfter->isChecked();
    in.downloadCompleted = m_downloadCompleted;
    for (const Row &row : m_rows) in.outcomes.append(row.outcome);
    in.headersOnDevice = m_headersOnDevice;
    in.lastRecordEnd = m_lastRecordEnd;
    in.downloadStarted = m_downloadStarted;
    switch (canErase(in)) {
    case EraseVerdict::Erase:
        m_downloader->allowDestructive(true);
        m_downloader->eraseAllRecords();
        return;
    case EraseVerdict::DownloadIncomplete:
        m_eraseText = tr("The oximeter was not erased because the download didn't finish.");
        break;
    case EraseVerdict::NotAllSaved:
        m_eraseText = tr("The oximeter was not erased because some of its records are not in OSCAR.");
        break;
    case EraseVerdict::StillRecording:
        m_eraseText = tr("The oximeter was not erased because it seems to be still recording.");
        break;
    case EraseVerdict::Disabled:
    case EraseVerdict::NothingToErase:
        break;
    }
    finish(QString());
}

void BluetoothOximeterPage::onEraseFinished(bool ok)
{
    m_eraseText = ok ? tr("All records were erased from the oximeter.")
                     : tr("The oximeter didn't confirm the erase, so its records may still be there.");
    finish(QString());
}

void BluetoothOximeterPage::onFailed(const QString &message)
{
    if (!m_busy) return;
    if (m_importer) m_importer->finish();     // keep what was saved
    finish(message);
}

void BluetoothOximeterPage::cancel()
{
    if (m_busy) onFailed(tr("The import was cancelled."));
}

void BluetoothOximeterPage::finish(const QString &error)
{
    m_busy = false;
    stopDevice();
    if (error.isEmpty()) setStep(StepCount, true);   // on an error the reached step stays marked
    m_progress->hide();
    m_summary->setText(summaryText(error));
    m_retryButton->setVisible(!error.isEmpty());
    m_doneButton->show();
}

void BluetoothOximeterPage::stopDevice()
{
    if (m_scanner) m_scanner->stop();
    if (m_downloader) {
        m_downloader->cancel();
        m_downloader->deleteLater();
        m_downloader = nullptr;
    }
    if (m_link) {
        m_link->disconnectFromDevice();
        m_link->deleteLater();
        m_link = nullptr;
    }
}

void BluetoothOximeterPage::addTableRow(const Row &row)
{
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    const QDateTime start = row.header.start();
    const QString when = start.isValid()
        ? QLocale().toString(start.date(), QLocale::ShortFormat) + QStringLiteral(" ") + start.time().toString(QStringLiteral("HH:mm:ss"))
        : tr("invalid");
    const int s = row.header.samples;
    m_table->setItem(r, 0, new QTableWidgetItem(when));
    m_table->setItem(r, 1, new QTableWidgetItem(QString::asprintf("%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60)));
    m_table->setItem(r, 2, new QTableWidgetItem(outcomeText(row)));
    m_table->scrollToBottom();
}

void BluetoothOximeterPage::updateTableRow(int index)
{
    if (QTableWidgetItem *item = m_table->item(index, 2)) item->setText(outcomeText(m_rows.at(index)));
}

QString BluetoothOximeterPage::outcomeText(const Row &row) const
{
    if (row.pending) return tr("Downloading...");
    switch (row.outcome) {
    case Outcome::Imported: return tr("Imported");
    case Outcome::Updated: return tr("Updated (was shorter)");
    case Outcome::AlreadyPresent: return tr("Already in OSCAR");
    case Outcome::ConflictOtherOximeter: return tr("Skipped: this night already has oximetry from %1").arg(row.otherDevice);
    case Outcome::InvalidStart: return tr("Skipped: invalid start time");
    case Outcome::NotDownloaded: return tr("Not downloaded");
    case Outcome::SaveFailed: return tr("Save failed");
    }
    return QString();
}

QString BluetoothOximeterPage::summaryText(const QString &error) const
{
    int imported = 0, updated = 0, present = 0, skipped = 0;
    for (const Row &row : m_rows) {
        switch (row.outcome) {
        case Outcome::Imported: ++imported; break;
        case Outcome::Updated: ++updated; break;
        case Outcome::AlreadyPresent: ++present; break;
        default: ++skipped; break;
        }
    }
    QStringList lines;
    if (!error.isEmpty()) lines << QStringLiteral("<b>%1</b>").arg(error.toHtmlEscaped());
    if (!m_rows.isEmpty()) {
        lines << tr("%1 imported, %2 updated, %3 already in OSCAR, %4 skipped.")
                     .arg(imported).arg(updated).arg(present).arg(skipped);
    } else if (error.isEmpty()) {
        lines << tr("The oximeter has no stored records.");
    }
    if (!m_clockText.isEmpty()) lines << m_clockText.toHtmlEscaped();
    if (!m_eraseText.isEmpty()) lines << m_eraseText.toHtmlEscaped();
    return lines.join(QStringLiteral("<br>"));
}
```

- [ ] **Step 3: `oscar.pro`** — в блок `qtHaveModule(bluetooth):!no_bluetooth` добавить `bluetoothoximeterpage.cpp` к `SOURCES` и `bluetoothoximeterpage.h` к `HEADERS`:
```
    SOURCES += SleepLib/loader_plugins/contec_ble_link.cpp bluetoothoximeterpage.cpp
    HEADERS += SleepLib/loader_plugins/contec_ble_link.h bluetoothoximeterpage.h
```

- [ ] **Step 4: Интеграция в мастер**

`oscar/oximeterimport.h`: непосредственно перед строкой `class OximeterImport : public QDialog` добавить `class BluetoothOximeterPage;`; в `private slots:` после `void on_chooseSessionButton_clicked();` добавить
```cpp
    void onBluetoothImportClicked();
    void onBluetoothFinished(bool importedSomething);
```
в `private:` после `QList<int> chosen_sessions;` добавить
```cpp
    BluetoothOximeterPage *m_btPage = nullptr;
```

`oscar/oximeterimport.cpp`: к include'ам добавить
```cpp
#ifdef HAVE_BLUETOOTH
#include "bluetoothoximeterpage.h"
#endif
```
в конце конструктора (после `ui->cms50SyncTime->setChecked(p_profile->oxi->syncOximeterClock());`) добавить
```cpp
#ifdef HAVE_BLUETOOTH
    m_btPage = new BluetoothOximeterPage(this);
    ui->stackedWidget->addWidget(m_btPage);
    connect(m_btPage, &BluetoothOximeterPage::finished, this, &OximeterImport::onBluetoothFinished);
    auto *btButton = new QPushButton(tr("Import over Bluetooth from a Contec oximeter (CMS50FW, CMS50D-BT, ...)"),
                                     ui->importSelectionPage);
    btButton->setMinimumHeight(ui->directImportButton->minimumHeight());
    btButton->setToolTip(tr("Turn on Bluetooth in the oximeter's menu and close the Contec phone app first."));
    ui->verticalLayout_6->insertWidget(ui->verticalLayout_6->indexOf(ui->directImportButton), btButton);
    connect(btButton, &QPushButton::clicked, this, &OximeterImport::onBluetoothImportClicked);
#endif
```
в `on_cancelButton_clicked()` первой строкой после `qDebug()` добавить
```cpp
#ifdef HAVE_BLUETOOTH
    if (m_btPage && m_btPage->isBusy()) m_btPage->cancel();
#endif
```
и в конец файла:
```cpp
void OximeterImport::onBluetoothImportClicked()
{
#ifdef HAVE_BLUETOOTH
    ui->stackedWidget->setCurrentWidget(m_btPage);
    ui->nextButton->setVisible(false);
    ui->retryButton->setVisible(false);
    m_btPage->start();
#endif
}

void OximeterImport::onBluetoothFinished(bool importedSomething)
{
    if (importedSomething) {
        mainwin->EnableTabs(true);
        mainwin->getDaily()->LoadDate(mainwin->getDaily()->getDate());
        mainwin->getOverview()->ReloadGraphs();
    }
    accept();
}
```
(Проверить `grep -n "verticalLayout_6" build/ui_oximeterimport.h` — член `Ui::OximeterImport::verticalLayout_6` существует.)

- [ ] **Step 5: Сборки и тесты**

Run:
```bash
cd /Users/semyk/Downloads/Oscar_Project/build && /opt/homebrew/bin/qmake ../oscar-sql/oscar/oscar.pro -spec macx-clang > qmake.log 2>&1 && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'
cd /Users/semyk/Downloads/Oscar_Project/build-nobt && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; echo "nobt done"
cd /Users/semyk/Downloads/Oscar_Project/build-test && /opt/homebrew/bin/qmake ../oscar-sql/oscar/oscar.pro -spec macx-clang CONFIG+=test CONFIG+=sdk_no_version_check > qmake.log 2>&1 && make -j$(sysctl -n hw.logicalcpu) 2>&1 | grep -E ' error:'; /Users/semyk/Downloads/Oscar_Project/tools/runtests.sh ContecBleImportTests ContecBleDownloaderTests ContecBleProtocolTests
/Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh bluetoothoximeterpage oximeterimport | comm -13 /Users/semyk/Downloads/Oscar_Project/tools/warn-baseline-ble.txt -
```
Expected: ошибок нет в трёх сборках; `0 unexpected`; новых предупреждений нет.

- [ ] **Step 6: Дымовой запуск** (без прибора: только открыть мастер)

Попросить пользователя закрыть открытые окна OSCAR, затем:
Run: `open -n /Users/semyk/Downloads/Oscar_Project/build/OSCAR20.app --args --datadir /Users/semyk/Documents/OSCAR20_Data_dev; sleep 10; pgrep -lf 'build/OSCAR20.app'`
Expected: процесс жив. Пользователь открывает File → Import → Oximetry → видит кнопку «Import over Bluetooth…».

- [ ] **Step 7: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/bluetoothoximeterpage.h oscar/bluetoothoximeterpage.cpp oscar/oximeterimport.h oscar/oximeterimport.cpp oscar/oscar.pro
git commit -m "Import from Contec oximeters over Bluetooth in the oximeter wizard

A new button on the wizard's first page opens a page that finds the
oximeter, downloads every new record as a session of its night, reports
what was imported, updated or skipped, optionally sets the oximeter clock
and, when enabled and safe, erases the oximeter. Both options are
remembered in the profile.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Ручная проверка с CMS50FW, финальная сборка, push

**Files:** без изменений кода (исправления — отдельными коммитами с повтором соответствующих проверок).

- [ ] **Step 1: Полная пересборка и тесты** (как в Task 7 Step 5). Expected: зелёное.

- [ ] **Step 2: Резервная копия записей прибора** — пользователь запускает `.venv/bin/python -m contec --prefix SpO202 download` в `ContecBTdiscover` (только чтение), чтобы иметь копию до опытов со стиранием.

- [ ] **Step 3: Чек-лист с пользователем** (приложение: `open -n …/build/OSCAR20.app --args --datadir /Users/semyk/Documents/OSCAR20_Data_dev`; приложение Contec на телефоне закрыто; Bluetooth прибора включён)

- [ ] «разрешение»: первый запуск импорта — системный запрос доступа к Bluetooth; при «Не разрешать» — сообщение про Privacy & Security, без падения; после разрешения — работает
- [ ] прибор с выключенным Bluetooth — «No oximeter found…», «Retry» работает после включения
- [ ] первый импорт: таблица записей, прогресс, «Imported»; ночи видны в «Дне» как оксиметрия «Contec CMS50FW»
- [ ] ночь, где уже есть MD300W1 → «Skipped: this night already has oximetry from …»
- [ ] повторный импорт сразу — всё «Already in OSCAR»
- [ ] «растущая запись»: импорт с надетым датчиком, автостирание включено → «not erased … still recording»; позже импорт → последняя запись «Updated (was shorter)»
- [ ] галочка часов: после импорта «clock was set»; время прибора на экране = время компьютера
- [ ] автостирание выключено → прибор не стирается; включено и все записи в OSCAR, датчик снят > 5 мин → «All records were erased»; `python -m contec list` показывает пустой прибор
- [ ] выбор галочек запоминается после перезапуска OSCAR
- [ ] «обрыв»: выключить прибор посреди выгрузки → сообщение об обрыве; уже сохранённые — «Imported», остальные — «Not downloaded»; стирания нет
- [ ] «отмена»: Cancel/закрытие мастера посреди выгрузки → без падения; повторный импорт работает
- [ ] «длинная ночь»: запись ≥ 6 ч выгружается полностью (длительность в таблице = на приборе)
- [ ] проводной импорт и импорт из файла по-прежнему работают (регресс `on_saveButton_clicked`)

- [ ] **Step 4: Итоговые предупреждения**

Run: `/Users/semyk/Downloads/Oscar_Project/tools/warncheck.sh contec_ble_protocol contec_ble_downloader contec_ble_import contec_ble_loader contec_ble_link oximetry_session_builder bluetoothoximeterpage oximeterimport serialoximeter main | comm -13 /Users/semyk/Downloads/Oscar_Project/tools/warn-baseline-ble.txt -`
Expected: пусто.

- [ ] **Step 5: Push**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql && git push origin feature-ble-oximeter-import && git log --oneline upstream/master..HEAD
```
Expected: 2 fork-only (спецификация, план) + 7 коммитов задач 1–7 (+ исправления).

- [ ] **Step 6: Подготовка к MR (не выполнять без согласия пользователя)**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git checkout -b feature-ble-oximeter-import-mr upstream/master
git cherry-pick $(git log --reverse --format=%h upstream/master..feature-ble-oximeter-import -- oscar Building)
```
В описании MR: протокол восстановлен для совместимости; шифрование (версия > 13) реализовано по приложению и не проверено на живом приборе.
