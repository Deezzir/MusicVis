CC = clang
CFLAGS = -Wall -Wextra $(shell pkg-config --cflags raylib)
LIBS = $(shell pkg-config --libs raylib) $(shell pkg-config --libs glfw3) -lm -ldl -lpthread

DEBUG ?= 0
HOTRELOAD ?= 0

ifeq ($(DEBUG),1)
    CFLAGS += -ggdb
endif

ifeq ($(HOTRELOAD),1)
    TARGET = hotreload
else
	CFLAGS += -O3
    TARGET = release
endif

.PHONY: all clean $(TARGET)

all: $(TARGET)

release:
	mkdir -p ./build/
	$(CC) $(CFLAGS) -O3 -o ./build/musicvis ./src/plug.c ./src/musicvis.c $(LIBS) -L./build/

hotreload:
	mkdir -p ./build/
	$(CC) $(CFLAGS) -o ./build/libplug.so -fPIC -shared ./src/plug.c $(LIBS)
	$(CC) $(CFLAGS) -DHOTRELOAD -o ./build/musicvis ./src/musicvis.c $(LIBS) -L./build/

clean:
	rm -rf ./build/