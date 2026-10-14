# O2Ring Bluetooth import — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> Fork-only document, not for the upstream MR.

**Goal:** Download new night records from a Wellue/Viatom O2Ring over Bluetooth LE from the oximetry wizard, save them to the profile's backup folder and import them with the existing `ViatomLoader`.

**Architecture:** Four layers after the Contec pattern: pure protocol (`O2Ring::` frames, CRC-8, `FrameReader`, info/time/file-name payloads, file validation — a 1:1 port of `/Users/semyk/Downloads/1111/o2sync/protocol.py`), a byte-pipe `O2RingLink` with a QtBluetooth implementation and scanner, an asynchronous `O2RingDownloader` (QTimer, no blocking; port of `sync.py` `RingSession`), and `O2RingImporter` (backup file + `ViatomLoader::OpenFile`, "already in OSCAR"). A wizard page `O2RingPage` drives them; `OximeterImport` gets a button next to Contec's.

**Tech Stack:** Qt 6.11 (QtBluetooth LE), qmake, C++17 `-Werror`, QtTest; `HAVE_BLUETOOTH` gates the link, page and button (as Contec).

**Spec:** `docs/superpowers/specs/2026-10-15-o2ring-ble-import-design.md`

## Global Constraints

- UUIDs: service `14839ac4-7d7e-415c-9a42-167340cf2339`, write `8b00ace7-eb0b-49b0-bbe9-9aee0a26e1a3` (WriteWithoutResponse), notify `0734594a-a8e7-4b1a-a6b1-cd5243059a57`; device name prefix `"O2Ring "`.
- Request `AA cmd ~cmd seqLo seqHi lenLo lenHi payload crc8`; response head `0x55`, `byte1 == ~byte2`, len cap 8192; CRC-8 poly 0x07 init 0; vector `AA 14 EB 00 00 00 00` → `C6`.
- Commands: INFO 0x14, SYNC 0x16 (`{"SetTIME":"yyyy-MM-dd,HH:mm:ss"}` local, compact JSON), START 0x03 (name ASCII + 0x00 → uint32 LE size, valid 1..2 000 000), DATA 0x04 (seq = chunk index), END 0x05 (always sent, also after errors). Never 0x18.
- Response timeout 5000 ms, 4 attempts per command; a file is kept only if its byte count equals the announced size and `validateFile` passes (version 3 or 5, size field = length or (length−40)/2+40, (length−40) % 5 == 0, header start = file name).
- Backup path: `<profile data folder>/Backup/O2Ring_<SN>/<yyyyMMddHHmmss>`; write to `<name>.part` then rename (replacing).
- "Already in OSCAR": a machine `ViatomLoader` would use for that folder (serial = folder name `O2Ring_<SN>`) has `SessionExists(QDateTime::fromString(name, "yyyyMMddHHmmss").toSecsSinceEpoch())` (local time; matches `ViatomFile::m_sessionid`).
- Setting «Установить часы кольца»: `OxiSettings` `STR_OS_O2RingSetClock = "O2RingSetClock"`, default true, `o2ringSetClock()/setO2RingSetClock()`.
- No personal recordings in the repo: tests use synthetic files (port of `tests/conftest.py::make_file()`).
- Every new string translated (`validate_ts.py` TOTAL 0); explanations for new controls (glossary coverage passes).
- Build `../build-test`, `../build`, `../build-nobt` (exit codes); re-run qmake after adding files; `../tools/runtests.sh` green. App only via `open -n`.
- Commit trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. The ring disconnects (link lost) mid-file — no file written, END attempted, the records already downloaded stay imported, the page offers Retry.
2. A late duplicate DATA reply after a retry — must not end up as duplicated bytes in a saved file (exact size + validateFile reject it; the page reports the record as failed, not imported).
3. The phone app holds the ring — scan finds nothing / connect fails: clear message, no hang (scan and connect time out).
4. A record whose file name is not a valid timestamp or whose header disagrees — reported, skipped, others continue.
5. Cancel / closing the wizard while downloading — the downloader stops, the link disconnects, nothing half-written remains (`.part` removed).

---

### Task 1: Protocol

**Files:** Create `oscar/SleepLib/loader_plugins/o2ring_protocol.{h,cpp}`; tests `oscar/tests/o2ringprotocoltests.{h,cpp}`; add all to `oscar/oscar.pro` (protocol in the common SOURCES, tests in the test block).

