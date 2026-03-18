CC      = clang
CFLAGS  = -std=gnu23 -O2 -Wall -Wextra
TARGET  = flint
SRC     = flint.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET) fib30.flint

clean:
	rm -f $(TARGET)

.PHONY: run clean
