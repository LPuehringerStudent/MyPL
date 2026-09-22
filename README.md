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
- **Small and hackable**: A single C99 codebase. 
- **Scriptable**: Run `.mypl` files from the command line or explore data
  interactively in the REPL.

## Quick start

```bash
git clone https://github.com/LPuehringerStudent/MyPL.git
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
    age int,
    active bool not null default true
);

insert into users values (1, "alice", 30, true);

for user in select id, name from users where age > 25 and active = true {
    print concat(int_to_string(user.id), concat(" ", user.name));
}
```

Column types are `int`, `float`, `string` and `bool`. A `bool` column takes the
`true` and `false` literals and reads back into a `bool` variable; see
`examples/bool_columns.mypl`.

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

### Triggers

Statement-level triggers fire once per statement; row-level triggers fire
per affected row with `:new` / `:old` row context. Trigger definitions
persist in the database and survive restarts, and they fire on dynamic SQL
(`execute_immediate`, `dbms_sql.execute`) too.

```mypl
// Statement level: audit every insert into orders.
trigger orders_audit before insert on orders {
    dbms_output.put_line("insert into orders");
}

// Row level: validate each new row before it is written.
trigger orders_check before insert on orders for each row {
    if :new.total < 0 {
        raise_application_error(-20001, "negative total");
    }
}

drop trigger orders_audit;
```

### Sequences

Persistent sequences work like Oracle's: `create_sequence`, `nextval`,
`currval`, and `drop_sequence`, with the current value stored in the
database catalog so they continue across process restarts.

```mypl
create_sequence("order_seq", 1000, 1);
insert into orders values (nextval("order_seq"), "alice", 42);
print int_to_string(currval("order_seq"));
```

### Views

The custom engine supports `create view` / `drop view`; a view stores its
`SELECT` and resolves recursively, so it composes with an outer `WHERE`,
`ORDER BY`, and `LIMIT`. Views are read-only.

```mypl
create view big_orders as select id, total from orders where total > 100;
for o in select id from big_orders order by total desc limit 5 {
    print int_to_string(o.id);
}
drop view big_orders;
```

### Indexes, constraints, and NULLs

The custom engine has page-based B-tree indexes (`create index idx on t (col)`,
`drop index idx`), column constraints (`primary key`, `unique`, `not null`,
`default`), and full three-valued NULL semantics with `IS [NOT] NULL`,
`coalesce`, and `nvl`. `WHERE` supports `and`/`or`/`not`, parentheses, `IN`,
and `LIKE`.

### The dbms_sql and utl_file packages

`dbms_sql` offers a cursor-style API for dynamic SQL with bind variables,
and `utl_file` wraps host files (append/seek/flush, mkdir/remove):

```mypl
int c = dbms_sql.open_cursor();
dbms_sql.parse(c, "insert into orders values (?1, ?2, ?3)");
dbms_sql.bind_variable(c, "1", 7);
dbms_sql.bind_variable(c, "2", "bob");
dbms_sql.bind_variable(c, "3", 19);
int n = dbms_sql.execute_cursor(c);
dbms_sql.close_cursor(c);

int f = utl_file.fopen("/tmp/log.txt", "a");
utl_file.put_line(f, "appended line");
utl_file.fclose(f);
```

### Calling native libraries (external_call)

`external_call` invokes a C function from a shared library via
`dlopen`/`dlsym`. The native name selects the C return type; the argument's
C type follows its MyPL runtime type (`int`, `float`, or `string`):

```mypl
// double sqrt(double) from libm
float root = external_call_float("libm.so.6", "sqrt", 2.0);
// size_t strlen(const char*) from libc — int return
int n = external_call("libc.so.6", "strlen", "hello");
// const char* getenv(const char*) — string return (NULL becomes null)
string home = external_call_string("libc.so.6", "getenv", "HOME");
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

### Conditional compilation

Use line-oriented directives to include code for selected builds. Flags can be
defined in source with `$define` or supplied when running a file with `-DNAME`:

```mypl
proc main() -> int {
$if DEBUG $then
    print "debug logging enabled";
$else
    print "release mode";
$end
    return 0;
}
```

```bash
./bin/mypl -DDEBUG program.mypl
./bin/mypl -DDEBUG -DTRACE program.mypl
```

Command-line flags are boolean, may be repeated, and apply to the input file
and its imported modules. Use `$undefine NAME` within a source file to disable
a flag for the rest of that compilation unit.

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
- Three-valued NULL semantics: `IS [NOT] NULL`, `coalesce`, `nvl`.
- `SELECT ... INTO` for scalar, multi-value, and `array<row>` assignment.
- `BULK COLLECT INTO` and `FORALL` for set-based operations.
- Explicit cursor variables with `open`, `fetch`, `close`, and attributes
  `%FOUND`, `%NOTFOUND`, `%ROWCOUNT`, `%ISOPEN`.
- DDL in the custom engine: `create`/`drop`/`alter table`, `create`/`drop
  index`, `create`/`drop view`, plus column constraints and `alter table add
  or drop column`.
- Statement-level and row-level triggers (`for each row` with `:new`/`:old`)
  that persist in the database and fire on dynamic SQL; `drop trigger`.
- Persistent sequences (`create_sequence`, `nextval`, `currval`,
  `drop_sequence`) stored in the catalog.
- Exception handling with named predefined/user-defined exceptions,
  `raise`, `raise_application_error`, `sqlcode`, and `sqlerrm`.
- Packages with spec/body, state, and sidecar/catalog persistence — a user
  package of the same name overrides a built-in one.
- The `dbms_output`, `dbms_sql` (full cursor API), and `utl_file` packages.
- `external_call` FFI marshalling for int, float, and string signatures.
- User-defined subtypes (`subtype name is base;`).
- `%TYPE` and `%ROWTYPE` type attributes.
- Import system for splitting code across files.
- Conditional compilation with `$define`, `$undefine`, `$if`, `$elsif`,
  `$else`, `$end`, and command-line `-DNAME` flags.
- SQLite backend via `--db <path>` or `.connect <path>`.
- Custom SQL engine fallback when no `--db` is supplied.
- Standard library: `length`, `append`, `concat`, `split`, `join`, `replace`,
  `trim`, `to_upper`, `to_lower`, `parse_int`, `split_lines`, `range`,
  `assert`, `format`, `sort`, `reverse`, `clamp`, `to_date`, `to_char`,
  `current_date`, `current_timestamp`, file I/O, and more.


Contributions and ideas are welcome — see
[`CONTRIBUTING.md`](CONTRIBUTING.md) for the workflow (failing-test-first,
clean rebuilds, sanitizer checks) and [`SECURITY.md`](SECURITY.md) for
reporting vulnerabilities.

## License

[MIT](LICENSE)
