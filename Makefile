CC = gcc
CFLAGS = -O2 -Wall
TARGET = shell2c
SRC = shell2c.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

test: $(TARGET)
	@cd tests && for t in test1.sh test2.sh twopipe.sh; do \
		if [ -f $$t ]; then \
			../shell2c $$t $${t%.sh}.c 2>/dev/null; \
			$(CC) $(CFLAGS) -o $${t%.sh} $${t%.sh}.c 2>/dev/null; \
			if diff <(bash $$t 2>&1) <(./tests/$${t%.sh} 2>&1) >/dev/null 2>&1; then \
				echo "PASS: $$t"; \
			else \
				echo "FAIL: $$t"; \
			fi; \
		fi; \
	done

clean:
	rm -f $(TARGET) tests/*.c tests/test1 tests/test2 tests/twopipe

.PHONY: all test clean
