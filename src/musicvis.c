#include <assert.h>
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>

#include "plug.h"

#define LOGO_FILEPATH "./resources/images/favicon.png"

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
#include <dlfcn.h>

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

void clean_libplug(void) {
    if (libplug != NULL) dlclose(libplug);
}
#else
#define reload_libplug() true
#define clean_libplug() 0
#endif

int main(void) {
    if (!reload_libplug()) return 1;

    Image logo = LoadImage(LOGO_FILEPATH);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(FACTOR * 16, FACTOR * 9, "Music Visualizer");
    SetTargetFPS(60);
    SetWindowIcon(logo);
    SetExitKey(KEY_NULL);
    InitAudioDevice();

    plug_init();

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F5)) {
            void* plug = plug_pre_reload();
            if (!reload_libplug()) return 1;
            plug_post_reload(plug);
        }
        plug_update();
    }

    plug_clean();
    clean_libplug();

    UnloadImage(logo);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}