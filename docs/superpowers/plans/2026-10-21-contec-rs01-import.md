# Contec RS01 Import (stage 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

Fork-only, not for MR.

**Goal:** Import Contec RS01 recordings (nasal pressure, SpO2, pulse, pleth) from its USB disk into OSCAR as oximeter sessions, from a button in the oximetry wizard, never writing to the device.

**Architecture:** A pure reader (`rs01_reader`) turns .HD/.SEG bytes into per-signal runs of physical values; an importer (`rs01_import`) copies a recording folder into the profile's backup and builds an `MT_OXIMETER` session with SpO2, pulse, pleth and a new `OXI_NasalFlow` waveform; a wizard page (`rs01page`) finds the device or a folder, lists recordings and imports the ticked ones.

**Tech Stack:** C++17, Qt 6 (QStorageInfo, QFile), qmake; QtTest via `../tools/runtests.sh`.

**Spec:** `docs/superpowers/specs/2026-10-21-contec-rs01-import-design.md`; device format `/Users/semyk/Downloads/1111/docs/rs01-format.md`.

## Global Constraints

- Every rate, scale, unit, size from the .HD header; nothing hard-coded except the field offsets of §2.1.
- Record time = start + (segment − 1)·recordsPerSeg + (index − 1) s; a record without `FF FF` or with an index outside 1..recordsPerSeg is a missing second (a gap).
- SpO2 = 0 or PR = 0: no sample. Flow zero = the recording's median digital value; Pa by the header's scale; sign turned so inspiration is positive.
- New channel `OXI_NasalFlow` = 0x1806, WAVEFORM, MT_OXIMETER, SESSION, code "NasalFlow", name «Nasal flow» (ru «Носовой поток»), unit «Pa» (ru «Па»).
- Machine: MT_OXIMETER, brand "Contec", model "RS01", loader name "ContecRS01", serial "RS01"; session id = start time as seconds since the epoch (local time).
- Copies go to `<profile data folder>/Backup/RS01/<MMDDhhmm>/`; the device is opened read-only and nothing is created on it.
- Owner recordings (`~/RS01/`) are used locally only, never committed.
- New sources/tests registered in `oscar/oscar.pro`; re-run qmake in `../build` and `../build-test` after adding files with content. Translations: lupdate → fill → validate_ts TOTAL 0 → lrelease.

## Review Focus

1. A recording cut short (last segment partial, or a segment truncated mid-record) — reads up to the last whole record, no crash — test in Task 1.
2. A volume mounted writable on macOS — warned, still nothing written — test in Task 3 (logic function).
3. Re-import of a recording already in OSCAR — AlreadyThere, no duplicate session, no second copy — test in Task 2.
4. A folder with an .HD but no .SEG — a clear per-recording error, others imported — test in Task 2.
5. Device clock vs local time (DST) — start parsed as local time, session id stable across runs — test in Task 2.

---

### Task 1: Reader

**Files:** Create `oscar/SleepLib/loader_plugins/rs01_reader.{h,cpp}`, `oscar/tests/rs01readertests.{h,cpp}`; modify `oscar/oscar.pro`.

**Interfaces — Produces:**
```cpp
struct RS01Signal { QString label, unit; int samplesPerRecord = 0, bytesPerSample = 0;
                    double digMin = 0, digMax = 0, physMin = 0, physMax = 0; };
struct RS01Header { bool valid = false; QString error; QDateTime start;
                    int segHeaderBytes = 0, recordsPerSeg = 0, recordSeconds = 0, prefixBytes = 0, suffixBytes = 0;
                    QVector<RS01Signal> signals; int recordBytes() const; int indexOf(const QString &label) const; };
RS01Header rs01ParseHeader(const QByteArray &hd);
struct RS01Run { qint64 startMs = 0; double intervalMs = 0; QVector<float> samples; };
struct RS01Recording { bool valid = false; QString error; RS01Header header; QString name;
                       QHash<QString, QVector<RS01Run>> runs;   // by signal label: Flow, PR, SpO2, Pleth
                       int seconds = 0; int missingSeconds = 0; };
RS01Recording rs01ReadRecording(const QString &folder);
//! Recording folders (MMDDhhmm with an .HD inside) under \a root, by name.
QStringList rs01FindRecordings(const QString &root);
```
Tests build bytes with a helper `QByteArray makeHd(...)` / `makeSeg(...)` in the test file.

