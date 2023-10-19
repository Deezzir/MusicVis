#include <assert.h>
#include <dlfcn.h>
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>

#include "plug.h"

#define ARRAY_LEN(xs) sizeof(xs) / sizeof(xs[0])
#define FACTOR 60

const char* libplug_file_name = "libplug.so";
void* libplug = NULL;

#ifdef HOTRELOAD
#define PLUG(name, ...) name##_t* name = NULL;
LIST_OF_PLUGS
#undef PLUG
#else
#define PLUG(name, ...) name##_t name;
LIST_OF_PLUGS
#undef PLUG
#endif

#ifdef HOTRELOAD
bool reload_libplug(void) {
    if (libplug != NULL) dlclose(libplug);

    libplug = dlopen(libplug_file_name, RTLD_NOW);
    if (libplug == NULL) {
        fprintf(stderr, "ERROR: could nopt load %s: %s\n", libplug_file_name, dlerror());
        return false;
    }

#define PLUG(name, ...)                                                                                        \
    name = dlsym(libplug, #name);                                                                              \
    if (name == NULL) {                                                                                        \
        fprintf(stderr, "ERROR: could not find '%s' symbol in %s: %s\n", #name, libplug_file_name, dlerror()); \
        return false;                                                                                          \
    }
    LIST_OF_PLUGS
#undef PLUG

    return true;
}
#else
#define reload_libplug() true
#endif

int main(void) {
    if (!reload_libplug()) return 1;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(FACTOR * 16, FACTOR * 9, "Music Visualizer");
    SetTargetFPS(60);
    InitAudioDevice();

    plug_init();

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_R)) {
            void* plug = plug_pre_reload();
            if (!reload_libplug()) return 1;
            plug_post_reload(plug);
        }
        plug_update();
    }

    CloseAudioDevice();
    CloseWindow();
    return 0;
}