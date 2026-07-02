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

## Features

- C-like syntax.
- Embedded SQL with parameter binding (`?var`).
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
