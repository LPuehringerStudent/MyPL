CC      = cc
CFLAGS  = -Wall -Wextra -std=c99 -Iinclude
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
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_chunk tests/test_chunk.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_compiler tests/test_compiler.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_vm tests/test_vm.c $(LIB_OBJECTS) $(LDFLAGS)
	$(CC) $(CFLAGS) -Itests -o $(BINDIR)/test_sql tests/test_sql.c $(LIB_OBJECTS) $(LDFLAGS)
	$(BINDIR)/test_chunk
	$(BINDIR)/test_compiler
	$(BINDIR)/test_vm
	$(BINDIR)/test_sql

clean:
	rm -rf $(OBJDIR) $(BINDIR)
