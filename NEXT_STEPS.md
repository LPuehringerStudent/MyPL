# MyPL Next Steps: From Prototype to PL/SQL Alternative

> This document compares MyPL with Oracle PL/SQL and reframes every gap as a future goal. It replaces the open-ended "Roadmap" section in `README.md` with concrete, prioritized next steps.

## Where MyPL Stands Today

MyPL is already a usable small-language prototype:

- **Program units:** `proc` procedures with typed parameters and a single return value.
- **Core types:** `int`, `float`, `string`, `bool`, `array<T>`, `map<string,T>`, `row`, and user-defined `struct`.
- **Control flow:** `if`/`else`, `while`, `do ... while`, numeric `for`, `foreach`, `break`, `continue`, `return`.
- **Embedded SQL:** DDL/DML/transaction statements, `?var` parameter binding, `SELECT ... INTO` (scalar and `array<row>`), and `for row in SELECT ...` iteration.
- **SQL feedback:** `sql_rowcount()`, `sql_found()`, `sql_notfound()` report the result of the last DML statement.
- **Dynamic SQL:** `execute_immediate(sql_string)` runs DDL/DML built at runtime and returns the affected-row count.
- **Exception handling:** `try { ... } catch (err) { ... }` is being stabilized so the catch variable reliably holds the error message.
- **Modules:** `import "path";` splits code across files and prevents circular/duplicate imports.
- **Standard library:** string, math, array, file I/O, utility, and SQL helper natives.
- **Backends:** built-in custom SQL engine or SQLite via `--db`/`.connect`.
- **REPL:** `.connect`, `.tables`, `.schema`, `.sql`, `.vars`, `.load`, and multiline procedure definitions.

## Where PL/SQL Is Still Ahead

Oracle PL/SQL has decades of database-centric features that MyPL does not yet match. The table below lists the major areas where MyPL is behind, rewritten as positive next-step goals.

| Area | MyPL Today | PL/SQL Capability | Next-Step Goal |
|------|-----------|-------------------|----------------|
| **Program units** | Only `proc` (returns one value) | Separate `FUNCTION` and `PROCEDURE`; stored in the database schema | Add `func` as a first-class unit that can be called from SQL expressions |
| **Packages** | `import` files only | `PACKAGE` / `PACKAGE BODY` with public/private spec, session state, overloading | Implement packages for namespacing, encapsulation, and stateful APIs |
| **Parameters** | `IN` only | `IN`, `OUT`, `IN OUT` parameter modes | Support `OUT` and `IN OUT` parameters for multi-value returns |
| **Cursors** | Implicit via `for row in SELECT` | Explicit cursors, cursor variables (`REF CURSOR`), cursor attributes (`%FOUND`, `%NOTFOUND`, `%ROWCOUNT`, `%ISOPEN`) | Add explicit cursor variables and cursor attributes |
| **Collections** | `array<T>`, `map<string,T>` | Associative arrays, nested tables, `VARRAY`s, collection methods (`EXTEND`, `TRIM`, `DELETE`, `FIRST`/`LAST`/`NEXT`/`PRIOR`) | Add associative arrays and standard collection methods |
| **Types** | Basic types and structs | `%TYPE`, `%ROWTYPE`, records, user-defined subtypes, `DATE`/`TIMESTAMP`, JSON/vector (23ai) | Add `%TYPE`/`%ROWTYPE`, date/time type, and record compatibility |
| **Control flow** | `if`/`else`, loops | `CASE` statement/expression, labeled blocks, `GOTO` | Add a `case` statement and labeled blocks |
| **Blocks** | `proc` bodies only | Anonymous `DECLARE ... BEGIN ... EXCEPTION ... END` blocks | Support anonymous/unnamed blocks for ad-hoc scripting |
| **Exceptions** | Generic `try/catch` with message string | Named predefined/user-defined exceptions, `RAISE`, `RAISE_APPLICATION_ERROR`, `SQLCODE`/`SQLERRM` | Add named exceptions, user-defined exceptions, and `raise` |
| **Bulk binds** | Row-by-row loops | `BULK COLLECT INTO`, `FORALL` for set-based DML | Implement bulk-bind opcodes for performance |
| **Triggers** | None | DML/DDL/system triggers, row/statement level, `BEFORE`/`AFTER`/`INSTEAD OF` | Add database triggers |
| **Stored code** | Source files compiled at runtime | Schema-stored procedures/functions/packages compiled into the DB | Persist MyPL program units in the database catalog |
| **Built-in packages** | Custom natives | `DBMS_OUTPUT`, `UTL_FILE`, `DBMS_SQL`, `DBMS_SCHEDULER`, etc. | Grow MyPL's standard library into named packages |
| **Table functions** | None | Pipelined/table functions returning collections | Add table-valued/pipelined functions |
| **Object types** | `struct` only | Object types with methods, inheritance, persistence | Extend `struct` toward object types with methods |
| **Transactions** | `begin`/`commit`/`rollback` | Savepoints, autonomous transactions (`PRAGMA AUTONOMOUS_TRANSACTION`) | Add savepoints and autonomous transactions |
| **Security** | None | `AUTHID CURRENT_USER` / `AUTHID DEFINER` | Add invoker/definer rights for stored units |
| **Regex** | None | `REGEXP_LIKE`, `REGEXP_REPLACE`, etc. | Add regular-expression natives |
| **Sequences** | None | `SEQUENCE` objects and `CURRVAL`/`NEXTVAL` | Add sequence support |

