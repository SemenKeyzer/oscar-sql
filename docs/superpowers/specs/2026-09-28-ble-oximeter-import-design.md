# Импорт с пульсоксиметра по Bluetooth (Contec CMS50…W) — дизайн

- Дата: 2026-09-28
- Ветка: `feature-ble-oximeter-import` (от `upstream/master`, независима от выравнивания времени)
- Статус: согласовано в обсуждении, ожидает ревью спецификации
- Исследование протокола: `/Users/semyk/Downloads/ContecBTdiscover` (Python-клиент `contec/`,
  описание `docs/protocol.md`, тесты `tests/`) — эталон для переноса на C++

> Этот файл — рабочая спецификация форка. В Merge Request в upstream он не включается.

## 1. Цель и контекст

### Задача
Сейчас OSCAR импортирует данные оксиметра по проводу (CMS50F v3.7, CMS50D+/E/F, ChoiceMMed),
из файла или записью «вживую». Беспроводные Contec (CMS50FW и родственные) по проводу
не подключаются, а фирменное приложение Contec после выгрузки **стирает архив на приборе
раньше, чем сохранит данные**, и перестало строить отчёты с 01.09.2026. Пользователь
восстановил BLE-протокол и проверил его на живом CMS50FW (прошивка 2.0.0).

Цель — добавить в мастер импорта оксиметра импорт **по Bluetooth**: одним действием
забрать с прибора все новые ночные записи в OSCAR, по желанию выставить часы прибора
и (опционально, с запоминанием) стереть записи на приборе после успешного импорта.

### Критерии успеха
1. CMS50FW пользователя импортируется одним проходом мастера: поиск → выгрузка → сохранение,
   без шага синхронизации времени.
2. Повторный импорт не создаёт дублей; запись, выросшая с прошлого импорта, заменяется.
3. Стирание никогда не происходит без галочки и без выполнения всех условий безопасности (§5).
4. OSCAR без модуля Qt Bluetooth собирается и работает как раньше (без кнопки).
5. Юнит-тесты протокола и сеанса проходят без прибора; сборка без предупреждений.

### Решения пользователя (из обсуждения)
- Объём v1: **выгрузка записей**, **стирание после импорта**, **шифрование (версия протокола > 13)**.
  Не входят: реальное время по BT, вариант K (CMS50K/K1), точечные измерения, непрерывные наборы.
- Что импортировать: **все новые записи сразу**; каждая запись прибора — отдельная сессия своей ночи.
- Подход: **отдельный BT-импорт** в том же мастере (не через `SerialOximeter`/Direct import).
- Стирание — **галочка «Автоматически стирать записи на оксиметре после импорта»**, по умолчанию
  выключена, выбор запоминается.
- Часы прибора выставляются по компьютеру, галочка по умолчанию включена (используем
  существующую настройку `syncOximeterClock`).

## 2. Протокол (выжимка из исследования; полная версия — `ContecBTdiscover/docs/protocol.md`)

- **GATT:** сервис `0000ff12-0000-1000-8000-00805f9b34fb`, запись в `0000ff01-…`
  (write-without-response, если свойство есть, иначе write), уведомления `0000ff02-…`
  (CCCD `0x2902` = `01 00`). Сопряжение не нужно. Писать порциями ≤ 20 байт (MTU 23).
  После подключения пауза 0,8 с до поиска сервисов.
- **Модели по имени в эфире** (самое длинное совпадение префикса): `SpO201` CMS50EW, `SpO202`
  CMS50FW, `SpO206` CMS50IW, `SpO208` CMS50D-BT, `SpO211` CMS50S, `SpO212` CMS60D1,
  `SpO213` CMS50S+ — вариант A; `SpO209` CMS50K, `SpO210` CMS50K1 — вариант K (не поддерживается).
