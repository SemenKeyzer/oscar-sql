# OSCAR Notes — index

Developer and user notes for OSCAR 2. Documents fall into three kinds:

- **Reference** — describes the code as it is now; keep it in sync when the code changes.
- **Design / plan** — how a feature was designed; describes the code *at the time it was written*.
- **Review / investigation** — a dated snapshot of findings; not updated afterwards.

When a reference document and the code disagree, the code wins — please fix the document.

## Start here

- [Getting Started with OSCAR 2.0.md](Getting%20Started%20with%20OSCAR%202.0.md) — for users moving from OSCAR 1.x.
- [ARCHITECTURE.md](Developer%20Notes/ARCHITECTURE.md) — how the pieces fit together (startup, data model, import, UI, database, time corrections).
- [DATABASE_SCHEMA.md](Database/DATABASE_SCHEMA.md) — every table, with the schema version history.
- Building: `Building/Linux/BUILD_Linux.md`, `Building/Windows/BUILD-WIN-Qt6.md`, `Building/MacOS/BUILD-mac.md` (unit tests are described in the Linux guide).

## Reference

### Database

- [DATABASE_SCHEMA.md](Database/DATABASE_SCHEMA.md) — Schema reference and version history (the main one).
- [DATABASE_SCHEMA_REFERENCE.md](Database/DATABASE_SCHEMA_REFERENCE.md) — Same content, kept for the wiki; `DATABASE_SCHEMA_REFERENCE.mw` is its MediaWiki form. Update all three together.
- [DATA_DICTIONARY.md](Database/DATA_DICTIONARY.md) — Column-by-column dictionary.
- [DATABASE_INDEXES_AND_FOREIGN_KEYS.md](Database/DATABASE_INDEXES_AND_FOREIGN_KEYS.md) — Indexes and foreign keys.
- [SCHEMA_DESIGN_PHILOSOPHY.md](Database/SCHEMA_DESIGN_PHILOSOPHY.md) — Why the schema looks the way it does; BLOB storage and compression.
- [HOW_TO_USE_QUERIES.md](Database/HOW_TO_USE_QUERIES.md) — Running the example queries (`USEFUL_QUERIES.sql`, `QUERY_RECENT_SESSION_SETTINGS.sql`).
- [DATABASE_CORRUPTION_AND_RECOVERY.md](Database/DATABASE_CORRUPTION_AND_RECOVERY.md) — Corruption risks and recovery.
- [QSETTINGS_REGISTRY_KEYS.md](Database/QSETTINGS_REGISTRY_KEYS.md) — QSettings / registry keys.
- [OSCAR_Data Directory Contents.md](Database/OSCAR_Data%20Directory%20Contents.md) — What lives in the data folder.

### Code and algorithms

- [ARCHITECTURE.md](Developer%20Notes/ARCHITECTURE.md) — Architecture guide.
- [SessionChannelData_Calculation_Explanation.md](Developer%20Notes/SessionChannelData_Calculation_Explanation.md) — Where per-session numbers are computed.
- [STEADY_BREATHING_ALGORITHM.md](Developer%20Notes/STEADY_BREATHING_ALGORITHM.md) — Steady-breathing detection.
- [SLEEP_ANALYSIS.md](Developer%20Notes/SLEEP_ANALYSIS.md) — OSCAR's own sleep analysis ("second opinion"): pipeline, channels, storage, algorithms, parameters.
- [SIGNALS_AND_SLOTS_GUIDE.md](Developer%20Notes/SIGNALS_AND_SLOTS_GUIDE.md) — Qt signals/slots as used in the database manager.
- [PERFORMANCE_INSTRUMENTATION_GUIDE.md](Developer%20Notes/PERFORMANCE_INSTRUMENTATION_GUIDE.md) — Timing instrumentation.
- [Reducing log noise.md](Developer%20Notes/Reducing%20log%20noise.md) — Logging conventions.
- [MULTIPLE_INSTANCES.md](Developer%20Notes/MULTIPLE_INSTANCES.md) — Running several OSCAR instances (`.mw` = wiki form).
- [BACKUP_RESTORE_README.md](Developer%20Notes/BACKUP_RESTORE_README.md) — Backup/restore feature index.
- [SHARING_DESIGN.md](Developer%20Notes/SHARING_DESIGN.md) — Profile sharing: design and implementation.
- [URI_SCHEME_RESTORE.md](Developer%20Notes/URI_SCHEME_RESTORE.md) — Deep link for restoring a shared profile.

