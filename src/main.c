#include <assert.h>
#include <dlfcn.h>
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>

#include "plug.h"

#define ARRAY_LEN(xs) sizeof(xs) / sizeof(xs[0])
#define WIDTH 800
#define HEIGHT 450

const char* libplug_file_name = "libplug.so";
void* libplug = NULL;

#ifdef HOTRELOAD
#define PLUG(name) name##_t *name = NULL;
LIST_OF_PLUGS
#undef PLUG
#else
#define PLUG(name) name##_t name;
LIST_OF_PLUGS
#undef PLUG
#endif
Plug plug = {0};

char* shift_args(int* argc, char*** argv) {
    assert(*argc > 0);
    char* result = (**argv);
    (*argv) += 1;
    (*argc) -= 1;
    return result;
}

#ifdef HOTRELOAD
bool reload_libplug(void) {
    if (libplug != NULL) dlclose(libplug);

    libplug = dlopen(libplug_file_name, RTLD_NOW);
    if (libplug == NULL) {
        fprintf(stderr, "ERROR: could nopt load %s: %s\n", libplug_file_name, dlerror());
        return false;
    }

    #define PLUG(name) \
    name = dlsym(libplug, #name); \
    if (name == NULL) { \
        fprintf(stderr, "ERROR: could not find '%s' symbol in %s: %s\n", #name, libplug_file_name, dlerror()); \
        return false; \
    }
    LIST_OF_PLUGS
    #undef PLUG

    return true;
}
#else
#define reload_libplug() true
#endif

int main(int argc, char** argv) {
    if (!reload_libplug()) return 1;

    const char* program = shift_args(&argc, &argv);
    if (argc == 0) {
        fprintf(stderr, "Usage: %s <input>\n", program);
        fprintf(stderr, "ERROR: no input file was provided\n");
        return 1;
    }
    const char* mus_file_path = shift_args(&argc, &argv);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WIDTH, HEIGHT, "Music Visualizer");
    SetTargetFPS(60);
    InitAudioDevice();

    plug_init(&plug, mus_file_path);

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_R)) {
            plug_pre_reload(&plug);
            if (!reload_libplug()) return 1;
            plug_post_reload(&plug);
        }
        plug_update(&plug);
    }

    CloseAudioDevice();
    CloseWindow();
    return 0;
}