- **Кадр:** байт 0 — заголовок ≥ 0x80, остальные < 0x80, последний — `sum(предыдущих) & 0x7F`.
  Длина определяется по заголовку (таблица `frame_len` в `protocol.py`); неизвестный байт
  пропускается. Кадры могут приходить разрезанными/склеенными — нужен потоковый «нарезчик».
  Многобайтные числа — little-endian по 7 бит.
- **Сеанс выгрузки (вариант A):**
  1. `81 01` → `F1` (строка ID, соль для ключей); `82 02` → `F2` (версия прошивки, версия
     протокола `ver = F2[6] & 0x7F`).
  2. Если `ver > 13` — обмен ключами: `F3` (22 байта, отправляется 18 + пауза 0,3 с + 4) →
     `83`; далее все команды оборачиваются в `F4`, входящие `84` расшифровываются и режутся
     на обычные кадры. KDF `getEncryptArray` + AES-128-CTR (весь 16-байтный IV — счётчик,
     как Java `AES/CTR/NoPadding`); XOR `0x56` поверх значений в DIFFERENCE/E1/EB.
  3. `9F 1F` → `EF` (есть данные, `mask1`, `mask2`); при тайм-ауте — `8F 04 00 13` → `FF`, повтор.
  4. Если `mask1 & 0x40`: `90 06 16` → `E0` (15 байт, `count`); `8E 06 14` → `FE 06` (форматы:
     бит 1 DIFFERENCE, 2 ORIGINAL, 4 CODE). Предпочтение: **ORIGINAL** (проверен на приборе),
     иначе CODE, иначе DIFFERENCE.
  5. Цикл: `9C 01 1D` → `EC` (21 байт: `last = [1]&0x40`, `hasPI = [1]&0x0F`, `L=[2]`, `M=[3]`,
     старт `20YY-MM-DD hh:mm:ss` из `[4..9]`, `N = lo7([10..13])`). `N == 0` — конец.
     Для каналов 1 (SpO2), 2 (пульс), 3 (PI, если есть): `9D fmt ch L M off…` → `ED fmt` пакеты
     (ORIGINAL — 21 отсчёт в 30-байтном пакете; DIFFERENCE — 27 в 24-байтном; CODE — поток
     с escape-парами), до накопления `N`. Проверка: контрольная сумма, `ED[2] == ch`,
     номер пакета по порядку. Сбой → `9D 02 ch L M 00 00` (прервать), пауза 0,5 с, сброс
     буфера, запрос с ожидаемого пакета (CODE — с начала); до 5 попыток подряд.
     После `last` — выход.
  6. Часы: `83 YY MM DD hh mi ss msL msH cs` → `F3`.
  7. Стирание всех записей: `9D 7F 7F 7F 7F 00 00 19` → `ED 7F …`, успех при `[5] == 0`.
     Выборочного стирания прибор не поддерживает.
- **Тайм-аут** на каждый ответ — 5 с. Ответ `F0 …` — прибор отверг команду.
- **Записи:** 1 отсчёт/с. Недостоверно: SpO2 = 127 (и 0 или > 100), пульс = 255 (и 0), PI = 255.
  Время старта — по часам прибора (локальное). Прибор режет ночь на несколько записей;
  последняя запись может расти, пока датчик на пальце, в том числе во время BLE-сеанса.

## 3. Поведение (UX)

Строки интерфейса — на английском через `tr()`; переводы — штатным механизмом `.ts`.

1. **Первая страница мастера** (`importSelectionPage`): новая кнопка
   «Import over Bluetooth from a Contec oximeter (CMS50FW, CMS50D-BT, …)» над существующими.
   Подсказка: «Turn on Bluetooth in the oximeter's menu and close the Contec phone app first.»
   Кнопка есть только в сборках с Bluetooth (`HAVE_BLUETOOTH`).
2. **Страница Bluetooth-импорта** (`bluetoothImportPage`) — этапы с отметками:
   поиск → чтение списка записей → выгрузка новых (прогресс: запись i из n, полоса) →
   сохранение. Если найдено несколько оксиметров — выбор из списка (имя, модель, сила сигнала).
   Не найдено за 15 с — подсказка и «Retry».
