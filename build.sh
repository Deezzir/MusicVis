#!/bin/sh

set -xe

CFLAGS="-Wall -Wextra `pkg-config --cflags raylib`"
LIBS="`pkg-config --libs raylib` -lglfw -lm"

mkdir -p ./build/

if [ ! -z "${HOTRELOAD}" ]; then
    clang $CFLAGS -o ./build/libplug.so -fPIC -shared ./src/plug.c $LIBS
    clang $CFLAGS -DHOTRELOAD -o ./build/musializer ./src/musializer.c $LIBS -L./build/
else
    clang $CFLAGS  -o ./build/musializer ./src/plug.c ./src/musializer.c $LIBS -L./build/
fi