## Phased Roadmap

### Phase 1 — Core PL/SQL Foundations (complete)
- [x] SQL DML feedback: `sql_rowcount`, `sql_found`, `sql_notfound`
- [x] Dynamic SQL: `execute_immediate`
- [x] Stabilize structured exception handling: `try`/`catch` with reliable error message
- [x] Add `tests/test_phase1.c` and `examples/phase1.mypl`

### Phase 2 — Functions, Parameters, and Control Flow
- Add `func` keyword and SQL-callable functions
- Support `OUT` and `IN OUT` parameter modes
- Add `case` statement/expression
- Add labeled blocks and `GOTO`
- Support anonymous blocks

### Phase 3 — Cursors and Collections
- Explicit cursor variables and `REF CURSOR`-like behavior
- Cursor attributes (`%FOUND`, `%NOTFOUND`, `%ROWCOUNT`, `%ISOPEN`)
- Associative arrays and collection methods
- Bulk binds: `BULK COLLECT` and `FORALL`

### Phase 4 — Schema-Level Program Units
- Packages (`package`/`package body`) with spec/body separation
- Stored procedures/functions/packages in the database catalog
- `AUTHID CURRENT_USER` / `AUTHID DEFINER`
- Triggers (DML, DDL, system)

### Phase 5 — Type System and Standard Library
- `%TYPE` and `%ROWTYPE`
- `DATE`/`TIMESTAMP` type and formatting natives
- Named built-in packages (`dbms_output`, `utl_file`, etc.)
- Regular expressions
- Sequences

### Phase 6 — Advanced Features
- Pipelined/table functions
- Object types with methods
- Autonomous transactions
- Conditional compilation
- External procedure linkage

## How to Use This Document

1. Pick the next uncompleted item from Phase 1.
2. Write a failing test in `tests/` before implementing.
3. Update this file to mark the item done when `make test` passes.
4. Do not jump ahead to Phase 3+ features (explicit cursors, packages, triggers, etc.) without finishing the earlier phases first.

## References

- [Oracle PL/SQL Language Reference](https://docs.oracle.com/en/database/oracle/oracle-database/23/lnpls/database-pl-sql-language-reference.pdf)
- [PL/SQL — Wikipedia](https://en.wikipedia.org/wiki/PL/SQL)
- MyPL `README.md` for current build/test instructions.
