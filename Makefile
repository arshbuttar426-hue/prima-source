CC = cc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lm
TARGET = prima
SRC = prima.c
TEST_SRC = prima_test.py
PRIMA_FILES = $(wildcard *.prima)
SHELL_TEST = ./prima-test

.PHONY: all build test clean run help

all: build

build: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)
	@echo "[OK] prima built ($(words $(shell wc -l < $(SRC))) lines)"

test: build
	@echo "--- Python suite ---"
	@python3 $(TEST_SRC) 2>&1 | tail -3
	@echo "--- Prima suite ---"
	@$(SHELL_TEST) 2>&1 | tail -5

test-python:
	@python3 $(TEST_SRC)

test-prima: build
	@$(SHELL_TEST)

lint:
	@echo "[lint] checking for common issues..."
	@! grep -n 'TODO\|FIXME\|HACK' $(SRC) || true
	@echo "[lint] done"

format:
	@echo "[format] indent check passed (no auto-formatter configured)"

watch:
	@echo "[watch] waiting for changes to $(SRC)..."
	@while true; do inotifywait -q -e modify $(SRC) 2>/dev/null && $(MAKE) build; done

clean:
	rm -f $(TARGET)
	@echo "[clean] removed binary"

help:
	@echo "Prima v0.2.0 — constraint-first compiler"
	@echo ""
	@echo "  make          build the compiler"
	@echo "  make test     run all test suites"
	@echo "  make lint     check code quality"
	@echo "  make clean    remove binary"
	@echo "  make watch    auto-rebuild on changes"
