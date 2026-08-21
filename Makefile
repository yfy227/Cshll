CC = gcc
CFLAGS = -O2 -Wall -Isrc -I.
LDFLAGS = -lpthread
TARGET = shell2c

SHELL := bash
TESTSHELL = bash

# ---- Modular build (default) -------------------------------------------
# 11 compiler translation units + 4 support units (obfuscate, mangle,
# vm_runtime, vm_bridge). Generated during the 2026-08 modularization
# from the former single-file amalgamation; outputs verified
# byte-identical across the full test corpus (40/40 scripts).
MOD_SRC = \
    src/vm_compiler.c \
    src/symtab.c \
    src/ast.c \
    src/tokenizer.c \
    src/translate.c \
    src/expand.c \
    src/cond.c \
    src/emit.c \
    src/parse.c \
    src/runtime_data.c \
    src/s2c_main.c
SUP_SRC = \
    src/s2c_obfuscate.c \
    src/s2c_mangle.c \
    src/s2c_vm_runtime.c \
    src/s2c_vm_bridge.c

MOD_OBJ = $(MOD_SRC:.c=.o)
SUP_OBJ = $(SUP_SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(MOD_OBJ) $(SUP_OBJ)
	$(CC) $(CFLAGS) -o $@ $(MOD_OBJ) $(SUP_OBJ) $(LDFLAGS)

%.o: %.c src/s2c_all.h
	$(CC) $(CFLAGS) -Werror=implicit-function-declaration -c -o $@ $<

# ---- Amalgamation build (cross-verification) ---------------------------
# Legacy single-file path: shell2c.c #includes src/parts/*.inc.
# Kept as a verification target — build both and diff outputs:
#   make shell2c_amalg && ./tools/verify_parity.sh
shell2c_amalg: shell2c.c $(SRC_INC) src/s2c_obfuscate.c src/s2c_mangle.c src/s2c_vm_runtime.c src/s2c_vm_bridge.c
	$(CC) $(CFLAGS) -o $@ shell2c.c src/s2c_obfuscate.c src/s2c_mangle.c src/s2c_vm_runtime.c src/s2c_vm_bridge.c $(LDFLAGS)

SRC_INC = $(wildcard src/parts/*.inc)

# ---- Test suites ---------------------------------------------------------
test: $(TARGET)
	bash tests/regression_2026_08.sh
	bash tests/run_syntax_tests.sh
	@cd tests && for t in test1.sh test2.sh test3.sh test4.sh test5.sh test6.sh twopipe.sh test_hd.sh test_realworld.sh test_newfeat.sh test_compat.sh test_complex.sh test_patterns.sh; do \
		if [ -f $$t ]; then \
			../shell2c $$t $${t%.sh}.c 2>/dev/null; \
			$(CC) $(CFLAGS) -o $${t%.sh} $${t%.sh}.c 2>/dev/null; \
			if diff <($(TESTSHELL) $$t 2>&1) <(./$${t%.sh} 2>&1) >/dev/null 2>&1; then \
				echo "PASS: $$t"; \
			else \
				echo "FAIL: $$t"; \
			fi; \
		fi; \
	done

test-vm: $(TARGET)
	@cd tests && for t in test1.sh test2.sh test3.sh; do \
		if [ -f $$t ]; then \
			../shell2c $$t $${t%.sh}_vm.c --vm 2>/dev/null; \
			$(CC) $(CFLAGS) -o $${t%.sh}_vm $${t%.sh}_vm.c 2>/dev/null; \
		fi; \
	done

test-obfuscate: $(TARGET)
	@cd tests && for t in test1.sh test2.sh test3.sh; do \
		if [ -f $$t ]; then \
			../shell2c $$t $${t%.sh}_obf.c --obfuscate 2>/dev/null; \
			$(CC) $(CFLAGS) -o $${t%.sh}_obf $${t%.sh}_obf.c 2>/dev/null; \
			if diff <($(TESTSHELL) $$t 2>&1) <(./$${t%.sh}_obf 2>&1) >/dev/null 2>&1; then \
				echo "PASS (obfuscated): $$t"; \
			else \
				echo "FAIL (obfuscated): $$t"; \
			fi; \
		fi; \
	done

clean:
	rm -f $(TARGET) shell2c_amalg $(MOD_OBJ) $(SUP_OBJ) tests/*.c tests/test1 tests/test2 tests/test3 tests/test4 tests/test5 tests/test6 tests/twopipe tests/test_hd tests/test_rw tests/test_nf tests/test_compat tests/test_complex tests/test_patterns tests/test_syscmd tests/*_obf tests/*_vm

.PHONY: all test test-vm test-obfuscate clean
