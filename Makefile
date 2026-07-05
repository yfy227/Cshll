CC = gcc
CFLAGS = -O2 -Wall
TARGET = shell2c
SRC = shell2c.c src/s2c_obfuscate.c src/s2c_mangle.c

all: $(TARGET)

$(TARGET): $(SRC)
        $(CC) $(CFLAGS) -o $@ $<

test: $(TARGET)
        @cd tests && for t in test1.sh test2.sh test3.sh test4.sh test5.sh test6.sh twopipe.sh test_hd.sh test_realworld.sh test_newfeat.sh test_compat.sh test_complex.sh test_patterns.sh; do \
                if [ -f $$t ]; then \
                        ../shell2c $$t $${t%.sh}.c 2>/dev/null; \
                        $(CC) $(CFLAGS) -o $${t%.sh} $${t%.sh}.c 2>/dev/null; \
                        if diff <(bash $$t 2>&1) <(./$${t%.sh} 2>&1) >/dev/null 2>&1; then \
                                echo "PASS: $$t"; \
                        else \
                                echo "FAIL: $$t"; \
                        fi; \
                fi; \
        done

test-obfuscate: $(TARGET)
        @cd tests && for t in test1.sh test2.sh; do \
                if [ -f $$t ]; then \
                        ../shell2c $$t $${t%.sh}_obf.c --obfuscate 2>/dev/null; \
                        $(CC) $(CFLAGS) -o $${t%.sh}_obf $${t%.sh}_obf.c 2>/dev/null; \
                        if diff <(bash $$t 2>&1) <(./$${t%.sh}_obf 2>&1) >/dev/null 2>&1; then \
                                echo "PASS (obfuscated): $$t"; \
                        else \
                                echo "FAIL (obfuscated): $$t"; \
                        fi; \
                fi; \
        done

clean:
        rm -f $(TARGET) tests/*.c tests/test1 tests/test2 tests/test3 tests/test4 tests/test5 tests/test6 tests/twopipe tests/test_hd tests/test_rw tests/test_nf tests/test_compat tests/test_complex tests/test_patterns tests/test_syscmd tests/*_obf

.PHONY: all test test-obfuscate clean