**Interfaces — Produces (namespace `O2Ring`):**
- constants above; `quint8 crc8(const QByteArray &data);` `QByteArray buildRequest(quint8 cmd, const QByteArray &payload = {}, quint16 seq = 0);`
- `struct Frame { quint8 status; quint16 seq; QByteArray payload; };` `class FrameReader { QList<Frame> feed(const QByteArray &data); void reset(); };`
- `struct Info { QString serial, model, firmware, time, battery; QStringList files; };` `bool parseInfo(const QByteArray &payload, Info *out);` (JSON keys SN, Model, SoftwareVer, CurTIME, CurBAT, FileList; trailing NULs ignored; empty names dropped)
- `QByteArray timePayload(const QDateTime &local);` `QByteArray fileNamePayload(const QString &name);`
- `bool validateFile(const QString &name, const QByteArray &data, QString *error);`

- [ ] **Step 1: Failing tests** (`O2RingProtocolTests`), ported from `tests/test_protocol.py`: `testCrcVector` (crc of `AA 14 EB 00 00 00 00` == 0xC6; `buildRequest(0x14)` == `AA 14 EB 00 00 00 00 C6`); `testRequestWithPayloadAndSeq` (DATA seq 0x0102: bytes 3,4 = 02 01, len 0); `testReaderSplitFrames` (one response fed in 3 pieces → one Frame with payload and seq); `testReaderSkipsFalseStart` (garbage `55 00 00 …` before a valid frame, bad CRC frame → skipped); `testReaderLenCap` (len 9000 → not waited for); `testParseInfo` (`{"SN":"2682C06554","CurTIME":"2026-10-13,07:01:02","CurBAT":"65%","FileList":"20261012231500,20261013220000,"}\0\0` → 2 files, serial, battery); `testTimePayload` (`{"SetTIME":"2026-10-15,22:05:09"}`); `testValidateFile` (synthetic file valid; wrong name, odd record area, bad size → false with error).
- [ ] **Step 2:** qmake + build (exit 0) → FAIL (missing files).
- [ ] **Step 3:** implement (port of protocol.py).
- [ ] **Step 4:** suite green.
- [ ] **Step 5:** commit "O2Ring: the Bluetooth protocol (frames, CRC-8, info, file check)".

### Task 2: Downloader

**Files:** Create `oscar/SleepLib/loader_plugins/o2ring_downloader.{h,cpp}`; test `oscar/tests/o2ringdownloadertests.{h,cpp}` with `oscar/tests/fakeo2ring.{h,cpp}` (port of `tests/test_sync.py` FakeRing: answers INFO/SYNC/START/DATA/END from in-memory files, can drop the n-th answer, send a late duplicate, cut the link, report a wrong size).

**Interfaces — Consumes:** Task 1. **Produces:**
- `class O2RingLink : public QObject { virtual void write(const QByteArray &) = 0; signals: void received(const QByteArray &); void linkLost(); };`
- `class O2RingDownloader : public QObject` — `void start(O2RingLink *link);` (sends INFO; emits `infoReceived(const O2Ring::Info &)`), `void setClock(const QDateTime &local);` (→ `clockSet(bool ok)`), `void download(const QStringList &names);` (per name START/DATA…/END; emits `fileProgress(const QString &name, qint64 done, qint64 total)`, `fileDownloaded(const QString &name, const QByteArray &data)`, `fileFailed(const QString &name, const QString &error)`; then `finished()`), `void cancel();`, `setResponseTimeout(int ms)` (default 5000), `setAttempts(int)` (default 4); `failed(const QString &message)` when the link is lost outside a file or INFO fails. Before each request the reader is reset (stale frames dropped).

- [ ] **Step 1: Failing tests:** `testDownloadTwoFiles` (both arrive intact, END after each); `testRetryAfterLostAnswer` (DATA answer 1 dropped → retried, file intact); `testLateDuplicate` (duplicate of a DATA answer arrives after the retry → file still exactly equal to the source, or `fileFailed` — never a wrong file emitted); `testLinkLostMidFile` (`fileFailed`, END attempted, earlier file already emitted); `testWrongSize` (START says 100, data gives 95 then empty → `fileFailed`); `testSetClock` (SYNC payload JSON as Task 1, `clockSet(true)`); `testInfoTimeout` (no answers → after 4 × timeout `failed`). Use timeouts of 50 ms in tests.
- [ ] **Step 2:** build → FAIL.
- [ ] **Step 3:** implement (QTimer state machine, port of `RingSession`).
- [ ] **Step 4:** suite green.
- [ ] **Step 5:** commit "O2Ring: the downloader (commands, chunks, retries)".