### OSCAR 1.x file formats (read only by the profile importer)

- [.001 Events File Format Documentation-revised.md](Developer%20Notes/.001%20Events%20File%20Format%20Documentation-revised.md) — `.001` events format (supersedes the shorter non-revised file).
- [sessions.info file use.md](Developer%20Notes/sessions.info%20file%20use.md) — `Sessions.info`.
- [shg file use.md](Developer%20Notes/shg%20file%20use.md) — `.shg` graph layouts (now in `graph_layouts`).
- [rxchanges.cache file use.md](Developer%20Notes/rxchanges.cache%20file%20use.md) — `RXChanges.cache`.

### Device loaders

- [LOADER_DETECTION_PATTERNS.md](loaders/LOADER_DETECTION_PATTERNS.md) — how each loader recognises its card.
- [SD_CARD_FINGERPRINTS.md](loaders/SD_CARD_FINGERPRINTS.md) — real-world card samples.
- File formats: `loaders/G3X/` (BMC G3X), `loaders/Luna/` (BMC legacy), `loaders/Apex/`, `loaders/SEFAM_*_CARD_ANALYSIS.md`, `loaders/AEONMED_AS100_CARD_ANALYSIS.md` (no loader yet).

### Getting data out

- [OSCAR Data - Python Library for CPAP Data Access.md](Accessing%20OSCAR%20Data/OSCAR%20Data%20-%20Python%20Library%20for%20CPAP%20Data%20Access.md) — Python access to the database.
- [Reproducing_1.7.1_CSV_Exports.md](Accessing%20OSCAR%20Data/Reproducing_1.7.1_CSV_Exports.md) — Reproducing OSCAR 1.7.1 CSV exports with SQL.
- [python_waveform_demo_spec.md](Accessing%20OSCAR%20Data/python_waveform_demo_spec.md) — Waveform demo (identical copy in `Waveform Demo/`).

### Project housekeeping

- `Git/` — branch update, cherry-picking from upstream, merging an orphan branch.
- `Maintenance/` — yearly copyright update, bug import script.
- `Wiki/` — backup of the ApneaBoard wiki pages; see `Wiki/UPDATING_WIKI_BACKUP.md`.
- `AI Aids/` — notes on working with AI assistants on this code base.
- `OSCAR Usage/Profile Restore Constraints.md` — user-facing restore limits.

## Designs and plans (implemented; describe the code when written)

- [](specs/) — Recent feature designs and plans (purge range of days, combine similar machines, CSV report fields, OH/CH split, OH/CH capability gating).
- [2026-09-15-oh-ch-capability-gating.md](plans/2026-09-15-oh-ch-capability-gating.md) — OH/CH capability gating plan (schema v19).
- [DATABASE_MIGRATION_STRATEGY.md](Developer%20Notes/DATABASE_MIGRATION_STRATEGY.md) — Original move from files to SQLite; see also `DATABASE_MIGRATION_PROGRESS.md`, `DATABASE_MIGRATION_COMPLETE.md` (schema v6 era).
- [DATABASE_INTEGRATION_GUIDE.md](Developer%20Notes/DATABASE_INTEGRATION_GUIDE.md) — Early database integration with XML fallback — historical; XML is no longer used.
- [DATABASE_LOADER_COMPATIBILITY.md](Developer%20Notes/DATABASE_LOADER_COMPATIBILITY.md) — Loader compatibility during the migration.
- [LOADER_CALL_ANALYSIS.md](Developer%20Notes/LOADER_CALL_ANALYSIS.md) — Loader external calls during the migration.
- [JOURNAL_DATABASE_MIGRATION_DESIGN.md](Developer%20Notes/JOURNAL_DATABASE_MIGRATION_DESIGN.md) — Journal moved into the database.
- [AUTOMATIC_PROFILE_MIGRATION_IMPLEMENTATION.md](Developer%20Notes/AUTOMATIC_PROFILE_MIGRATION_IMPLEMENTATION.md) — First-run migration from OSCAR 1.x.
- [BACKUP_RESTORE_IMPLEMENTATION_PLAN.md](Developer%20Notes/BACKUP_RESTORE_IMPLEMENTATION_PLAN.md) — Backup/restore plan.
- [DATABASE_MENU_DESIGN.md](Developer%20Notes/DATABASE_MENU_DESIGN.md) — File/Database menu (`DATABASE_MENU_DESIGN_v1.md` is the earlier discussion).
- [REPORT_TREE_UI_REDESIGN.md](Developer%20Notes/REPORT_TREE_UI_REDESIGN.md) — Report tree UI.
- [Clock Drift Plan.md](Developer%20Notes/Clock%20Drift%20Plan.md) — Clock drift / session alignment port from 1.7.1.
- [daily_summaries_per_machine_redesign.md](Developer%20Notes/daily_summaries_per_machine_redesign.md) — Dropping the per-machine dimension of `daily_summaries` (v16).
- [PERCENTILE_INTERPOLATION_DESIGN.md](Developer%20Notes/PERCENTILE_INTERPOLATION_DESIGN.md) — Percentile interpolation.
- [2026-06-10-g3x-evt-only-import.md](G3X/2026-06-10-g3x-evt-only-import.md) — BMC G3X EVT-only import plan.
- [SEFAM_LOADER_DESIGN.md](loaders/SEFAM_LOADER_DESIGN.md) — SEFAM loader design (plan: `SEFAM_LOADER_PLAN.md`).
- [APEX_LOADER_DESIGN.md](loaders/Apex/APEX_LOADER_DESIGN.md) — Apex loader design (tests: `APEX_TESTING.md`).

