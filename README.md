# MyPL

A lightweight, open-source alternative to PL/SQL with C-like syntax. MyPL
compiles to bytecode for a small stack VM and uses SQLite as its embedded
database engine.

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

## Example

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

Run it against a SQLite database:

```bash
./bin/mypl examples/todo.mypl --db todos.db
```

## SELECT INTO

MyPL can assign query results directly to local variables:

```mypl
proc lookup_user(search_id int) -> int {
    string name = "";
    int age = 0;
    SELECT name, age INTO name, age FROM users WHERE id = ?search_id;
    print concat(name, concat(" is ", int_to_string(age)));
    return 0;
}
```

You can also load an entire result set into an `array<row>`:

```mypl
array<row> users = [];
SELECT * INTO users FROM users;
print length(users);
print users[0].name;
```

See `examples/select_into.mypl` for a runnable example.

## Features

- C-like syntax.
- Embedded SQL with parameter binding (`?var`).
- `SELECT ... INTO` assignment for single or multiple scalar variables.
- SQLite backend (`--db <path>`).
- Custom SQL engine fallback when no `--db` is supplied.
- Interactive REPL with `.connect`, `.tables`, `.schema`, `.sql`, `.vars`, and
  `.load` commands.

## Build

```bash
make clean && make && make test
```

Requires:

- A C99 compiler
- `sqlite3` development library (`-lsqlite3`)

## REPL

```bash
./bin/mypl
> .connect :memory:
> create table todos (id integer primary key, title string, done int);
> .tables
> .schema todos
> .exit
```

## License

MIT