3. **Галочки** на странице (видны до начала и во время работы, применяются в конце):
   - «Set the oximeter clock to this computer's time» — `p_profile->oxi->syncOximeterClock()`
     (существующая настройка, по умолчанию true);
   - «Automatically erase the records on the oximeter after a successful import» —
     новая `p_profile->oxi->bleEraseAfterImport()` (по умолчанию false). Изменение сразу
     сохраняется в профиль.
4. **Итог:** таблица по записям прибора — старт, длительность, результат:
   `Imported` / `Already in OSCAR` / `Updated (was shorter)` /
   `Skipped: this night already has oximetry from <device>` / `Skipped: invalid start time` /
   `Not downloaded` (при прерывании). Строка итога: «N imported, M already in OSCAR, …»,
   затем строка о часах («Oximeter clock set» / ошибка) и о стирании («Oximeter erased» /
   «Oximeter not erased: <причина>» / ничего, если галочка выключена).
5. **Кнопки:** «Cancel» во время работы (аккуратно отключиться; сохранённое остаётся),
   «Done» по завершении (закрывает мастер; Daily/Overview обновляются так же, как после
   существующего импорта).

## 4. Архитектура

### 4.1 Компоненты

**(1) Протокол — `oscar/SleepLib/loader_plugins/contec_ble_protocol.{h,cpp}`**
(namespace `ContecBle`, без Qt Bluetooth, собирается всегда). Прямой перенос `protocol.py`:
- UUID-строки; `ModelInfo modelForName(const QString&)` (модель, вариант 'A'/'K').
- `quint8 checksum(const QByteArray&)`, `QByteArray frame(std::initializer_list<quint8>)`,
  `pack7/unpack7`, `quint32 lo7(...)`.
- `class FrameSplitter` (`QList<QByteArray> feed(const QByteArray&)`), `int frameLength(...)`.
- Команды: `cmdId()`, `cmdInfo()`, `cmdPrepare()`, `cmdStorage()`, `cmdCountRecords()`,
  `cmdFormats()`, `cmdNextHeader()`, `cmdChannel(fmt, ch, L, M, offset)`,
  `cmdChannelAbort(ch, L, M)`, `cmdSetTime(QDateTime)`, `cmdEraseAllRecords()`,
  `bool isDestructive(const QByteArray&)`.
- Разбор: `parseF1` (ID), `parseF2` (`DeviceInfo{firmware, protocolVersion}`),
  `parseEF` (`StorageStatus`), `parseE0Count`, `parseFE06` (форматы), `parseEC`
  (`RecordHeader{last, hasPI, L, M, start (QDateTime, invalid если поля невозможные), samples}`),
  `edPacketNo`, `edChecksumOk`, `parseEdOriginal`, `parseEdDifference(ver)`,
  `class CodeDecoder`, `pickFormat(supported)`.
- Шифрование: `kdf(seed16, s)`, `aesCtr(data, key, iv)` (Botan `AES-128` блочный шифр +
  собственный 128-битный big-endian счётчик — Botan CTR_BE в амальгамации не гарантирован),
  `appSeed(QDateTime)`, `buildF3(seed)`, `seedFrom83`, `deriveRx/deriveTx`, `buildF4(cmd, keys)`,
  `open84(frame, keys)`.

**(2) Сеанс — `oscar/SleepLib/loader_plugins/contec_ble_downloader.{h,cpp}`**
(без Qt Bluetooth, собирается всегда). `class ContecBleLink : QObject` — абстрактный канал:
`virtual void write(const QByteArray&) = 0`, сигналы `received(QByteArray)`, `linkLost()`.
`class ContecBleDownloader : QObject` — асинхронный автомат состояний на `QTimer`
(никаких блокирующих ожиданий):
- `start(ContecBleLink*, const QString& advertisedName)`; `cancel()`.
- Колбэк решения по записи: `std::function<bool(const ContecBle::RecordHeader&)> wantRecord`
  (мастер отвечает «скачивать/нет»).
