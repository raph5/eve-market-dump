all: build

CC = clang
CFLAGS = -std=c99 -Wall -Werror
TARGET = emd
SRC = main.c

build: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

dev: $(SRC)
	$(CC) -g $(CFLAGS) $(SRC) -o $(TARGET)
