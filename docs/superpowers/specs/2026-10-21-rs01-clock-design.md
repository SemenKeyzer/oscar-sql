# Contec RS01, stage 3: setting the device's clock over USB HID — design

Fork-only, not for MR. Approved in chat 2026-10-21: hidapi built into OSCAR (option 1); the clock set
when the RS01 page imports from a connected device, plus a «Set clock» button. Stages 1–2:
`docs/superpowers/specs/2026-10-21-contec-rs01-import-design.md`,
`docs/superpowers/specs/2026-10-21-rs01-nasal-flow-analysis-design.md`. Protocol: the RS01 format notes
(O2Rig), §4 — verified on the owner's device.

## Goal

A recording's start time comes from the RS01's own clock (there is no command to read it). A clock that
has drifted puts the night beside the CPAP with an offset, and the day analysis then decides wrongly which
events were under the mask. OSCAR sets the clock from the computer's local time whenever the device is
connected to the RS01 page, and on request.

Success: with the device plugged in, the RS01 page says «Device clock set: 21.10.2026 22:15:03»; the next
recording's start time (its .HD) matches the computer's clock within a few seconds.

## Protocol (only what is verified; nothing else is ever sent)

- USB HID, VID `0x28E9`, PID `0x01F3`; writes are 65 bytes (report ID `0x00` + 64), replies 64 bytes,
  zero padded.
- Checksum `CK = ((cmd & 0x7F) + Σ params) & 0x7F`.
- Identify: `81 01` → reply starting `F1`, containing `RS01` (protocol A). Anything else: not set.
- Set clock: `83 YY MM DD hh mm ss 00 00 CK`, YY = year − 2000, local time → reply `F3 00 73`.
- Never sent: `84 …` (unknown), `8B …` «erase» (effect unknown), protocol B.
- HID does not touch the device's disk (its FAT breaks on host writes — stage 1); stage 1's read-only rule
  for the disk stays.

## Components

- `SleepLib/loader_plugins/rs01_clock.{h,cpp}` — pure, tested: `QByteArray rs01ClockCommand(const
  QDateTime &local)` (the 64-byte report body), `rs01Checksum`, `bool rs01IsIdentifyReply(const QByteArray &)`,
  `bool rs01IsClockReply(const QByteArray &)`; years outside 2000–2127 refused.
- `SleepLib/loader_plugins/rs01_hid.{h,cpp}` — the transport: `RS01Hid::setClock(const QDateTime &local,
  QString *error) → bool`: open the first 28E9:01F3 device, identify, set, check the reply, close; each
  reply waited for at most 1 s. Errors in words for the page: device not found over USB («switch it on
  before plugging the cable»), could not open, not an RS01, no reply / wrong reply. Built on hidapi.
- `3rdparty/hidapi/` — hidapi 0.15.0 (libusb/hidapi, BSD-style licence, compatible with GPL 3): `hidapi.h`,
  `mac/hid.c` (IOKit, CoreFoundation), `windows/hid.c` (+ `hidapi_*.h`; links `setupapi`, already linked,
  and `hid`). Source taken from the official GitHub release tarball (download asked of the owner first).
  Only macOS and Windows; on Linux `RS01Hid` is a stub that says it is not available there and the
  button is hidden.
- `rs01page.{h,cpp}`: when the list comes from the device's own disk (SLEEP METER found), the page sets the
  clock once, before listing, and shows the result on a line of its own; a «Set clock» button sets it again
  (also when only the clock is wanted). A failure never blocks the import. Runs on the GUI thread with a
  wait cursor (two short exchanges, ≤ 2 s).

## Testing

- Unit (`rs01clocktests`): the command for 2026-10-21 22:15:03 → `83 1A 0A 15 16 0F 03 00 00 CK` with CK as
  above, zero padded to 64; checksum of the verified reply `F3 00` → `73`; identify reply with «RS01»
  accepted, `82…`/other refused; clock reply `F3 00 73` accepted, anything else refused; year 1999 / 2128
  refused.
- Page: the clock line's texts for success and each error (a function returning the text).
- On the device (owner, by hand): set the clock, make a short recording, import it; its start time ≈ the
  computer's. No automated test talks to the device.

## Risks

- macOS may ask for permission to access the device («Input Monitoring») if it sees the HID interface as
  an input device; the error text says so. The O2Rig peer set the clock from macOS through hidapi without
  that.
- The clock is set to local time; after a DST change the next set corrects it.