- Сигналы: `deviceIdentified(model, firmware, protocolVersion)`, `recordCountKnown(int)`,
  `headerRead(RecordHeader)`, `recordDownloaded(ContecBle::Record)` (заголовок + векторы
  SpO2/пульс/PI), `progress(int done, int total)`, `downloadFinished()`,
  `clockSet(bool ok)`, `eraseFinished(bool ok)`, `failed(QString)`.
- После `downloadFinished` мастер вызывает `setClock(QDateTime)` и/или
  `eraseAllRecords()`; последнее работает, только если перед этим вызван
  `allowDestructive(true)`, иначе `failed`.
- Реализует §2: рукопожатие, ключи, статус, число, форматы, цикл заголовков и каналов с
  повторами, тайм-ауты 5 с, `F0` → ошибка, вариант K → ошибка «not supported yet».
- В лог (`qDebug`) — команды и заголовки, **без значений SpO2/пульса**.

**(3) Bluetooth-канал — `oscar/SleepLib/loader_plugins/contec_ble_link.{h,cpp}`**
(только `HAVE_BLUETOOTH`): `class ContecBleScanner` (`QBluetoothDeviceDiscoveryAgent`,
LE, имена с `modelForName() != null`, 15 с, сигналы `found(list)`/`error(QString)`),
`class QtContecBleLink : ContecBleLink` (`QLowEnergyController`: подключение, пауза 0,8 с,
`discoverServices`, сервис `ff12`, CCCD `ff02` = `01 00`, запись в `ff01` порциями ≤ 20 байт,
тип записи по свойствам; сигналы `ready()`, `failed(QString)`).

**(4) Загрузчик — `oscar/SleepLib/loader_plugins/contec_ble_loader.{h,cpp}`**:
`class ContecBleLoader : MachineLoader`, регистрируется как остальные (`Register()` в
`main`/месте регистрации оксиметрических загрузчиков), `loaderName() == "ContecBLE"`,
`Version()`, `MachineInfo newInfo(const QString& model)` (MT_OXIMETER, brand «Contec»,
model из таблицы, serial пустой — однотипное устройство на загрузчик).

**(5) Сохранение оксиметрии — `oscar/SleepLib/oximetry_session_builder.{h,cpp}`**
- `void addOximetryEvents(Session*, qint64 startMs, const QVector<OxiRecord>&, qint64 stepMs,
  bool havePerfIndex)` — перенос цикла из `OximeterImport::on_saveButton_clicked`
  (списки событий `OXI_Pulse`/`OXI_SPO2`/`OXI_Perf` с коэффициентом 0,01, разрывы на нулевых
  значениях, `setFirst/setLast`, итоги сессии). Плетизмограмма режима «Live» остаётся в
  `on_saveButton_clicked`; сам `on_saveButton_clicked` вызывает новую функцию без изменения
  поведения.
- `QDate Machine::dayForSession(qint64 firstMs) const` — вынесенное из `Machine::AddSession`
  правило выбора дня (сдвиг по времени разделения суток, объединение); `AddSession`
  использует его.
- Чистая функция решения по записи:
  `ImportDecision decideRecord(const RecordHeader&, const ExistingSession* sameStart,
  bool dayHasOtherOximeter)` → `Import` / `AlreadyPresent` / `ReplaceShorter` /
  `ConflictOtherOximeter` / `InvalidStart`.
- Чистая функция правила стирания `EraseVerdict canErase(const ImportSummary&, QDateTime
  downloadStarted)` — см. §5.

