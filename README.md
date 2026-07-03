# MyPL

A lightweight, open-source alternative to PL/SQL with C-like syntax. MyPL
compiles to bytecode for a small stack VM and uses SQLite as its embedded
database engine, so you get stored-procedure-style scripting without the
weight of an Oracle installation.

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
- **SQLite-backed**: Run against `:memory:`, a file, or no database at all
  (custom engine fallback).
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

### Procedures

```mypl
proc greet(name string) -> int {
    print concat("Hello, ", name);
    return 0;
}
```

### Variables and types

```mypl
int count = 42;
float pi = 3.14;
string message = "hello";
bool active = true;
array<int> nums = [1, 2, 3];
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

## Examples

The `examples/` directory contains runnable programs that show what MyPL looks
like for real tasks.

### Todo list (`examples/todo.mypl`)

A minimal CRUD example.

```bash
./bin/mypl examples/todo.mypl --db todos.db
```

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

- C-like syntax.
- Procedures with parameters and return values.
- `int`, `float`, `string`, `bool`, and typed `array<T>`.
- `while`, numeric `for`, `for ... in`, `break`, `continue`.
- Embedded SQL with `?var` parameter binding.
- `SELECT ... INTO` for scalar and array assignment.
- SQLite backend via `--db <path>`.
- Custom SQL engine fallback when no `--db` is supplied.
- Import system for splitting code across files.
- Growing standard library: `length`, `append`, `concat`, `split`, `join`,
  `replace`, `trim`, `to_upper`, `to_lower`, `parse_int`, `split_lines`,
  `range`, `assert`, file I/O, and more.

## Roadmap

MyPL is intentionally small today, but the goal is to become a credible
open-source alternative to PL/SQL for lightweight database scripting. Upcoming
directions include:

- More control flow and collection operations.
- Better error messages with source locations.
- A small package/module ecosystem.
- More comprehensive standard library.
- Improved custom SQL engine.

Contributions and ideas are welcome.

## License

MIT
