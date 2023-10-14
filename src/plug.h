#ifndef PLUG_H_
#define PLUG_H_

#include <raylib.h>
#include <stdlib.h>

typedef struct {
    size_t freq_bins;
    Music music;
} Plug;

#define LIST_OF_PLUGS \
    PLUG(plug_init) \
    PLUG(plug_update) \
    PLUG(plug_pre_reload) \
    PLUG(plug_post_reload)

typedef void (plug_init_t)(Plug* plug, const char* file_path);
typedef void (plug_update_t)(Plug* plug);
typedef void (plug_pre_reload_t)(Plug* plug);
typedef void (plug_post_reload_t)(Plug* plug);

#endif