**(6) Мастер — `oscar/oximeterimport.{h,cpp,ui}`**: кнопка, страница, галочки, таблица,
связка сканер → канал → сеанс → решения → сохранение (каждая запись сохраняется сразу после
выгрузки) → часы → стирание → итог. Преобразование `Record` → `QVector<OxiRecord>`:
SpO2 127/0/>100 → 0; пульс 255/0 → 0; PI (единицы прибора — 0,1 %) 255/0 → 0, иначе
`perf = PI × 10` (OSCAR хранит перфузию в 0,01 %); `havePerfIndex = header.hasPI`
(CMS50FW PI не передаёт). Сессия: `sid = start.toUTC().toSecsSinceEpoch()`, шаг 1000 мс,
`new Session(mach, sid)`, `really_set_first(sid*1000)`, затем `addOximetryEvents`, `SetChanged(true)`,
`mach->AddSession`, сохранение как в существующем импорте.

**(7) Сборка и платформа**
- `oscar.pro`: `qtHaveModule(bluetooth):!no_bluetooth { QT += bluetooth; DEFINES += HAVE_BLUETOOTH;
  SOURCES/HEADERS += contec_ble_link.* }`; протокол, сеанс, загрузчик, builder — всегда.
  `CONFIG+=no_bluetooth` — проверка сборки без Bluetooth.
- `Building/MacOS/Info.plist.in`: `NSBluetoothAlwaysUsageDescription` =
  «OSCAR uses Bluetooth to import recordings from your pulse oximeter.»
- Новые настройки: `STR_OS_BleEraseAfterImport = "BleEraseAfterImport"` (false) в
  `OxiSettings` (`profiles.h`) с `bleEraseAfterImport()`/`setBleEraseAfterImport(bool)`.

### 4.2 Поток данных
```
Scanner ─found─► мастер (выбор) ─► QtContecBleLink ─ready─► Downloader.start()
Downloader ─headerRead─► мастер: decideRecord() ─► wantRecord = (Import | ReplaceShorter)
Downloader ─recordDownloaded─► мастер: new Session + addOximetryEvents() → (замена) → mach->AddSession() → Save
Downloader ─downloadFinished─► мастер: [часы] setClock ─clockSet─► [стирание] canErase()? → allowDestructive(true), eraseAllRecords() ─eraseFinished─► итог
```

## 5. Правила данных и безопасности

**Решение по записи** (для заголовка `EC` с корректным стартом `S` и длиной `N` с):
| Условие | Решение |
|---|---|
| старт невозможен (час ≥ 24 и т. п.) | `InvalidStart` — не скачивать |
| на ночь (`dayForSession(S)`) уже есть оксиметр **другого** устройства | `ConflictOtherOximeter` — не скачивать |
| у нашего устройства есть сессия с `sid == S` и её длительность ≥ `N` с | `AlreadyPresent` — не скачивать |
| есть сессия с `sid == S`, но короче `N` с | `ReplaceShorter` — скачать; после успешной выгрузки старую сессию удалить (`Session::Destroy` + убрать из машины/дня), новую добавить |
| иначе | `Import` |

**Автостирание** выполняется, только если **все** условия истинны:
1. `bleEraseAfterImport()` включена;
2. выгрузка завершилась без ошибок, отмены и разрыва (`downloadFinished` получен);
3. для **каждого** заголовка прибора результат ∈ {`Imported`, `Updated`, `AlreadyPresent`}
   и сохранение прошло успешно (нет `Conflict`, `InvalidStart`, `Not downloaded`, ошибок сохранения);
4. конец последней записи (`start + N с`) раньше момента начала выгрузки минус 5 минут
   (иначе прибор, вероятно, ещё пишет — стирание съело бы хвост);
5. прибор подтвердил стирание (`ED 7F`, `[5] == 0`); иначе в итоге «not erased: device error».
Иначе стирание пропускается с причиной в итоге. Сеанс физически не отправит
разрушительную команду без `allowDestructive(true)`. Порядок: выгрузка → сохранение →
часы → стирание.

