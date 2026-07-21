# MyPL — Next Steps

This document tracks the remaining roadmap for MyPL. Phases 1–10 are complete (see git history and the closed issues #1–#16 for details). The phases below address the known gaps that remain after Phase 10.

## Known Gaps (as of Phase 10 completion)

- The custom SQL engine has no indexes (`src/btree.c` is a stub), no `DROP TABLE`/`ALTER TABLE`, single-condition `WHERE`, three column types, and no views or constraints.
- There is no NULL semantics anywhere (`VAL_NULL` does not exist; SQLite NULLs map to `0`).
- Phase 9/10 features are first cuts: statement-level triggers on static SQL only, session-only sequences, simplified `dbms_sql`/`utl_file`, single-signature FFI, line-flag-only conditional compilation.
- Fixed ceilings everywhere (`STACK_MAX`, `MAX_LOCALS`, handle counts), full-recompile REPL, no package init for imported modules, non-overridable built-in packages, ref-count-only GC.
- No fuzzing, examples not covered by CI, no install target, README lags the feature set, POSIX-only.

## Phased Roadmap

### Phase 11 — SQL Engine Depth
- [ ] NULL semantics: `VAL_NULL`, `IS NULL` / `IS NOT NULL`, three-valued logic in `WHERE`, `COALESCE`/`NVL` natives, correct NULL mapping in both drivers
- [ ] `DROP TABLE` and `ALTER TABLE` (`ADD COLUMN`, `DROP COLUMN`)
- [ ] Rich `WHERE` for SELECT/UPDATE/DELETE: `AND`/`OR`/`NOT`, parentheses, `IN`, `LIKE`
- [ ] Real B-tree storage in `src/btree.c` plus `CREATE INDEX` / `DROP INDEX` and index-assisted lookups
- [ ] Column constraints: `PRIMARY KEY`, `UNIQUE`, `NOT NULL`, `DEFAULT`
- [ ] `CREATE VIEW` / `DROP VIEW` with view resolution in SELECT

### Phase 12 — Persistence & Runtime Completeness
- [ ] Persist sequences in the database catalog (survive process restarts)
- [ ] Persist triggers in the catalog, add `DROP TRIGGER`, and fire triggers on dynamic SQL (`execute_immediate`, `dbms_sql.execute`)
- [ ] Row-level triggers (`FOR EACH ROW`) with `:new` / `:old` row context
- [ ] Full `dbms_sql` cursor API: `open_cursor`, `parse`, `bind_variable`, `execute`, `fetch_rows`, `column_value`, `close_cursor`
- [ ] Initialize packages declared in imported modules
- [ ] Allow user packages to override/replace built-in packages
- [ ] `utl_file` expansion: append/seek/flush, larger handle table, directory objects
- [ ] `external_call` marshalling for float and string signatures
- [ ] CLI flag definitions for conditional compilation (e.g. `mypl -DDEBUG file.mypl`)

### Phase 13 — Hardening & Tooling
- [ ] Fuzzing harness for the lexer, parser, and conditional-compilation preprocessor (libFuzzer/AFL++)
- [ ] Run all `examples/*.mypl` as smoke tests in CI
- [ ] Makefile `install` target and a man page
- [ ] Rewrite README to document the full Phase 1–10 feature set
- [ ] Replace fixed ceilings (`STACK_MAX`, `MAX_LOCALS`, handle tables) with dynamic growth
- [ ] Incremental REPL compilation instead of full recompile per input
- [ ] Cycle detection or cycle-safe collection for reference-counted arrays/maps

## How to Use This Document

1. Pick the next uncompleted item from the lowest active phase.
2. Write a failing test in `tests/` before implementing.
3. Update this file to mark the item done when `make test` passes.
4. Do not jump ahead to advanced features without finishing the earlier phases first.

## References

- Oracle PL/SQL Language Reference (triggers, sequences, DBMS_SQL, UTL_FILE, conditional compilation)
- SQLite documentation (indexes, views, NULL semantics) for the backend behavior MyPL mirrors
