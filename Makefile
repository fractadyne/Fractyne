CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Isrc
SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
BIN = bin/fractyne

.PHONY: all clean test

all: $(BIN)

$(BIN): $(OBJ) | bin
	$(CC) $(CFLAGS) -o $@ $(OBJ)

bin:
	mkdir -p bin

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: all
	bash tests/run_tests.sh

clean:
	rm -f src/*.o
	rm -rf bin
