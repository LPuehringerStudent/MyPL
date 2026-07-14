# MyPL

A lightweight, open-source alternative to PL/SQL with C-like syntax. MyPL
compiles to bytecode for a small stack VM and can run against either its
built-in custom SQL engine or SQLite, so you get stored-procedure-style
scripting without the weight of an Oracle installation. SQLite is entirely
optional — build with `USE_SQLITE=0` for a standalone custom-engine-only
binary.

```mypl
proc add_todo(title string) -> int {
    insert into todos (title, done) values (?title, 0);
    return 0;
}

proc list_todos() -> int {
    for todo in select id, title from todos {
        print concat(int_to_string(todo.id), concat(": ", todo.title));
    }
    return 0;
}
```

## Why MyPL?

- **Familiar syntax**: C-like procedures, variables, loops, and expressions —
  no PL/SQL boilerplate.
- **Embedded SQL**: Write DDL, DML, and queries inline with `?var` parameter
  binding.
- **Dual SQL backends**: Run against `:memory:` or a file with SQLite, or use
  the built-in custom engine with no SQLite dependency at all.
- **Small and hackable**: A single C99 codebase. SQLite is optional — build
  with `USE_SQLITE=0` for a standalone custom-engine-only binary.
- **Scriptable**: Run `.mypl` files from the command line or explore data
  interactively in the REPL.

## Quick start

```bash
git clone <repo>
cd MyPL
make
./bin/mypl examples/todo.mypl --db :memory:
```

Expected output:

```
1: buy milk
2: walk dog
0
```

## Build

```bash
make clean && make && make test
```

Default build requires:

- A C99 compiler
- The `sqlite3` development library (`-lsqlite3`)

On Ubuntu/Debian:

```bash
sudo apt-get install libsqlite3-dev
```

On macOS:

```bash
brew install sqlite3
```

### Standalone build (no SQLite)

MyPL can be built without SQLite. In that mode the custom SQL engine is the only
backend and `--db`/`.connect` are disabled:

```bash
make clean && make USE_SQLITE=0 && make USE_SQLITE=0 test
```

## Language tour

### Procedures and functions

```mypl
proc greet(name string) -> int {
    print concat("Hello, ", name);
    return 0;
}

func square(x int) -> int {
    return x * x;
}
```

### Variables and types

```mypl
int count = 42;
float pi = 3.14;
string message = "hello";
bool active = true;
date today = current_date();
timestamp now = current_timestamp();
array<int> nums = [1, 2, 3];
map<string, int> ages = {"alice": 30, "bob": 25};
```

### Parameter modes

```mypl
proc swap(in out a int, in out b int) -> int {
    int tmp = a;
    a = b;
    b = tmp;
    return 0;
}
```

### Control flow

```mypl
int i = 0;
while i < 10 {
    print(int_to_string(i));
    i = i + 1;
}

for n in range(1, 5) {
    print(int_to_string(n));
}
```

### Embedded SQL

```mypl
create table users (
    id int primary key,
    name string,
    age int
);

insert into users values (1, "alice", 30);

for user in select id, name from users where age > 25 {
    print concat(int_to_string(user.id), concat(" ", user.name));
}
```

### SELECT INTO

```mypl
string name = "";
int age = 0;
SELECT name, age INTO name, age FROM users WHERE id = 1;
print concat(name, concat(" is ", int_to_string(age)));
```

Load an entire result set into an `array<row>`:

```mypl
array<row> users = [];
SELECT * INTO users FROM users;
print length(users);
print users[0].name;
```

### Cursors

```mypl
cursor c is select id, name from users where age > 25;
open c;
while c%found {
    int id;
    string name;
    fetch c into id, name;
    print concat(int_to_string(id), concat(" ", name));
}
close c;
```

### Collections

```mypl
array<int> nums;
nums.extend(3);
nums[0] = 10;
nums[1] = 20;
nums[2] = 30;
nums.sort();
print int_to_string(nums[0]);  // 10

map<string, int> scores;
scores["ada"] = 95;
print int_to_string(scores["ada"]);
```

### Type attributes and subtypes

```mypl
// %TYPE copies a variable or column type
int x = 42;
x%type y = 7;

// Subtypes create aliases
subtype score is int;
score s = 100;

// %ROWTYPE creates a row/record matching a table
// (the table must already exist when the program is compiled)
users%rowtype u;
u.id = 2;
u.name = "bob";
print u.name;
```

### Exceptions

```mypl
proc maybe_fetch() -> int {
    int id;
    begin
        select id into id from users where name = "nobody";
    catch (err) {
        print concat("SQL error: ", err);
    }
    return 0;
}
```

