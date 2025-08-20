all: build

CC = clang
CFLAGS = -std=c99 -Wall -Werror
TARGET = emd
SRC = src/main.c
IDIR = -Isrc -Ideps
DEPS = deps/mongoose.c
LIBS = -lz -ljansson

build: $(SRC) $(DEPS)
	$(CC) $(CFLAGS) $(LIBS) $(IDIR) $(DEPS) $(SRC) -o $(TARGET)

dev: $(SRC) $(DEPS)
	$(CC) -g $(CFLAGS) $(LIBS) $(IDIR) $(DEPS) $(SRC) -o $(TARGET)

clean:
	rm $(TARGET)
