# ================================================================
#  Makefile  —  XPILE Transpiler
#
#  Targets
#  ───────
#  make            Build the transpiler binary (default)
#  make clean      Remove all object files and the binary
#  make rebuild    Clean then build from scratch
#  make test       Build and run the quick smoke-test suite
#
#  Output binary:  ../transpiler   (one directory above src/)
#  Object files:   *.o             (in this directory, cleaned by `make clean`)
#
#  Compiler flags
#  ──────────────
#  -std=c99        C99 for compound literals and // comments
#  -Wall -Wextra   All standard warnings
#  -w  →  removed; warnings are on so issues surface during development.
#          Pipe through  make 2>/dev/null  if you prefer silence.
# ================================================================

CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-format-truncation -Wno-comment
TARGET  = ../transpiler

# Every .c file in this directory is a compilation unit.
SRCS = globals.c \
       util.c \
       containers.c \
       lexer.c \
       parser.c \
       codegen.c \
       output.c \
       main.c

OBJS = $(SRCS:.c=.o)

# ── Default target ────────────────────────────────────────────────
.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "✓  Built $(TARGET)"

# ── Compile each .c → .o ─────────────────────────────────────────
# All headers live in the same directory so -I. suffices.
%.o: %.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

# ── Explicit header dependencies ─────────────────────────────────
# (keeps incremental builds correct when a header changes)
globals.o:    globals.c    xpile.h globals.h
util.o:       util.c       xpile.h globals.h util.h
containers.o: containers.c xpile.h globals.h containers.h
lexer.o:      lexer.c      xpile.h globals.h util.h lexer.h
parser.o:     parser.c     xpile.h globals.h util.h containers.h parser.h
codegen.o:    codegen.c    xpile.h globals.h util.h containers.h codegen.h
output.o:     output.c     xpile.h globals.h util.h codegen.h output.h
main.o:       main.c       xpile.h globals.h util.h lexer.h parser.h output.h

# ── Utility targets ───────────────────────────────────────────────
.PHONY: clean rebuild test

clean:
	rm -f $(OBJS) $(TARGET)
	@echo "✓  Cleaned"

rebuild: clean all

# ── Smoke tests ───────────────────────────────────────────────────
# Each test pipes a tiny program through the transpiler and checks
# that the JSON output contains (or does not contain) a key string.
# Prints PASS / FAIL per test, exits non-zero if any test fails.
test: $(TARGET)
	@echo "Running smoke tests…"
	@PASS=0; FAIL=0; \
	check() { \
	  label="$$1"; input="$$2"; want="$$3"; \
	  result=$$(printf '%b' "$$input" | $(TARGET)); \
	  if printf '%s' "$$result" | grep -q "$$want"; then \
	    echo "  PASS  $$label"; PASS=$$((PASS+1)); \
	  else \
	    echo "  FAIL  $$label"; \
	    printf '        %.120s\n' "$$result"; \
	    FAIL=$$((FAIL+1)); \
	  fi; \
	}; \
	check "C printf — IR_PRINT emits printf call" \
	      "c\nint x=5;\nprintf(\"%d\",x);" \
	      '"c":".*printf'; \
	check "C++ class — zero errors" \
	      "cpp\nclass Foo {\npublic:\n    int a = 0;\n};\n" \
	      '"errors":\[\]'; \
	check "vector<int> → ArrayList in Java" \
	      "cpp\nvector<int> v(3,0);\n" \
	      'ArrayList'; \
	check "push_back → append in Python" \
	      "cpp\nvector<int> v;\nv.push_back(1);\n" \
	      'append'; \
	check "srclang tag preserved" \
	      "java\nint x=1;\n" \
	      '"srclang":"java"'; \
	check "Python snippet — no stray numbers" \
	      "python\nx = 5\nprint x\n" \
	      '"errors":\[\]'; \
	check "map<int,int> → HashMap in Java" \
	      "cpp\nmap<int,int> m;\n" \
	      'HashMap'; \
	check "OOP class in C++ — no errors" \
	      "cpp\nclass Point {\npublic:\n    int x = 0;\n    int y = 0;\n    Point(int a, int b) {\n        x = a;\n        y = b;\n    }\n};\n" \
	      '"errors":\[\]'; \
	echo ""; \
	echo "Results: $$PASS passed, $$FAIL failed."; \
	[ "$$FAIL" = "0" ]
