# MyPL Future Goals: PL/SQL Gap Analysis & Roadmap

> This document compares MyPL with Oracle PL/SQL and reframes every gap as a future goal. It is the single source of truth for what to build next.

## Where MyPL Stands Today

MyPL is a usable small-language prototype with these features already in place:

- **Program units:** `proc` procedures and `func` functions with typed parameters and a single return value.
- **Parameter modes:** `IN`, `OUT`, and `IN OUT` parameters for multi-value returns.
- **Core types:** `int`, `float`, `string`, `bool`, `array<T>`, `map<string,T>`, `row`, and user-defined `struct`.
- **Control flow:** `if`/`else`, `while`, `do ... while`, numeric `for`, `foreach`, `case`, `break`, `continue`, `return`.
- **Blocks:** Anonymous `declare ... begin ... end` blocks for ad-hoc scripting.
- **Embedded SQL:** DDL/DML/transaction statements, `?var` parameter binding, `SELECT ... INTO` (scalar and `array<row>`), and `for row in SELECT ...` iteration.
- **SQL feedback:** `sql_rowcount()`, `sql_found()`, `sql_notfound()` report the result of the last DML statement.
- **Dynamic SQL:** `execute_immediate(sql_string)` runs DDL/DML built at runtime and returns the affected-row count.
- **Exception handling:** `try { ... } catch (err) { ... }` with a reliable error-message variable.
- **Named exceptions:** predefined (`no_data_found`, `too_many_rows`) and user-defined exceptions, `raise exception_name;`, `raise_application_error(code, message)`, and `sqlcode`/`sqlerrm` inside catch blocks.
- **Explicit cursors:** `cursor` variables, `OPEN`, `FETCH`, `CLOSE`, and attributes `%FOUND`, `%NOTFOUND`, `%ROWCOUNT`, `%ISOPEN`.
- **Packages:** `package is` specs and `package body is` bodies with public/private members, package state, and sidecar persistence across runs.
- **Modules:** `import "path";` splits code across files and prevents circular/duplicate imports.
- **Standard library:** string, math, array, file I/O, utility, and SQL helper natives.
- **Backends:** built-in custom SQL engine or SQLite via `--db`/`.connect`.
- **REPL:** `.connect`, `.tables`, `.schema`, `.sql`, `.vars`, `.load`, `.defs`, `.history`, and multiline procedure definitions.

## PL/SQL Gaps Reframed as Future Goals

The table below lists where Oracle PL/SQL is still ahead, rewritten as positive next-step goals. Completed items are marked with a checkmark in the "Status" column.

| Area | MyPL Today | PL/SQL Capability | Future Goal | Status |
|------|-----------|-------------------|-------------|--------|
| **Functions** | `func` exists but is called like a procedure | `FUNCTION` callable from SQL expressions | Allow `func` in SQL `SELECT`, `WHERE`, and assignment expressions | ✅ Core `func` unit done |
| **Packages** | Spec/body with sidecar persistence | Full schema-stored packages, overloading, `AUTHID` | Persist package source and state in the database catalog; add `AUTHID CURRENT_USER`/`DEFINER` | ✅ Spec/body/state/authid done; package persistence via existing sidecar, top-level proc/func catalog added |
| **Parameter modes** | `IN`, `OUT`, `IN OUT` | Same set plus `NOCOPY` | Add `NOCOPY` hint for large parameters | ✅ IN/OUT/IN OUT done |
| **Cursors** | Explicit cursor variables | `REF CURSOR`, cursor variables as parameters, dynamic `OPEN FOR` | Add `REF CURSOR`-like cursor variables and `OPEN cursor FOR query` | ✅ Explicit cursors done; REF CURSOR next |
| **Collections** | `array<T>`, `map<string,T>` | Associative arrays, nested tables, `VARRAY`s, collection methods | Add associative arrays and standard collection methods (`EXTEND`, `TRIM`, `DELETE`, `FIRST`/`LAST`/`NEXT`/`PRIOR`) | ✅ Done |
| **Types** | Basic types and structs | `%TYPE`, `%ROWTYPE`, records, subtypes, `DATE`/`TIMESTAMP` | Add `%TYPE`/`%ROWTYPE`, a `date`/`timestamp` type, and user-defined subtypes | ✅ Done |
| **Control flow** | `case` statement/expression | Labeled blocks, `GOTO` | Add labeled blocks and `GOTO` for parity with legacy PL/SQL | ✅ `case` done; labeled blocks/GOTO next |
| **Blocks** | Anonymous blocks supported | Same | — | ✅ Done |
| **Exceptions** | Generic `try/catch` with message string | Named predefined/user-defined exceptions, `RAISE`, `RAISE_APPLICATION_ERROR`, `SQLCODE`/`SQLERRM` | Add named exceptions, `raise`, `raise_application_error`, and `sqlcode`/`sqlerrm` | ✅ Done |
| **Bulk binds** | Row-by-row loops | `BULK COLLECT INTO`, `FORALL` for set-based DML | Implement bulk-bind opcodes for performance | ✅ Done |
| **Triggers** | None | DML/DDL/system triggers, row/statement level, `BEFORE`/`AFTER`/`INSTEAD OF` | Add database triggers | 🔲 Not started |
| **Stored code** | Source files compiled at runtime | Schema-stored procedures/functions/packages compiled into the DB | Persist MyPL program units in the database catalog | ✅ Done |
| **Built-in packages** | Custom natives | `DBMS_OUTPUT`, `UTL_FILE`, `DBMS_SQL`, `DBMS_SCHEDULER`, etc. | Grow MyPL's standard library into named packages (`dbms_output`, `utl_file`, etc.) | 🔲 Not started |
| **Table functions** | None | Pipelined/table functions returning collections | Add table-valued/pipelined functions | 🔲 Not started |
| **Object types** | `struct` only | Object types with methods, inheritance, persistence | Extend `struct` toward object types with methods | 🔲 Not started |
| **Transactions** | `begin`/`commit`/`rollback` | Savepoints, autonomous transactions (`PRAGMA AUTONOMOUS_TRANSACTION`) | Add savepoints and autonomous transactions | ✅ Done |
| **Security** | None | `AUTHID CURRENT_USER` / `AUTHID DEFINER` | Add invoker/definer rights for stored units | ✅ Done |
| **Regex** | None | `REGEXP_LIKE`, `REGEXP_REPLACE`, etc. | Add regular-expression natives | 🔲 Not started |
| **Sequences** | None | `SEQUENCE` objects and `CURRVAL`/`NEXTVAL` | Add sequence support | 🔲 Not started |
| **Pragmas** | None | `PRAGMA AUTONOMOUS_TRANSACTION`, `PRAGMA EXCEPTION_INIT`, etc. | Add pragma support starting with autonomous transactions | ✅ Done |

