# MyPL — Next Steps

This document tracks the remaining roadmap for MyPL. Phases 1–11 are complete (see git history and the closed issues #1–#22 for details). The phases below address the known gaps that remain after Phase 11.

## Known Gaps (as of Phase 13 completion)

- The custom SQL engine still has only three column types; views cannot be JOIN targets; B-tree deletion does not rebalance (indexes are rebuilt wholesale on row-chain rewrites); index keys use only the first 36 bytes of strings.
- The custom engine ignores `?var` bind params for static SQL (`tests/test_phase6.c` has 4 pre-existing failures in `USE_SQLITE=0` builds); the `dbms_sql` cursor API substitutes `?N` at execute time instead.
- `utl_file` lines are still limited to 1 KB; `external_call` takes a single int/float/string argument; REPL recompiles everything per input; reference counting has no cycle collection.
- Custom-engine programs cannot loop over a table they create in the same run (columns are checked against the catalog at compile time), so several examples need `--db`; reported error line numbers include the prepended built-in package source; no install target, README lags the feature set, POSIX-only.

## Phased Roadmap

### Phase 11 — SQL Engine Depth
- [x] NULL semantics: `VAL_NULL`, `IS NULL` / `IS NOT NULL`, three-valued logic in `WHERE`, `COALESCE`/`NVL` natives, correct NULL mapping in both drivers
- [x] `DROP TABLE` and `ALTER TABLE` (`ADD COLUMN`, `DROP COLUMN`)
- [x] Rich `WHERE` for SELECT/UPDATE/DELETE: `AND`/`OR`/`NOT`, parentheses, `IN`, `LIKE`
- [x] Real B-tree storage in `src/btree.c` plus `CREATE INDEX` / `DROP INDEX` and index-assisted lookups
- [x] Column constraints: `PRIMARY KEY`, `UNIQUE`, `NOT NULL`, `DEFAULT`
- [x] `CREATE VIEW` / `DROP VIEW` with view resolution in SELECT

### Phase 12 — Persistence & Runtime Completeness
- [x] Persist sequences in the database catalog (survive process restarts)
- [x] Persist triggers in the catalog, add `DROP TRIGGER`, and fire triggers on dynamic SQL (`execute_immediate`, `dbms_sql.execute`)
- [x] Row-level triggers (`FOR EACH ROW`) with `:new` / `:old` row context
- [x] Full `dbms_sql` cursor API: `open_cursor`, `parse`, `bind_variable`, `execute`, `fetch_rows`, `column_value`, `close_cursor`
- [x] Initialize packages declared in imported modules
- [x] Allow user packages to override/replace built-in packages (a package declared in the program replaces a stored or built-in package of the same name)
- [x] `utl_file` expansion: append/seek/flush, larger handle table, directory operations
- [x] `external_call` marshalling for float and string signatures (`external_call_float` / `external_call_string`; int, float or string argument)
- [x] CLI flag definitions for conditional compilation (e.g. `mypl -DDEBUG file.mypl`)

### Phase 13 — Hardening & Tooling
- [x] Fuzzing harness for the lexer, parser, and conditional-compilation preprocessor (libFuzzer: `make fuzz` / `make fuzz-run`, seeds and regressions replayed by `make test`, `Fuzz` CI workflow)
- [x] Run all `examples/*.mypl` as smoke tests in CI (`make examples-test`, part of `make test`; per-example `// smoke:` directives in `tests/run_examples.sh`)
- [x] Makefile `install` target and a man page (PREFIX/DESTDIR, `mypl.1`)
- [x] Rewrite README to document the full feature set through Phase 12
- [x] Replace fixed ceilings (`STACK_MAX`, `MAX_LOCALS`, handle tables) with dynamic growth (value stack, frame arrays, locals tables grow to a 2^20 cap)
- [x] Incremental REPL compilation instead of full recompile per input (persistent chunk/compiler; fragments compiled once)
- [x] Cycle detection or cycle-safe collection for reference-counted arrays/maps (tracing collector over containers, adaptive threshold)

## How to Use This Document

1. Pick the next uncompleted item from the lowest active phase.
2. Write a failing test in `tests/` before implementing.
3. Update this file to mark the item done when `make test` passes.
4. Do not jump ahead to advanced features without finishing the earlier phases first.

## References

- Oracle PL/SQL Language Reference (triggers, sequences, DBMS_SQL, UTL_FILE, conditional compilation)
- SQLite documentation (indexes, views, NULL semantics) for the backend behavior MyPL mirrors
