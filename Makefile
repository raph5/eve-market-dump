all: build

CC = clang
CFLAGS = -std=c99 -Wall -Werror
TARGET = emd
SRC = src/main.c
IDIR = -Isrc -Ideps
LIBS = -lz -ljansson -lcurl

build: $(SRC) $(DEPS)
	$(CC) $(CFLAGS) $(LIBS) $(IDIR) $(DEPS) $(SRC) -o $(TARGET)

dev: $(SRC) $(DEPS)
	$(CC) $(FEATURES) -g -DFANCY_PANIC $(CFLAGS) $(LIBS) $(IDIR) $(DEPS) $(SRC) -o $(TARGET)

sanitize: $(SRC) $(DEPS)
	$(CC) $(FEATURES) -g -DFANCY_PANIC -fsanitize=address $(CFLAGS) $(LIBS) $(IDIR) $(DEPS) $(SRC) -o $(TARGET)

clean:
	rm $(TARGET)