## Phased Roadmap

### Phase 1 — Core PL/SQL Foundations ✅
- [x] SQL DML feedback: `sql_rowcount`, `sql_found`, `sql_notfound`
- [x] Dynamic SQL: `execute_immediate`
- [x] Structured exception handling: `try`/`catch` with reliable error message
- [x] `tests/test_phase1.c` and `examples/phase1.mypl`

### Phase 2 — Functions, Parameters, and Control Flow ✅
- [x] `func` keyword and callable functions
- [x] `OUT` and `IN OUT` parameter modes
- [x] `case` statement/expression
- [x] Anonymous blocks

### Phase 3 — Explicit Cursors ✅
- [x] Explicit cursor variables
- [x] `OPEN`, `FETCH`, `CLOSE`
- [x] Cursor attributes (`%FOUND`, `%NOTFOUND`, `%ROWCOUNT`, `%ISOPEN`)

### Phase 4 — Schema-Level Program Units ✅
- [x] `package is` / `package body is` syntax
- [x] Package state and sidecar persistence
- [x] `tests/test_phase4.c` and `examples/phase4.mypl`

### Phase 5 — Exception Model & Named Errors ✅
- [x] Named predefined exceptions
- [x] User-defined exceptions
- [x] `raise` and `raise_application_error`
- [x] `sqlcode` and `sqlerrm`

### Phase 6 — Collections & Bulk Binds ✅
- [x] Associative arrays
- [x] Collection methods: `EXTEND`, `TRIM`, `DELETE`, `FIRST`, `LAST`, `NEXT`, `PRIOR`
- [x] `BULK COLLECT INTO`
- [x] `FORALL`

### Phase 7 — Type System Enhancements ✅
- [x] `%TYPE` and `%ROWTYPE`
- [x] `date` / `timestamp` type and formatting natives
- [x] User-defined subtypes

### Phase 8 — Stored Code & Security ✅
- [x] Persist procedures/functions/packages in the database catalog
- [x] `AUTHID CURRENT_USER` / `AUTHID DEFINER`
- [x] Savepoints and autonomous transactions

### Phase 9 — Standard Library as Packages
- [ ] `dbms_output`
- [ ] `utl_file`
- [ ] `dbms_sql`
- [ ] Regular-expression natives
- [ ] Sequence support

### Phase 10 — Advanced Features
- [ ] DML/DDL triggers
- [ ] Table/pipelined functions
- [ ] Object types with methods
- [ ] Conditional compilation
- [ ] External procedure linkage

## How to Use This Document

1. Pick the next uncompleted item from the lowest active phase.
2. Write a failing test in `tests/` before implementing.
3. Update this file to mark the item done when `make test` passes.
4. Do not jump ahead to advanced features without finishing the earlier phases first.

## References

- [Oracle PL/SQL Language Reference](https://docs.oracle.com/en/database/oracle/oracle-database/23/lnpls/database-pl-sql-language-reference.pdf)
- [PL/SQL — Wikipedia](https://en.wikipedia.org/wiki/PL/SQL)
- MyPL `README.md` for current build/test instructions.