### Task 3: Importer and the setting

**Files:** Create `oscar/SleepLib/loader_plugins/o2ring_import.{h,cpp}`; modify `oscar/SleepLib/profiles.h` (setting); test `oscar/tests/o2ringimporttests.{h,cpp}` (profile fixture like `ContecBleImportTests`).

**Interfaces — Produces:**
- `class O2RingImporter` — `explicit O2RingImporter(const QString &serial);` `QString folder() const;` (`<data>/Backup/O2Ring_<serial>`), `bool alreadyImported(const QString &name) const;`, `enum class Result { Imported, AlreadyThere, Failed };` `Result importFile(const QString &name, const QByteArray &data, QString *error);` (validate → write `.part` → rename → `ViatomLoader().OpenFile(path)`), `static SessionID sessionIdFor(const QString &name);`.

- [ ] **Step 1: Failing tests:** `testImportSavesAndParses` (synthetic 30-min file → `Imported`, file at `folder()/name`, a session exists for machine serial `O2Ring_<SN>` with id `sessionIdFor(name)`, no `.part` left); `testAlreadyImported` (after it, `alreadyImported(name)` true; importing again → `AlreadyThere`); `testInvalidFileNotSaved` (bad header → `Failed`, nothing in the folder); `testSetClockSettingDefault` (`o2ringSetClock()` true by default).
- [ ] **Step 2:** build → FAIL. **Step 3:** implement. **Step 4:** green. **Step 5:** commit "O2Ring: saving and importing downloaded records".

### Task 4: Link, page, wizard button

**Files:** Create `oscar/SleepLib/loader_plugins/o2ring_link.{h,cpp}` (`O2RingScanner`, `QtO2RingLink` — mirror `ContecBleScanner`/`QtContecBleLink`: scan 15 s for names starting "O2Ring ", connect timeout 15 s, enable notifications, WriteWithoutResponse), `oscar/o2ringpage.{h,cpp}`; modify `oscar/oximeterimport.{h,cpp}` (button `o2ringImportButton` «O2Ring (Bluetooth)» under the Contec one, page in the stack, Done/Retry like Contec), `oscar/uiglossary.cpp`, `Translations/Russkiy.ru.ts`. All under `HAVE_BLUETOOTH`.
**Test:** `oscar/tests/o2ringpagetests.{h,cpp}` with the fake ring from Task 2 injected through `O2RingPage::startWithLink(O2RingLink *, const QString &name)` (a test seam that skips scanning).

**Interfaces — Consumes:** Tasks 1–3.

- [ ] **Step 1: Failing tests:** `testRecordsListed` (fake ring with 2 files, one already imported → table shows 2 rows, the imported one unchecked and marked "already in OSCAR", the new one checked); `testDownloadImportsChecked` (press Download → only the checked one downloaded and imported; summary "1 imported"); `testSetClockFollowsSetting` (checkbox on → SYNC sent before download; off → not sent; the checkbox writes the setting); `testCancelRemovesPart` (cancel mid-file → no `.part`, downloader stopped).
- [ ] **Step 2:** build → FAIL. **Step 3:** implement; page texts per spec §3 (hint «Закройте приложение ViHealth на телефоне — кольцо держит одно подключение; наденьте кольцо, чтобы оно проснулось»; unsupported ring «Это кольцо пока не поддерживается» when INFO fails with a non-V1 answer or the service is missing). **Step 4:** lupdate + Russian, `validate_ts.py` TOTAL 0, lrelease; glossary entries for the button and page controls; three builds; suite green. **Step 5:** commit "O2Ring: the wizard page and the import button".

### Task 5: On the ring

- [ ] `open -n ../build/OSCAR20.app --args --datadir /Users/semyk/Documents/OSCAR20_Data_dev --profile Папа`; owner: File → Oximetry import → O2Ring (Bluetooth), phone app closed; checks list, download, Daily. Fix what the device shows (each fix with a test).
- [ ] Handoff note (`git add -f`), memory; commit.
