# Oximetry artefacts: detect, show, exclude, correct — design

Fork-only, not for MR. Approved in chat 2026-10-16 ("да отлично", with manual correction).

## Goal

OSCAR's analysis already drops some bad SpO2 silently (out-of-range values, 10-point spikes that
come back within 5 s, fragments shorter than the minimum, a short median filter). The doctor and the
owner cannot see what was dropped, and motion artefacts that look like desaturations are still
counted in ODI, T90 and the hypoxic burden. This change:

1. detects more kinds of oximeter artefacts automatically;
2. shows every excluded stretch on the Daily graphs and in the analysis panel, with its reason;
3. leaves those stretches out of the oximetry analysis (ODI, time below thresholds, hypoxic burden,
   problem zones, pulse figures);
4. lets the user correct it: «Артефакт» (the scoring menu's existing Exclude stretch) also excludes
   oximetry, and a new «Не артефакт» stretch cancels automatic artefacts inside it.

Success: on a night with probe trouble the panel shows "excluded as artefact: N min", the stretches are
visible and listed, the figures no longer count them, and a wrong automatic call can be undone in two
clicks.

## Detection rules (on the 1 Hz SpO2 and pulse grids, before the median filter)

Each rule yields spans with a reason. Overlapping spans merge (reasons kept).

| # | Reason (shown) | Rule |
|---|----------------|------|
| R1 | out of range | SpO2 outside 50–100 (existing `keepRange`), pulse outside 30–220 |
| R2 | spike | jump of >= 10 points within 2 s that comes back within 3 points within 5 s (existing `removeSpikes`) |
| R3 | too fast a fall | SpO2 falls by >= 4 points within <= 2 s and comes back to within 2 points of the level before the fall within 30 s; the span runs from the fall to the return. A fall that does not come back within 30 s is left alone (could be real). |
| R4 | pulse disturbed | a SpO2 fall of >= 3 points whose start is within 5 s of a pulse jump of >= 30 bpm within 2 s, or of a gap in the pulse; the span is the fall to its recovery (as the desaturation detector finds it). A pulse rise after a real apnea is slower (10–20 bpm over several seconds) and does not trigger it. |
| R5 | frozen signal | SpO2 and pulse both exactly constant for >= 3 min (a living pulse never is); the whole constant stretch. Constant SpO2 with a varying pulse is normal and not flagged. |
| R6 | probe on / off | at the start and the end of each recording: the stretch before the first (after the last) 60 s in which SpO2 varies by <= 3 points, at most 5 min. |
| R7 | short fragment | stretches shorter than the minimum segment (existing `dropShortSegments`) |

Constants live next to the existing ones in `oxi_analyzer.cpp`; they are not user settings (YAGNI).

## Exclusion

- Excluded seconds are set to "no data" in the SpO2 (and, for R1/R5/R6, pulse) grid before the
  existing median filter and detectors run. So ODI, desaturations, the burden, the SpO2 histogram,
  T90, problem zones and the recorded time (`spo2Seconds`, the denominator) all leave them out.
- Manual «Артефакт» (Kind::Exclude) stretches of the day are passed to the analysis and excluded the
  same way (reason "marked by hand").
- New manual kind «Не артефакт» (Kind::Keep, stored as "keep"; `manual_scoring.kind` is TEXT, so no
  schema migration): automatic spans R3–R6 inside it are dropped. R1/R2/R7 stay (they are not
  physiological values at all).
- The day's manual Exclude/Keep stretches enter `dayInputsHash`, so a correction recalculates the
  night. `kAnalysisAlgoVersion` 10.

## Showing it

- New analysis channel `AN_OxiArtifact` (SPAN, next free code in 0x1A00–0x1A3F), label «aAR»,
  name «Артефакт оксиметра · анализ», grey; drawn on the «Флаги · анализ» graph and as a light grey
  band on the SpO2 graph, like other analysis spans.
- Analysis panel, Oximetry block: «Исключено как артефакт: N мин (M отрезков)», hover help listing
  minutes per reason. Hidden when nothing was excluded.
- «Анализ OSCAR» tab: a group «Артефакты оксиметра» with each span (time, length, reason); clicking
  jumps the graphs there, like the other groups. Its context menu (and the flow/SpO2 graph's scoring
  menu on an artefact) offers «Не артефакт», which adds a Keep stretch over the span.
- The scoring menu over the SpO2 graph keeps «Артефакт» (X); a Keep stretch shows in the manual list
  as «Не артефакт» and can be removed like an Exclude.
- Glossary entry `oxi_artifact`: what OSCAR treats as an artefact, the rules in plain words, how to
  undo a wrong one.

## Not in scope

Statistics / Overview totals of excluded minutes; user-tunable thresholds; artefacts in the flow
signal (the flow analysis has its own unscoreable time).

## Testing

Synthetic grids (`tests/analysis_synth.h`), one test per rule and its guard:
- R3: 5-point fall in 1 s back in 10 s → excluded; the same fall over 15 s → a desaturation.
- R4: fall with a 35 bpm pulse jump in 1 s → excluded; fall with a 15 bpm rise over 8 s → kept.
- R5: SpO2 and pulse constant 4 min → excluded; SpO2 constant 10 min with varying pulse → kept.
- R6: noisy first 90 s then stable → first stretch excluded, at most 5 min.
- Exclusion: an excluded desaturation is not in ODI or the burden; `spo2Seconds` drops by the span.
- Manual: an Exclude stretch removes a desaturation; a Keep stretch restores an R3 one but not an R2
  spike; the inputs hash changes with either.
- Panel: the excluded line and the tab group appear only when something was excluded.

## Risks

- False positives remove real events: guards per rule above, the panel line makes it visible, and
  «Не артефакт» undoes it.
- Recalculation of every analysed night after the version bump (already the norm for version bumps).
