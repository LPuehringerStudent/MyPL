CC      = cc
CFLAGS  = -Wall -Wextra -std=c99 -D_GNU_SOURCE -Iinclude
LDFLAGS =

SRCDIR  = src
OBJDIR  = build
BINDIR  = bin

SOURCES     = $(wildcard $(SRCDIR)/*.c)
OBJECTS     = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))
LIB_OBJECTS = $(filter-out $(OBJDIR)/main.o,$(OBJECTS))

TARGET      = $(BINDIR)/mydb

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR) $(BINDIR):
	mkdir -p $@

test: $(TARGET)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_value tests/test_value.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_chunk tests/test_chunk.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_compiler tests/test_compiler.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_pager tests/test_pager.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_vm tests/test_vm.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_sql tests/test_sql.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_sql_engine tests/test_sql_engine.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_lexer tests/test_lexer.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_parser tests/test_parser.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_cli tests/test_cli.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_natives tests/test_natives.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_strings tests/test_strings.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_file_io tests/test_file_io.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_typecheck tests/test_typecheck.c $(LIB_OBJECTS) $(LDFLAGS)
	$(BINDIR)/test_value
	$(BINDIR)/test_chunk
	$(BINDIR)/test_compiler
	$(BINDIR)/test_pager
	$(BINDIR)/test_vm
	$(BINDIR)/test_sql
	$(BINDIR)/test_sql_engine
	$(BINDIR)/test_lexer
	$(BINDIR)/test_parser
	$(BINDIR)/test_cli
	$(BINDIR)/test_natives
	$(BINDIR)/test_strings
	$(BINDIR)/test_file_io
	$(BINDIR)/test_typecheck

clean:
	rm -rf $(OBJDIR) $(BINDIR)