### Packages

```mypl
package math_utils is
    func add(a int, b int) -> int;
end math_utils;

package body math_utils is
    func add(a int, b int) -> int {
        return a + b;
    }
end math_utils;
```

## Examples

The `examples/` directory contains runnable programs that show what MyPL looks
like for real tasks.

### Todo list (`examples/todo.mypl`)

A minimal CRUD example.

```bash
./bin/mypl examples/todo.mypl --db todos.db
```

### Phase feature walkthroughs (`examples/phase1.mypl` … `examples/phase7.mypl`)

Each phase file demonstrates a completed milestone (exceptions, cursors,
packages, collections, type attributes, dates, subtypes, etc.).

### Data migration (`examples/migration.mypl`)

Migrates messy legacy data into a clean schema, normalizing names and
classifying ages along the way.

```bash
./bin/mypl examples/migration.mypl --db :memory:
```

Output:

```
Migrated customers: 4
Sample rows:
1: ALICE SMITH (adult)
2: BOB JONES (adult)
3: CHARLIE BROWN (minor)
4: DIANA PRINCE (young adult)
```

### Sales report (`examples/report.mypl`)

Aggregates order data into a formatted CLI report with revenue totals,
product breakdowns, and top customers.

```bash
./bin/mypl examples/report.mypl --db :memory:
```

### Inventory service (`examples/inventory.mypl`)

A small catalog-backed service that lists stock, flags low-stock items, and
processes sales with quantity validation.

```bash
./bin/mypl examples/inventory.mypl --db :memory:
```

## REPL

Start an interactive session:

```bash
./bin/mypl
```

Useful commands:

```
> .connect :memory:
> create table todos (id integer primary key, title string, done int);
> .tables
> .schema todos
> .sql select * from todos;
> .exit
```

## Features

- C-like syntax with procedures and functions.
- `in`, `out`, and `in out` parameter modes.
- Scalar types: `int`, `float`, `string`, `bool`, `date`, `timestamp`.
- Typed collections: `array<T>`, `map<string, T>`, plus methods like `extend`,
  `trim`, `sort`, `reverse`, `first`, `last`, etc.
- `struct` records and table-driven `%ROWTYPE` records.
- Control flow: `if`/`else`, `while`, `do ... while`, numeric `for`,
  `for ... in`, `case`, `break`, `continue`, `return`.
- Anonymous `declare ... begin ... end` blocks.
- Embedded SQL with `?var` parameter binding.
- `SELECT ... INTO` for scalar, multi-value, and `array<row>` assignment.
- `BULK COLLECT INTO` and `FORALL` for set-based operations.
- Explicit cursor variables with `open`, `fetch`, `close`, and attributes
  `%FOUND`, `%NOTFOUND`, `%ROWCOUNT`, `%ISOPEN`.
- Exception handling with named predefined/user-defined exceptions,
  `raise`, `raise_application_error`, `sqlcode`, and `sqlerrm`.
- Packages with spec/body, state, and sidecar/catalog persistence.
- User-defined subtypes (`subtype name is base;`).
- `%TYPE` and `%ROWTYPE` type attributes.
- Import system for splitting code across files.
- SQLite backend via `--db <path>` or `.connect <path>`.
- Custom SQL engine fallback when no `--db` is supplied.
- Standard library: `length`, `append`, `concat`, `split`, `join`, `replace`,
  `trim`, `to_upper`, `to_lower`, `parse_int`, `split_lines`, `range`,
  `assert`, `format`, `sort`, `reverse`, `clamp`, `to_date`, `to_char`,
  `current_date`, `current_timestamp`, file I/O, and more.

## Roadmap / Next Steps

MyPL is intentionally small today, but the goal is to become a credible
open-source alternative to PL/SQL for lightweight database scripting. Phases
1–7 are complete; see [`NEXT_STEPS.md`](NEXT_STEPS.md) for a concrete, phased
comparison with Oracle PL/SQL and the remaining goals that will close the gap.

High-level direction:

- Persist procedures/functions/packages in the database catalog (Phase 8).
- Add `AUTHID CURRENT_USER` / `AUTHID DEFINER` and transaction control
  improvements (Phase 8).
- Grow the standard library into named packages (`dbms_output`, `utl_file`,
  `dbms_sql`, regex, sequences) (Phase 9).
- Add triggers, pipelined/table functions, object types with methods, and
  conditional compilation (Phase 10).

Contributions and ideas are welcome.

## License

MIT
