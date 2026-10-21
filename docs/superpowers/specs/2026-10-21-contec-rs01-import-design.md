# Contec RS01 import, stage 1: records and graphs — design

Fork-only, not for MR. Approved in chat 2026-10-21 (stage split, oximeter wizard button, clock later).
Device format: `/Users/semyk/Downloads/1111/docs/rs01-format.md` (verified on the owner's device by
the O2Rig session) — the authority for every byte below. Stage 2 (OSCAR's analysis of the nasal flow)
and stage 3 (setting the device clock over HID) get their own specs.

## Goal

The Contec RS01 sleep screener records nasal-cannula pressure, SpO2, pulse and a photo-plethysmogram
onto its own USB disk ("SLEEP METER"). Stage 1 brings its recordings into OSCAR: one oximetry session
per recording with SpO2, pulse, pleth and a new «Носовой поток» waveform, shown on the Daily page and
analysed by OSCAR's existing oximetry analysis, imported from a button in the oximetry wizard,
without ever writing to the device.

Success: on the owner's copy of 10170150 (7.8 h) the night shows on the Daily page with the four
graphs, the oximetry figures (ODI, T90, hypoxic burden) and the start time 2026-10-17 01:50:53;
re-importing changes nothing; the device's disk is never written to.

## Reading a recording (parser, no Qt widgets)

`SleepLib/loader_plugins/rs01_reader.{h,cpp}`, pure functions on bytes, unit-tested on synthetic
files:

- `RS01Header parseHeader(const QByteArray &hd)` — the 506-byte .HD, fixed-width ASCII fields as in
  the format doc §2.1: start time (yyyyMMddHHmmss, device local clock), SEG header size, records
  per SEG, record seconds, prefix/suffix bytes, and per signal: label, samples per record, bytes per
  sample, unit, digital min/max, physical min/max. Nothing hard-coded: a header with Pleth at 60 Hz
  reads as 60 Hz. Invalid (short, ns = 0, a non-number where a number must be, record size that does
  not add up) → `valid = false` with a reason.
- `RS01Segment parseSegment(const QByteArray &seg, const RS01Header &h)` — checks the ASCII segment
  number, then reads `(size − header) / recordBytes` records; each record: `FF FF`, u16 LE index
  (1..records per SEG), then each signal's samples in header order (u16 LE or u8 by bytes per
  sample). A record without `FF FF`, or with an index out of range, is a missing second.
- `RS01Recording readRecording(const QString &folder)` — `MMDDhhmm.HD` and `001.SEG…` in number
  order; a missing segment number stops the reading there (later segments would land at the wrong
  time). Time of a record = start + (segment − 1)·records per SEG + (index − 1) seconds.
- Values: digital → physical by the header's linear scale. PR = 0 and SpO2 = 0 (with the pulse 0)
  are "no signal": no sample. Flow: the zero is the recording's median digital value (≈2006–2008,
  not 2048), then scaled to Pa; the sign is turned so that **inspiration is positive**, as OSCAR's
  flow graphs and analysis expect (the device has inspiration = value going down).
- Output: per signal a list of contiguous runs `{start ms, interval ms, samples}`; a missing second
  ends a run (a gap, not invented values).

## Into OSCAR

- `RS01Importer` (`SleepLib/loader_plugins/rs01_import.{h,cpp}`), like `O2RingImporter`:
  - machine: `MT_OXIMETER`, brand «Contec», model «RS01», loader name `ContecRS01`, serial «RS01»
    (the header holds no serial);
  - session id: the start time (seconds since the epoch, device local clock read as local time);
    an existing session with that id = already imported (re-import does nothing);
  - channels: `OXI_SPO2`, `OXI_Pulse`, `OXI_Plethy` (waveforms at the header's rates) and a new
    `OXI_NasalFlow` (0x1806, WAVEFORM, MT_OXIMETER, «Носовой поток», unit «Па», code
    "NasalFlow"); one EventList per contiguous run;
  - the oximetry analysis (stage 1 of OSCAR's analysis) runs on the new session as on any oximeter's.
- Copy first: before parsing, each chosen recording folder is copied from the device into
  `<profile data folder>/Backup/RS01/<MMDDhhmm>/` (through temporary names, then renamed); parsing
  reads only the copy. A folder already copied and complete is not copied again.
- Never write to the device: the importer opens the device's files read-only and creates nothing on
  it. On macOS, when the "SLEEP METER" volume is mounted writable (`QStorageInfo::isReadOnly()` false),
  the page warns: «Прибор подключён с правом записи — macOS может испортить его файловую систему.
  Отключите его и подключите только для чтения:» with the two `diskutil` commands for its device
  node, and a «Продолжить всё равно» choice (OSCAR itself still writes nothing).

## The wizard

- `OximeterImport`: a button «Contec RS01 (USB)» next to «O2Ring (Bluetooth)» (with its own picture,
  like the O2Ring button), opening `RS01Page`.
- `RS01Page` (`oscar/rs01page.{h,cpp}`):
  - finds the device: a mounted volume labelled "SLEEP METER" (`QStorageInfo::mountedVolumes()`);
    else «Выбрать папку…» for a copy of the records (any folder holding `MMDDhhmm/MMDDhhmm.HD`);
  - lists the recordings: start date and time, length (from the segment sizes), «уже в OSCAR» for
    those imported; new ones ticked, as on the O2Ring page;
  - «Импортировать»: copies and imports the ticked ones with a progress line, then «Готово» shows the
    last imported night like the other imports; per recording a clear error line when it fails
    (bad header, no segments), the rest go on.

## Not in scope (stages 2, 3)

Apnea/hypopnea scoring of the nasal flow and a screening AHI (stage 2); setting the device clock over
HID (stage 3); the unknown header fields (ids 110/160, the 32 binary bytes); erasing the device.

## Testing

Synthetic .HD/.SEG built in the tests (no owner data in the repo):
- header: the doc's example parses to its values; a 60 Hz pleth header reads 60; short/invalid → reason.
- segment: records with and without `FF FF`, an index out of range, a short last segment.
- recording: two segments → times continue across them; a missing segment number stops there;
  the flow zero from the median, inspiration positive; SpO2/PR 0 → no sample; gaps split runs.
- importer: a session with the four channels and the start time; importing twice → AlreadyThere;
  the copy lands in Backup/RS01 and the source folder is unchanged (no new files, same mtimes).
- page: lists two synthetic recordings, the imported one marked; the read-write warning text for a
  writable volume (logic in a testable function).
Local only, not committed: the owner's `~/RS01/10170150` imports with 27 937 s and no gaps.

## Risks

- The device's clock is its own: until stage 3 its nights may need «Выровнять» against CPAP.
- Large nights: 7.8 h = 1.4 M flow samples + 2.8 M pleth samples per night — read segment by segment.