- [ ] Step 1: tests `testHeaderParsesExample` (doc §2.1 example: start 2026-10-17 01:50:53, 4 signals, Flow 50/2/Pa, Pleth 100, recordBytes 206), `testHeaderSixtyHzPleth`, `testHeaderInvalid` (short → error), `testSegmentRecords` (two records, flow/PR/SpO2/pleth values), `testMissingMarkerIsGap` (middle record without FF FF → two runs), `testIndexOutOfRangeIsGap`, `testSegmentsContinueInTime` (second segment starts at +600 s), `testMissingSegmentStops` (001, 003 → only 001 read), `testFlowZeroAndSign` (median 2008 → 0 Pa; a lower value → positive), `testNoSignalZeros` (SpO2 0 → no sample), `testTruncatedSegment` (last record cut → read up to it).
- [ ] Step 2: run `../tools/runtests.sh RS01ReaderTests` — build fails.
- [ ] Step 3: implement; register files in oscar.pro; qmake.
- [ ] Step 4: run the class and the suite — pass.
- [ ] Step 5: commit "RS01 reader: .HD header and .SEG records into signal runs".

### Task 2: Importer and channel

**Files:** Create `oscar/SleepLib/loader_plugins/rs01_import.{h,cpp}`, `oscar/tests/rs01importtests.{h,cpp}`; modify `oscar/SleepLib/schema.cpp` (+ header/extern for `OXI_NasalFlow`), `oscar/oscar.pro`.

**Interfaces — Consumes:** Task 1. **Produces:**
```cpp
class RS01Importer { public:
    enum class Result { Imported, AlreadyThere, Failed };
    QString backupFolder() const;                       // <profile data>/Backup/RS01
    bool alreadyImported(const QString &recordingFolder) const;
    Result import(const QString &recordingFolder, QString *error);   // copy, then parse the copy, build the session
    static SessionID sessionIdFor(const QDateTime &start);
};
```
- [ ] Step 1: tests (with a temporary profile as other importer tests do): `testImportBuildsSession` (four channels, first time = start, flow EventList rate 20 ms), `testImportTwiceAlreadyThere`, `testCopyLeavesSourceUntouched` (file list and mtimes of the source equal before/after; copy in Backup/RS01), `testHdWithoutSegmentsFails` (error text, no session), `testSessionIdStable` (same start → same id), `testNasalFlowChannel` (0x1806, MT_OXIMETER, unit).
- [ ] Step 2–4: red, implement (copy through `<name>.part` then rename; session via `p_profile->CreateMachine`, `AddEventList(EVL_Waveform, gain 1, rate)` per run, `really_set_first/last`, `mach->AddSession`, store as the Viatom loader does), green + suite.
- [ ] Step 5: commit "RS01 importer: copy the recording, then an oximeter session with nasal flow".

### Task 3: Wizard page and button

**Files:** Create `oscar/rs01page.{h,cpp}`; modify `oscar/oximeterimport.{h,cpp}`, `oscar/oscar.pro`, `oscar/uiglossary.cpp` (entries for new controls), translations; test in `oscar/tests/rs01importtests.cpp`.

**Interfaces — Produces:** `static QString RS01Page::writableWarning(const QString &mountPoint, const QString &device)` (empty when the volume is read-only is decided by the caller); `RS01Page::listRecordings(const QString &root)` filling a tree with checkable rows (new ticked, imported marked «уже в OSCAR»); signal `done()`.
- [ ] Step 1: tests `testWritableWarningText` (contains `diskutil unmount` and `diskutil mount readOnly <device>`), `testPageListsRecordings` (two synthetic recordings, one imported → unticked with the mark).
- [ ] Step 2–4: red, implement (find "SLEEP METER" via `QStorageInfo::mountedVolumes()`; «Выбрать папку…»; «Импортировать» with progress; per-recording error lines; the wizard button «Contec RS01 (USB)» opening the page like the O2Ring button), green + suite; UiCoverage passes.
- [ ] Step 5: translations; commit "RS01 page in the oximetry wizard".

Then: owner's night 10170150 imported locally (27 937 s, no gaps), final review (opus), fixes, owner check.
