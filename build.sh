#!/bin/sh

set -xe

CFLAGS="-Wall -Wextra `pkg-config --cflags raylib`"
LIBS="`pkg-config --libs raylib` -lglfw -lm"

mkdir -p ./build/

if [ ! -z "${DEBUG}" ]; then
    CFLAGS="${CFLAGS} -ggdb"
fi

if [ ! -z "${HOTRELOAD}" ]; then
    clang $CFLAGS -o ./build/libplug.so -fPIC -shared ./src/plug.c $LIBS
    clang $CFLAGS -DHOTRELOAD -o ./build/musicvis ./src/musicvis.c $LIBS -L./build/
else
    clang $CFLAGS  -o ./build/musicvis ./src/plug.c ./src/musicvis.c $LIBS -L./build/
    
fi