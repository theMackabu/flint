CC      = clang
CFLAGS  = -std=gnu23 -O3 -flto -Wall -Wextra
TARGET  = flint
SRC     = flint.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<
	strip $@

run: $(TARGET)
	./$(TARGET) fib30.flint

clean:
	rm -f $(TARGET)

.PHONY: run clean