## 6. Ошибки
| Ситуация | Поведение |
|---|---|
| Bluetooth выключен / нет разрешения macOS | Сообщение с путём «System Settings → Privacy & Security → Bluetooth», «Retry» |
| Прибор не найден за 15 с | Подсказка (включить BT в меню прибора, закрыть приложение Contec), «Retry» |
| Несколько приборов | Выбор из списка |
| Нет сервиса `ff12`, не удалось подключиться | Сообщение, «Retry» |
| Тайм-аут 5 с, 5 неудачных повторов пакета, разрыв | Остановка; уже сохранённое остаётся; оставшиеся — `Not downloaded`; стирания нет |
| Отмена | Аккуратное отключение; как выше |
| `F0` (команда отвергнута) | Ошибка с кодом |
| Вариант K | «CMS50K/K1 are not supported yet» |
| Ошибка сохранения в БД | Запись — «Save failed», стирание блокируется |

## 7. Тестирование
Автотесты (`qmake CONFIG+=test`, `oscar/tests`, `DECLARE_TEST`):
1. **Протокол:** checksum/frame; pack7/unpack7; frameLength и FrameSplitter (кадры,
   разорванные и склеенные между уведомлениями, мусорные байты); parseF1/F2/EF/E0/FE/EC
   (в т. ч. невозможный старт, переход через полночь); ORIGINAL/DIFFERENCE/CODE
   (включая escape-пары через границу пакетов); modelForName; isDestructive.
   Эталоны — перенос из `ContecBTdiscover/tests/test_protocol.py`.
2. **Шифрование:** kdf, aesCtr, buildF4/open84 на векторах `ContecBTdiscover/tests/data/vectors.txt`
   (сняты с Java-кода приложения; не персональные данные).
3. **Сеанс с поддельным прибором:** `FakeContecDevice : ContecBleLink` — симулятор варианта A
   на синтетических записях (версии 13 и 14): обычная выгрузка 2 записей; `wantRecord=false`
   для одной; потерянный/битый пакет → повтор с нужного номера; тайм-аут → `failed`;
   `F0` → `failed`; вариант K → `failed`; `eraseAllRecords` без `allowDestructive` → отказ
   без отправки; с разрешением → команда стирания и `eraseFinished(true)`; `setClock`.
4. **Решения и стирание:** `decideRecord` (все 5 исходов), `canErase` (каждое условие по отдельности).
5. **addOximetryEvents:** SpO2/пульс/PI из `OxiRecord` при шаге 1 с, разрывы на нулях, первая/последняя
   метки; `on_saveButton_clicked` проходит существующие сценарии (регресс — ручной).
6. **Сборка без Bluetooth:** `qmake CONFIG+=no_bluetooth` собирается, кнопки нет.

Ручная проверка — только на копии данных (`--datadir OSCAR20_Data_dev`), с реальным CMS50FW:
первый импорт; повторный (всё «Already in OSCAR»); импорт с надетым датчиком → позже
«Updated»; автостирание вкл./выкл. и каждое условие блокировки; Bluetooth выключен;
прибор не рекламируется; отмена во время выгрузки; ночь, где уже есть MD300W1 → Conflict.

**Приватность:** логи и записи пользователя не коммитятся; в тестах — только синтетика и
векторы из кода приложения.

## 8. Вне рамок (YAGNI)
- Реальное время по Bluetooth; вариант K; точечные измерения; непрерывные наборы; шаги/ЭКГ.
- Выборочное стирание (прибор не поддерживает).
- Анализ ODI/T90 в стиле Contec (у OSCAR свои расчёты).
- Склейка сегментов ночи в одну сессию (OSCAR и так показывает сессии дня вместе).

## 9. Замечания для MR
- Протокол восстановлен из приложения производителя для совместимости; OSCAR уже содержит
  загрузчики, полученные так же. Шифрование в v1 не проверено на живом приборе — в описании
  MR и в коде пометить как «implemented from the vendor app, not yet verified on a device
  with protocol version > 13».
- Коммиты: протокол → шифрование → сеанс → решения/builder (с переходом `on_saveButton_clicked`)
  → загрузчик/настройки → Bluetooth-канал/сборка/Info.plist → мастер.