## Reviews and investigations (dated snapshots)

- [BUG_FIXES.md](Developer%20Notes/BUG_FIXES.md) — Running bug-fix log.
- [Code Review — Commit b5adca94.md](Developer%20Notes/Code%20Review%20%E2%80%94%20Commit%20b5adca94.md) — Review of one commit.
- [TIME_ALIGNMENT_CODE_REVIEW.md](Developer%20Notes/TIME_ALIGNMENT_CODE_REVIEW.md) — Time alignment review (second opinion: `TIME_ALIGNMENT_CODE_REVIEW_CODEX.md`).
- [OPENPROFILE_MEMORY_PERFORMANCE_REVIEW.md](Developer%20Notes/OPENPROFILE_MEMORY_PERFORMANCE_REVIEW.md) — Profile open memory/performance (second opinion: `-codex` variant).
- [LEAK_PERCENTILE_METHOD_REVIEW_2026-04-16.md](Developer%20Notes/LEAK_PERCENTILE_METHOD_REVIEW_2026-04-16.md) — Leak percentile method.
- [RESMED_LEAK95_INVESTIGATION_SUMMARY_2026-04-16.md](Developer%20Notes/RESMED_LEAK95_INVESTIGATION_SUMMARY_2026-04-16.md) — ResMed Leak95 investigation.
- [percentile_findiings.md](Developer%20Notes/percentile_findiings.md) — Weighted percentile review.
- [profile_import_assessment.md](Developer%20Notes/profile_import_assessment.md) — Profile import review.
- [analyze_001_diagnostic_notes.md](Developer%20Notes/analyze_001_diagnostic_notes.md) — `.001` diagnostics.
- [ResMed_IE_Time_Trace.md](Developer%20Notes/ResMed_IE_Time_Trace.md) — ResMed Ti/Te trace from loader to Daily page.
- [Eliminate DST zone.md](Developer%20Notes/Eliminate%20DST%20zone.md) — Removing the DST zone field.
- [OSCAR_DATE_FIX.md](Developer%20Notes/OSCAR_DATE_FIX.md) — Date handling fix.
- [QCOLOR_RGB_CONSTRUCTOR_CHANGES.md](Developer%20Notes/QCOLOR_RGB_CONSTRUCTOR_CHANGES.md) — QColor constructor conversion.
- [QT_COMBOBOX_SIGNAL_CHANGE.md](Developer%20Notes/QT_COMBOBOX_SIGNAL_CHANGE.md) — QComboBox signal change.
- [APEX_CODE_REVIEW.md](loaders/Apex/APEX_CODE_REVIEW.md) — Apex loader review.
- [SEFAM_ANALYZE_VS_OSCAR.md](loaders/SEFAM_ANALYZE_VS_OSCAR.md) — Sefam Analyze vs OSCAR.
- [leak_field_analysis.md](loaders/G3X/leak_field_analysis.md) — BMC G3X leak field.

## Small personal notes

`Developer Notes/Getting updates from Brian.md`, `Loaders sampled for learning to date.md`, `StructuredJournal.txt`, `time differences labels.md`, `Using wikimedia for help.md`, `Data Migration Process.md`.
