#include "plug.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"

/* Types */
typedef enum {
    CIRCLE_FRAGMENT = 0,
    COUNT_FRAGMENTS
} FragmentFile;

typedef enum {
    CIRCLE_RADIUS_UNIFORM = 0,
    CIRCLE_POWER_UNIFORM,
    COUNT_UNIFORMS
} Uniform;

typedef enum {
    MODE_NONE = 0,            // 000
    MODE_REPEAT = 1,          // 001
    MODE_REPEAT1 = 2,         // 010
    MODE_SHUFFLE = 4,         // 100
    MODE_REPEAT_SHUFFLE = 5,  // 101
    MODE_REPEAT1_SHUFFLE = 6  // 110
} PlayMode;

typedef enum {
    TRACK_PREV = 0,
    TRACK_NEXT,
    TRACK_PLAY,
    TRACK_PAUSE,
} MusicControl;

typedef enum {
    BTN_NONE,
    BTN_HOVER,
    BTN_CLICKED
} ButtonState;

typedef struct {
    char* file_path;
    Music music;
} Track;

typedef struct {
    Track* items;
    size_t count;
    size_t capacity;
} Tracks;

typedef struct {
    const char* key;
    Image value;
} ImageItem;

typedef struct {
    ImageItem* items;
    size_t count;
    size_t capacity;
} Images;

typedef struct {
    const char* key;
    Texture2D value;
} TextureItem;

typedef struct {
    TextureItem* items;
    size_t count;
    size_t capacity;
} Textures;

typedef struct {
    Images images;
    Textures textures;
} Assets;

typedef struct {
    float lifetime;
    char* header;
    char* msg;
} Popup;

#define POPUP_CAPACITY 15
#define POPUP_GET(p, index) (assert(index < (p)->count), &(p)->items[((p)->begin + (index)) % POPUP_CAPACITY])
#define POPUP_FIRST(p) POPUP_GET((p), 0)
#define POPUP_LAST(p) POPUP_GET((p), (p)->count - 1)
typedef struct {
    Popup items[POPUP_CAPACITY];
    size_t begin;
    size_t count;
    float slide;
} Popups;

/* Forward Declarations */
// Assets Management
static Image assets_image(const char* file_path);
static Texture2D assets_texture(const char* file_path);
static void assets_unload(void);
// Active UI handlers
static int handle_btn(uint64_t id, Rectangle boundary);
// FFT and Audio Processing
static void fft_clean(void);
static void fft_clean_in(void);
static void fft(float in[], size_t stride, float complex out[], size_t n);
static size_t fft_proccess(float dt);
static void draw_texture_from_endpoints(Texture2D tex, Vector2 start_pos, Vector2 end_pos, float radius, Color c);
static void fft_render(Rectangle boundary, size_t m);
static void fft_push(float frame);
static void callback(void* bufferData, unsigned int frames);
// Track and Music Management
static Track* track_get_cur();
static Track* track_get_by_idx(int i);
static void track_remove(int i);
static void track_next_shuffle();
static void track_next_in_order();
static void track_prev();
static bool track_exists(const char* file_path);
static void track_stop_play();
static void track_prev_handle();
static void track_next_handle(bool by_user);
static void track_add(char* file_path);
static void music_play_pause();
static void music_volume_up();
static void music_volume_down();
static bool music_is_playing();
static void music_mute(float* prev_volume);
// Timeline UI renderer
static void timeline_render(Rectangle boundary, Track* track);
// Popup Management
static void popups_push(Popups* ps, char* header, char* msg);
static void popups_render(Popups* ps, Rectangle boundary, float dt);
// Fullscreen Button UI renderer
#define fullscreen_btn_render(boundary) fullscreen_btn_loc(__FILE__, __LINE__, boundary)
static int fullscreen_btn_loc(const char* file, int line, Rectangle boundary);
// Volume Control UI renderer
static void vert_slider_render(Rectangle boundary, float icon_size, float icon_margin, float* volume, bool* expanded);
#define volume_control_render(boundary) volume_control_loc(__FILE__, __LINE__, boundary)
static bool volume_control_loc(const char* file, int line, Rectangle boundary);
// Track Panel UI renderer
#define track_render(boundary, item, i) track_render_loc(__FILE__, __LINE__, boundary, item, i)
static void track_render_loc(const char* file, int line, Rectangle boundary, Rectangle item, int i);
static void tracks_render(Rectangle boundary, float item_size, float scroll_w, float panel_scroll);
static void track_panel_render(Rectangle boundary, float dt);
static void handle_scroll(Rectangle boundary, bool* scroll, float* scroll_offset, float* panel_velocity, float scrollable_area, float scroll_w, float panel_scroll, float item_size);
// Music Control UI renderer
#define music_options_render(boundary, mode, icon_pos) music_options_loc(__FILE__, __LINE__, boundary, mode, icon_pos)
static void music_options_loc(const char* file, int line, Rectangle boundary, PlayMode icon, int icon_pos);
#define music_control_render(boundary, icon, icon_pos) music_control_loc(__FILE__, __LINE__, boundary, icon, icon_pos)
static void music_control_loc(const char* file, int line, Rectangle boundary, MusicControl icon, int icon_pos);
// Helpers
static void str_fit_width(char* text, float width, float font_size, float text_pad);
static char* get_track_name(const char* file_path);
static Rectangle calculate_preview(void);
static void draw_icon(const char* file_path, int icon_id, int icon_cnt, Rectangle dest, Color c);

/* Constants */
// Fragment Files
#define CIRCLE_FS_FILEPATH "./resources/shaders/circle.fs"

// Images
#define FULLSCREEN_IMAGE_FILEPATH "./resources/images/fullscreen.png"
#define VOLUME_IMAGE_FILEPATH "./resources/images/volume.png"
#define MUSIC_OPTIONS_IMAGE_FILEPATH "./resources/images/music_options.png"
#define MUSIC_CONTROLS_IMAGE_FILEPATH "./resources/images/music_cntrls.png"
#define TRACK_DRAG_IMAGE_FILEPATH "./resources/images/track_drag.png"

// Controls
#define KEY_TOGGLE_PLAY KEY_SPACE
#define KEY_FULLSCREEN1 KEY_F
#define KEY_FULLSCREEN2 KEY_F11
#define KEY_TOGGLE_MUTE KEY_M
#define KEY_TRACK_REMOVE KEY_DELETE
#define KEY_TRACK_NEXT KEY_RIGHT
#define KEY_TRACK_PREV KEY_LEFT
#define KEY_VOLUME_UP KEY_UP
#define KEY_VOLUME_DOWN KEY_DOWN

// Parameters
#define FFT_SIZE (1 << 14)
#define FREQ_STEP 1.03f
#define LOW_FREQ 22.0f
#define SMOOTHNESS 20
#define SMEARNESS 5

#define BASE_WIDTH 1920.0f
#define BASE_HEIGHT 1080.0f

#define GENERAL_FONT_SIZE 50.0f
#define TRACK_NAME_FONT_SIZE 15.0f
#define HUD_POPUP_FONT_SIZE 15.0f

#define PANEL_PERCENT 0.25f
#define TIMELINE_PERCENT 0.1f
#define SCROLL_PERCENT 0.02f
#define TRACK_ITEM_PERCENT 0.2f
#define VELOCITY_DECAY 0.9f

#define HUD_TIMER_SECS 2.0f
#define UI_NEXT_TRACK_TIMER_SECS 0.4f
#define HUD_ICON_SIZE_BASE 70.0f
#define HUD_ICON_MARGIN_BASE 20.0f
#define HUD_VOLUME_SEGMENTS 4.0f
#define HUD_VOLUME_STEPS 0.1f
#define HUD_EDGE_WIDTH 2.0f
#define HUD_POPUP_LIFETIME_SECS 2.5f
#define HUD_POPUP_SLIDEIN_SECS 0.1f
#define HUD_POPUP_WIDTH 300.0f
#define HUD_POPUP_HEIGHT 50.0f
#define HUD_POPUP_PAD 10.0f

#define HSV_SATURATION 0.75f
#define HSV_VALUE 1.0f

#define TWO_PI 2 * PI

#define COLOR_ACCENT ColorFromHSV(200, 0.75, 0.8)
#define COLOR_BACKGROUND GetColor(0x0F172AFF)
#define COLOR_TRACK_PANEL_BACKGROUND ColorBrightness(COLOR_BACKGROUND, -0.3)
#define COLOR_TRACK_BUTTON_BACKGROUND ColorBrightness(COLOR_BACKGROUND, 0.15)
#define COLOR_TRACK_BUTTON_HOVEROVER ColorBrightness(COLOR_TRACK_BUTTON_BACKGROUND, 0.15)
#define COLOR_TRACK_BUTTON_SELECTED COLOR_ACCENT
#define COLOR_TIMELINE_CURSOR COLOR_ACCENT
#define COLOR_TIMELINE_BACKGROUND ColorBrightness(COLOR_BACKGROUND, -0.3)
#define COLOR_HUD_BTN_BACKGROUND ColorBrightness(COLOR_BACKGROUND, 0.15)
#define COLOR_HUD_BTN_HOVEROVER ColorBrightness(COLOR_HUD_BTN_BACKGROUND, 0.15)
#define COLOR_POPUP_BACKGROUND ColorBrightness(COLOR_BACKGROUND, 0.2)

static_assert(COUNT_UNIFORMS == 2, "Update list of uniform names");
const char* uniform_names[COUNT_UNIFORMS] = {
    [CIRCLE_RADIUS_UNIFORM] = "radius",
    [CIRCLE_POWER_UNIFORM] = "power"};

static_assert(COUNT_FRAGMENTS == 1, "Update list of fragment file paths");
const char* fragment_files[COUNT_FRAGMENTS] = {
    [CIRCLE_FRAGMENT] = CIRCLE_FS_FILEPATH,
};

typedef struct {
    Tracks tracks;
    int cur_track;
    bool music_is_paused;
    float volume;
    PlayMode mode;

    Shader circle;
    int uniform_locs[COUNT_UNIFORMS];
    bool fullscreen;
    uint64_t active_btn_id;

    Assets assets;
    Popups popups;

    float in_raw[FFT_SIZE];
    float in_windowed[FFT_SIZE];
    float complex out_raw[FFT_SIZE];
    float out_logscaled[FFT_SIZE];
    float out_smoothed[FFT_SIZE];
    float out_smeared[FFT_SIZE];
    float hann[FFT_SIZE];
} Plug;

static Plug* p = NULL;

/* Assets Management */
static Image assets_image(const char* file_path) {
    Image* image = assoc_find(p->assets.images, file_path);
    if (image) return *image;

    ImageItem item = {0};
    item.key = file_path;
    item.value = LoadImage(file_path);
    da_append(&p->assets.images, item);
    return item.value;
}

static Texture2D assets_texture(const char* file_path) {
    Texture2D* texture = assoc_find(p->assets.textures, file_path);
    if (texture) return *texture;

    Image image = assets_image(file_path);

    TextureItem item = {0};
    item.key = file_path;
    item.value = LoadTextureFromImage(image);
    GenTextureMipmaps(&item.value);
    SetTextureFilter(item.value, TEXTURE_FILTER_BILINEAR);
    da_append(&p->assets.textures, item);
    return item.value;
}

static void assets_unload() {
    for (size_t i = 0; i < p->assets.textures.count; i++) {
        UnloadTexture(p->assets.textures.items[i].value);
    }
    p->assets.textures.count = 0;
    for (size_t i = 0; i < p->assets.images.count; i++) {
        UnloadImage(p->assets.images.items[i].value);
    }
    p->assets.images.count = 0;
}

/* Active UI handlers */
static int handle_btn(uint64_t id, Rectangle boundary) {
    Vector2 mouse = GetMousePosition();
    int hover = IsCursorOnScreen() && CheckCollisionPointRec(mouse, boundary);
    int clicked = 0;

    if (p->active_btn_id == 0) {
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            p->active_btn_id = id;
    } else if (p->active_btn_id == id) {
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            p->active_btn_id = 0;
            if (hover) clicked = 1;
        }
    }

    return (clicked << 1) | hover;
}

/* FFT and Audio Processing */
static void fft_clean(void) {
    memset(p->in_raw, 0, sizeof(p->in_raw));
    memset(p->in_windowed, 0, sizeof(p->in_windowed));
    memset(p->out_raw, 0, sizeof(p->out_raw));
    memset(p->out_logscaled, 0, sizeof(p->out_logscaled));
    memset(p->out_smoothed, 0, sizeof(p->out_smoothed));
    memset(p->out_smeared, 0, sizeof(p->out_smeared));
}

static void fft_clean_in(void) {
    memset(p->in_raw, 0, sizeof(p->in_raw));
    memset(p->in_windowed, 0, sizeof(p->in_windowed));
}

static void fft(float in[], size_t stride, float complex out[], size_t n) {
    if (n == 1) {
        out[0] = in[0];
        return;
    }

    fft(in, stride * 2, out, n / 2);
    fft(in + stride, stride * 2, out + n / 2, n / 2);

    for (size_t k = 0; k < n / 2; ++k) {
        float t = (float)k / n;
        float complex v = cexp(-2 * I * PI * t) * out[k + n / 2];
        float complex e = out[k];
        out[k] = e + v;
        out[k + n / 2] = e - v;
    }
}

static size_t fft_proccess(float dt) {
    size_t m = 0;
    float max_amp = 1.0f;

    // Hann Windowing
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        p->in_windowed[i] = p->in_raw[i] * p->hann[i];
    }

    // Perform FFT
    fft(p->in_windowed, 1, p->out_raw, FFT_SIZE);

    for (float f = LOW_FREQ; (size_t)f < FFT_SIZE / 2; f = ceilf(f * FREQ_STEP)) {
        float f1 = ceilf(f * FREQ_STEP);
        float ampl = 0.0f;

        for (size_t q = (size_t)f; q < FFT_SIZE / 2 && q < (size_t)f1; ++q) {
            float a = amp(p->out_raw[q]);
            ampl = fmaxf(ampl, a);
        }

        max_amp = fmaxf(max_amp, ampl);
        p->out_logscaled[m++] = ampl;
    }

    for (size_t i = 0; i < m; ++i) {
        p->out_logscaled[i] /= max_amp;                                                      // Normalize
        p->out_smoothed[i] += (p->out_logscaled[i] - p->out_smoothed[i]) * SMOOTHNESS * dt;  // Smooth
        p->out_smeared[i] += (p->out_smoothed[i] - p->out_smeared[i]) * SMEARNESS * dt;      // Smear
    }

    return m;
}

static void draw_texture_from_endpoints(Texture2D tex, Vector2 start_pos, Vector2 end_pos, float radius, Color c) {
    Rectangle dest, source;

    dest.width = 2 * radius;

    if (end_pos.y >= start_pos.y) {
        dest.x = start_pos.x - radius;
        dest.y = start_pos.y;
        dest.height = end_pos.y - start_pos.y;
        source = (Rectangle){0, 0.5, 1, 0.5};
    } else {
        dest.x = end_pos.x - radius;
        dest.y = end_pos.y;
        source = (Rectangle){0, 0, 1, 0.5};
        dest.height = start_pos.y - end_pos.y;
    }

    DrawTexturePro(tex, source, dest, CLITERAL(Vector2){0}, 0, c);
}

static void fft_render(Rectangle boundary, size_t m) {
    float h = boundary.height;
    float w = boundary.width;
    float cell_width = roundf(w / m);

    // Draw Bars and Circles
    for (size_t i = 0; i < m; ++i) {
        float t_smooth = p->out_smoothed[i];
        float t_smear = p->out_smeared[i];

        float hue = 170;  //(float)i / m * 360;
        Color c = ColorFromHSV(hue, HSV_SATURATION, HSV_VALUE);

        float thick = roundf(cell_width / 3);
        float radius = 3 * cell_width * sqrtf(t_smooth);

        Texture2D texture = {rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};

        Vector2 middlePos = {boundary.x + i * cell_width + cell_width / 2, h / 2};
        Vector2 start_pos_t = {middlePos.x, h / 2 - h / 3 * t_smooth};
        Vector2 start_pos_b = {middlePos.x, h / 2 + h / 3 * t_smooth};

        // Draw Bars
        DrawLineEx(start_pos_t, middlePos, thick, c);
        DrawLineEx(start_pos_b, middlePos, thick, c);

        // Draw Smear
        SetShaderValue(p->circle, p->uniform_locs[CIRCLE_RADIUS_UNIFORM], (float[1]){0.3f}, SHADER_UNIFORM_FLOAT);
        SetShaderValue(p->circle, p->uniform_locs[CIRCLE_POWER_UNIFORM], (float[1]){2.0f}, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(p->circle);
        {
            float radius = cell_width * sqrtf(t_smooth);
            Vector2 end_pos_t = {middlePos.x, h / 2 - h / 3 * t_smear};
            Vector2 end_pos_b = {middlePos.x, h / 2 + h / 3 * t_smear};
            draw_texture_from_endpoints(texture, start_pos_t, end_pos_t, radius, c);
            draw_texture_from_endpoints(texture, start_pos_b, end_pos_b, radius, c);
        }
        EndShaderMode();

        // Draw Circles
        SetShaderValue(p->circle, p->uniform_locs[CIRCLE_RADIUS_UNIFORM], (float[1]){0.08f}, SHADER_UNIFORM_FLOAT);
        SetShaderValue(p->circle, p->uniform_locs[CIRCLE_POWER_UNIFORM], (float[1]){4.0f}, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(p->circle);
        {
            Vector2 pos_t = {.x = start_pos_t.x - radius, .y = start_pos_t.y - radius};
            Vector2 pos_b = {.x = start_pos_b.x - radius, .y = start_pos_b.y - radius};
            DrawTextureEx(texture, pos_t, 0, 2 * radius, c);
            DrawTextureEx(texture, pos_b, 0, 2 * radius, c);
        }
        EndShaderMode();
    }
}

static void fft_push(float frame) {
    memmove(p->in_raw, p->in_raw + 1, (FFT_SIZE - 1) * sizeof(p->in_raw[0]));
    p->in_raw[FFT_SIZE - 1] = frame;
}

static void callback(void* bufferData, unsigned int frames) {
    float(*fs)[2] = bufferData;

    for (size_t i = 0; i < frames; ++i) {
        fft_push(fs[i][0]);
    }
}

/* Track and Music Management */
static Track* track_get_cur() {
    da_get_by_idx(&p->tracks, p->cur_track);
}

static Track* track_get_by_idx(int i) {
    da_get_by_idx(&p->tracks, i);
}

static void track_remove(int i) {
    Track* track = track_get_by_idx(i);
    if (track == NULL) return;

    DetachAudioStreamProcessor(track->music.stream, callback);
    UnloadMusicStream(track->music);
    free(track->file_path);
    da_remove(&p->tracks, i);

    if (i < p->cur_track) p->cur_track = p->cur_track - 1;
}

static void track_next_in_order() {
    int i = (p->cur_track + 1) % p->tracks.count;

    Track* track = track_get_cur();
    if (track) StopMusicStream(track->music);
    PlayMusicStream(p->tracks.items[i].music);
    p->cur_track = i;
}

static void track_stop_play() {
    Track* track = track_get_cur();
    if (!track) return;
    if (IsMusicStreamPlaying(track->music)) {
        StopMusicStream(track->music);
        p->music_is_paused = true;
    }
}

static void track_next_handle(bool by_user) {
    size_t tracks_cnt = p->tracks.count;
    size_t cur_track = p->cur_track;
    bool is_last = cur_track == tracks_cnt - 1 || tracks_cnt == 1;
    Track* track = track_get_cur();
    if (!track) return;

    // TODO: fix this
    // SHUFFLE -> last song stops
    // SHUFFLE -> shuffle list instead of playing random song

    switch (p->mode) {
        case MODE_REPEAT1_SHUFFLE:
            if (is_last) {
                if (by_user)
                    track_stop_play();
                else
                    PlayMusicStream(track->music);
            } else {
                if (by_user)
                    track_next_shuffle();
                else
                    PlayMusicStream(track->music);
            }
            break;
        case MODE_REPEAT1:
            if (is_last) {
                if (by_user)
                    track_stop_play();
                else
                    track_next_in_order();

            } else {
                if (by_user)
                    track_next_in_order();
                else
                    PlayMusicStream(track->music);
            }
            break;
        case MODE_REPEAT:
            track_next_in_order();
            break;
        case MODE_SHUFFLE:
                track_next_shuffle();
            break;
        default:
            if (is_last)
                track_stop_play();
            else
                track_next_in_order();
            break;
    }
}

static void track_prev_handle() {
    Track* track = track_get_cur();
    if (!track) return;

    if (GetMusicTimePlayed(track->music) < 5.0f && p->tracks.count > 1) {
        if ((p->mode & MODE_SHUFFLE && !(p->mode & MODE_REPEAT1)) || p->cur_track > 0) {
            track_prev();
        } else {
            if (p->mode & MODE_REPEAT) {
                Track* track = track_get_cur();
                if (track) StopMusicStream(track->music);
                PlayMusicStream(p->tracks.items[p->tracks.count - 1].music);
                p->cur_track = p->tracks.count - 1;
            } else {
                SeekMusicStream(track->music, 0.0f);
            }
        }
    } else {
        SeekMusicStream(track->music, 0.0f);
    }
}

static void track_prev() {
    if (p->cur_track == 0) {
        track_stop_play();
    } else {
        Track* track = track_get_cur();
        if (track) StopMusicStream(track->music);
        PlayMusicStream(p->tracks.items[p->cur_track - 1].music);
        p->cur_track -= 1;
    }
}

static void track_next_shuffle() {
    if (p->tracks.count == 1) return;

    int i = p->cur_track;
    while (i == p->cur_track) i = GetRandomValue(0, p->tracks.count - 1);
    Track* track = track_get_cur();
    if (track) StopMusicStream(track->music);
    PlayMusicStream(p->tracks.items[i].music);
    p->cur_track = i;
}

static bool track_exists(const char* file_path) {
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track* track = &p->tracks.items[i];
        if (strcmp(track->file_path, file_path) == 0) return true;
    }
    return false;
}

static void track_add(char* file_path) {
    Music music = LoadMusicStream(file_path);
    music.looping = false;

    if (IsMusicReady(music)) {
        SetMusicVolume(music, p->volume);
        AttachAudioStreamProcessor(music.stream, callback);
        da_append(&p->tracks, (CLITERAL(Track){.file_path = file_path, .music = music}));
    } else {
        char* msg = get_track_name(file_path);
        str_fit_width(msg, HUD_POPUP_WIDTH, HUD_POPUP_FONT_SIZE, HUD_POPUP_PAD);
        char* header = strdup("Could not load the track");
        popups_push(&p->popups, header, msg);
        free(file_path);
    }
}

static void music_mute(float* prev_volume) {
    if (p->volume != 0.0f) *prev_volume = p->volume;
    p->volume = p->volume == 0.0f ? *prev_volume : 0.0f;
}

static bool music_is_playing() {
    Track* track = track_get_cur();
    if (track) return IsMusicStreamPlaying(track->music);
    return false;
}

static void music_play_pause() {
    Track* track = track_get_cur();
    if (!track) return;
    if (!p->music_is_paused && IsMusicStreamPlaying(track->music)) {
        PauseMusicStream(track->music);
        p->music_is_paused = true;
    } else {
        PlayMusicStream(track->music);
        ResumeMusicStream(track->music);
        p->music_is_paused = false;
    }
}

static void music_volume_up() {
    p->volume += HUD_VOLUME_STEPS;
    if (p->volume > 1.0f) p->volume = 1.0f;
}

static void music_volume_down() {
    p->volume -= HUD_VOLUME_STEPS;
    if (p->volume < 0.0f) p->volume = 0.0f;
}

/* Timeline UI renderer */
static void timeline_render(Rectangle boundary, Track* track) {
    float played = GetMusicTimePlayed(track->music);
    float len = GetMusicTimeLength(track->music);
    float progress = played / len * GetScreenWidth();

    Vector2 start_pos = {progress, boundary.y};
    Vector2 end_pos = {progress, boundary.y + boundary.height};

    // Draw time elapsed and whole time of the track in each corner of the timeline
    const char* time_elapsed = TextFormat("%02i:%02i", (int)played / 60, (int)played % 60);
    const char* time_whole = TextFormat("%02i:%02i", (int)len / 60, (int)len % 60);
    float font_size = boundary.height * 0.3f;
    float text_pad = ceilf(boundary.width * 0.02f);
    int text_w = MeasureText(time_whole, font_size);
    float pos_y = boundary.y + boundary.height / 2 - font_size / 2;

    BeginScissorMode(boundary.x, boundary.y, boundary.width, boundary.height + HUD_EDGE_WIDTH);
    {
        ClearBackground(COLOR_TIMELINE_BACKGROUND);
        DrawText(time_elapsed, boundary.x + text_pad, pos_y, font_size, WHITE);
        DrawText(time_whole, boundary.x + boundary.width - text_pad - text_w, pos_y, font_size, WHITE);

        DrawLineEx(start_pos, end_pos, 2, COLOR_TIMELINE_CURSOR);                       // Draw Cursor
        DrawRectangleLinesEx(boundary, HUD_EDGE_WIDTH, COLOR_TRACK_BUTTON_BACKGROUND);  // Draw Contour

        Vector2 mouse = GetMousePosition();
        if (CheckCollisionPointRec(mouse, boundary)) {
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
                float t = (mouse.x - boundary.x) / boundary.width;
                SeekMusicStream(track->music, t * len);
            }
        }
    }
    EndScissorMode();
}

/* Popup Management */
static void popups_push(Popups* ps, char* header, char* msg) {
    if (ps->count < POPUP_CAPACITY) {
        if (ps->begin == 0) {
            ps->begin = POPUP_CAPACITY - 1;
        } else {
            ps->begin -= 1;
        }

        ps->count += 1;

        POPUP_FIRST(ps)->header = header;
        POPUP_FIRST(ps)->msg = msg;
        ps->slide += HUD_POPUP_SLIDEIN_SECS;
        POPUP_FIRST(ps)->lifetime = HUD_POPUP_LIFETIME_SECS + ps->slide;
    }
}

static void popups_render(Popups* ps, Rectangle boundary, float dt) {
    if (ps->slide > 0) ps->slide -= dt;
    if (ps->slide < 0) ps->slide = 0;

    for (size_t i = 0; i < ps->count; i++) {
        Popup* it = POPUP_GET(ps, i);
        it->lifetime -= dt;

        float t = it->lifetime / HUD_POPUP_LIFETIME_SECS;
        float alpha = t >= 0.5f ? 1.0f : t / 0.5f;
        float q = ps->slide / HUD_POPUP_SLIDEIN_SECS;

        Rectangle popup = {
            .x = boundary.x + HUD_POPUP_PAD,
            .y = boundary.y + HUD_POPUP_PAD + (i + q) * (HUD_POPUP_HEIGHT + HUD_POPUP_PAD),
            .width = HUD_POPUP_WIDTH,
            .height = HUD_POPUP_HEIGHT,
        };

        DrawRectangleRounded(popup, 0.3, 20, ColorAlpha(COLOR_POPUP_BACKGROUND, alpha));
        Color c = ColorAlpha(WHITE, alpha);
        int width = MeasureText(it->header, HUD_POPUP_FONT_SIZE + 2);
        DrawText(it->header, popup.x + popup.width / 2 - width / 2, popup.y + HUD_POPUP_PAD / 2, HUD_POPUP_FONT_SIZE + 1, c);
        width = MeasureText(it->msg, HUD_POPUP_FONT_SIZE);
        DrawText(it->msg, popup.x + popup.width / 2 - width / 2, popup.y + HUD_POPUP_PAD / 2 + HUD_POPUP_FONT_SIZE, HUD_POPUP_FONT_SIZE, c);
    }

    while (ps->count > 0 && POPUP_LAST(ps)->lifetime <= 0) {
        free(POPUP_LAST(ps)->msg);
        free(POPUP_LAST(ps)->header);
        ps->count -= 1;
    }
}

/* Fullscreen Button UI renderer */
static int fullscreen_btn_loc(const char* file, int line, Rectangle boundary) {
    int icon_cnt = 4;
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    float icon_margin = HUD_ICON_MARGIN_BASE * boundary.height / BASE_HEIGHT;
    float icon_size = HUD_ICON_SIZE_BASE * boundary.height / BASE_HEIGHT;

    int icon_id;
    Rectangle btn = {boundary.x + boundary.width - (icon_size + icon_margin), boundary.y + icon_margin, icon_size, icon_size};
    int state = handle_btn(id, btn);

    Color c = state & BTN_HOVER ? COLOR_HUD_BTN_HOVEROVER : COLOR_HUD_BTN_BACKGROUND;
    if (state & BTN_HOVER) {
        icon_id = p->fullscreen ? 3 : 1;
    } else {
        icon_id = p->fullscreen ? 2 : 0;
    }

    draw_icon(FULLSCREEN_IMAGE_FILEPATH, icon_id, icon_cnt, btn, c);

    return state;
}

/* Volume Control UI renderer */
static void vert_slider_render(Rectangle boundary, float icon_size, float icon_margin, float* volume, bool* expanded) {
    Vector2 mouse = GetMousePosition();

    static bool dragging = false;

    Color c = COLOR_HUD_BTN_HOVEROVER;
    float segments = HUD_VOLUME_SEGMENTS;

    float slider_w = icon_size / 4.5;
    float slider_h = (segments - 1) * icon_size;
    float slider_radius = slider_w;
    float cutoff = boundary.height - (slider_h + 1.5f * icon_margin);

    Rectangle slider_bar = {boundary.x + boundary.width / 2 - slider_w / 2, boundary.y + icon_margin, slider_w, slider_h};
    Vector2 slider_pos = {slider_bar.x + slider_bar.width / 2, slider_bar.y + slider_bar.height * (1 - p->volume)};
    Rectangle slider_boundary = {boundary.x, boundary.y, boundary.width, boundary.height - cutoff};

    DrawRectangleRounded(slider_bar, 0.8, 40, c);
    DrawCircleV(slider_pos, slider_radius, COLOR_ACCENT);

    if (!dragging) {
        if (CheckCollisionPointCircle(mouse, slider_pos, slider_radius)) {
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) dragging = true;
        }
        if (CheckCollisionPointRec(mouse, slider_boundary)) {
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                *volume = slider_get_value(mouse.y, slider_bar.y, slider_bar.y + slider_bar.height);
            }
        }
    } else {
        *volume = slider_get_value(mouse.y, slider_bar.y, slider_bar.y + slider_bar.height);
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) dragging = false;
    }

    *expanded = dragging || (IsCursorOnScreen() && CheckCollisionPointRec(mouse, boundary));
}

static bool volume_control_loc(const char* file, int line, Rectangle boundary) {
    Vector2 mouse = GetMousePosition();
    int icon_cnt = 3;

    static bool expanded = false;
    static float prev_volume = 0.5f;

    float segments = HUD_VOLUME_SEGMENTS;
    float icon_margin = HUD_ICON_MARGIN_BASE * boundary.height / BASE_HEIGHT;
    float icon_size = HUD_ICON_SIZE_BASE * boundary.height / BASE_HEIGHT;

    float btn_offset = icon_size + icon_margin;
    float full_margin = icon_margin * 2;

    Rectangle volume_box = {
        boundary.x + boundary.width - (icon_size + full_margin),
        boundary.y + boundary.height - (icon_size + full_margin),
        icon_size + full_margin,
        icon_size + full_margin,
    };
    Rectangle btn = {boundary.x + boundary.width - btn_offset, boundary.y + boundary.height - btn_offset, icon_size, icon_size};

    if (IsCursorOnScreen() && CheckCollisionPointRec(mouse, volume_box)) expanded = true;

    if (expanded) {
        volume_box.height = segments * icon_size + full_margin + 1.5f * icon_margin;
        volume_box.y -= (segments - 1) * icon_size + 1.5f * icon_margin;
        vert_slider_render(volume_box, icon_size, icon_margin, &p->volume, &expanded);
        p->volume += GetMouseWheelMove() * HUD_VOLUME_STEPS;
        if (p->volume < 0.0f) p->volume = 0.0f;
        if (p->volume > 1.0f) p->volume = 1.0f;
    }

    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    if (IsKeyPressed(KEY_TOGGLE_MUTE) || handle_btn(id, btn) & BTN_CLICKED) music_mute(&prev_volume);

    Color c = expanded ? COLOR_HUD_BTN_HOVEROVER : COLOR_HUD_BTN_BACKGROUND;
    int icon_id = icon_id = (p->volume > 0.5f) ? 2 : ((p->volume == 0.0f) ? 0 : 1);

    draw_icon(VOLUME_IMAGE_FILEPATH, icon_id, icon_cnt, btn, c);

    return expanded;
}

/* Track Panel UI renderer */
static void track_render_loc(const char* file, int line, Rectangle boundary, Rectangle item, int i) {
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    Color c;
    if (i != p->cur_track) {
        uint64_t item_id = djb2(id, &i, sizeof(i));
        int state = handle_btn(item_id, GetCollisionRec(boundary, item));

        if (state & BTN_HOVER) {
            c = COLOR_TRACK_BUTTON_HOVEROVER;
            if (IsKeyPressed(KEY_TRACK_REMOVE)) track_remove(i);
        } else {
            c = COLOR_TRACK_BUTTON_BACKGROUND;
        }
        if (state & BTN_CLICKED) {
            Track* track = track_get_cur();
            if (track) StopMusicStream(track->music);
            PlayMusicStream(p->tracks.items[i].music);
            p->cur_track = i;
        }
    } else {
        c = COLOR_TRACK_BUTTON_SELECTED;
    }
    DrawRectangleRounded(item, 0.2, 20, c);

    // TODO: introduce track dragging to change order
    float icon_size = (HUD_ICON_SIZE_BASE * boundary.height / BASE_HEIGHT) / 2;
    float icon_margin = (HUD_ICON_MARGIN_BASE * boundary.height / BASE_HEIGHT) / 2;
    Rectangle drag = {item.x + item.width - (icon_size + icon_margin), item.y + item.height / 2 - icon_size / 2, icon_size, icon_size};

    draw_icon(TRACK_DRAG_IMAGE_FILEPATH, 0, 1, drag, BLACK);

    float font_size = TRACK_NAME_FONT_SIZE + item.height * 0.1f;
    float text_pad = item.width * 0.05f;
    char* track_name = get_track_name(p->tracks.items[i].file_path);
    str_fit_width(track_name, item.width - (icon_size + icon_margin * 2), font_size, text_pad);

    DrawText(track_name, item.x + text_pad, item.y + item.height / 2 - font_size / 2, font_size, BLACK);
    free(track_name);
}

static void tracks_render(Rectangle boundary, float item_size, float scroll_w, float panel_scroll) {
    for (size_t i = 0; i < p->tracks.count; ++i) {
        float panel_pad = item_size * 0.05f;

        Rectangle item = {
            .x = boundary.x + panel_pad - 2,
            .y = i * item_size + boundary.y + panel_pad - panel_scroll,
            .width = boundary.width - 2 * panel_pad - scroll_w,
            .height = item_size - 2 * panel_pad,
        };

        track_render(boundary, item, (int)i);
    }
}

static void handle_scroll(Rectangle boundary, bool* scroll, float* scroll_offset, float* panel_velocity, float scrollable_area, float scroll_w, float panel_scroll, float item_size) {
    Vector2 mouse = GetMousePosition();

    if (scrollable_area > boundary.height) {
        float t = boundary.height / scrollable_area;
        float q = panel_scroll / scrollable_area;
        Rectangle scrollbar_area = {
            .x = boundary.x + boundary.width - scroll_w - HUD_EDGE_WIDTH,
            .y = boundary.y,
            .width = scroll_w,
            .height = boundary.height,
        };

        Rectangle scrollbar_boundary = {
            .x = boundary.x + boundary.width - scroll_w - HUD_EDGE_WIDTH,
            .y = boundary.y + boundary.height * q,
            .width = scroll_w,
            .height = boundary.height * t,
        };
        DrawRectangleRounded(scrollbar_boundary, 0.8, 20, COLOR_ACCENT);

        if (*scroll) {
            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) *scroll = false;
        } else {
            if (CheckCollisionPointRec(mouse, scrollbar_boundary)) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    *scroll = true;
                    *scroll_offset = mouse.y - scrollbar_boundary.y;
                }
            } else if (CheckCollisionPointRec(mouse, scrollbar_area)) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    if (mouse.y < scrollbar_boundary.y) {
                        *panel_velocity += item_size * 16;
                    } else if (scrollbar_boundary.y + scrollbar_boundary.height < mouse.y) {
                        *panel_velocity += -item_size * 16;
                    }
                }
            }
        }
    }
}

static void track_panel_render(Rectangle boundary, float dt) {
    Vector2 mouse = GetMousePosition();

    static bool scroll = false;
    static float scroll_offset = 0.0f;

    size_t track_count = p->tracks.count;
    float tracks_offset = (3 * HUD_ICON_MARGIN_BASE + 2 * HUD_ICON_SIZE_BASE) * boundary.height / BASE_HEIGHT;
    float scroll_w = boundary.width * SCROLL_PERCENT - HUD_EDGE_WIDTH;
    float item_size = boundary.width * TRACK_ITEM_PERCENT;
    float scrollable_area = item_size * track_count;

    static float panel_velocity = 0.0f;
    if (CheckCollisionPointRec(mouse, boundary))
        panel_velocity = panel_velocity * VELOCITY_DECAY + GetMouseWheelMove() * item_size * 8;

    float max_scroll = scrollable_area - boundary.height + tracks_offset;
    static float panel_scroll = 0.0f;
    panel_scroll -= panel_velocity * dt;

    if (scroll)
        panel_scroll = (mouse.y - boundary.y - tracks_offset - scroll_offset) / (boundary.height + tracks_offset) * scrollable_area;
    if (panel_scroll < 0) panel_scroll = 0;
    if (max_scroll < 0) max_scroll = 0;
    if (panel_scroll > max_scroll) panel_scroll = max_scroll;

    BeginScissorMode(boundary.x, boundary.y, boundary.width, boundary.height);
    {
        ClearBackground(COLOR_TRACK_PANEL_BACKGROUND);

        music_options_render(boundary, p->mode & MODE_REPEAT1 ? MODE_REPEAT1 : MODE_REPEAT, 0);  // Draw repeat
        music_options_render(boundary, MODE_SHUFFLE, 1);                                         // Draw shuffle
        music_control_render(boundary, TRACK_PREV, 0);                                           // Draw prev
        music_control_render(boundary, music_is_playing() ? TRACK_PAUSE : TRACK_PLAY, 1);        // Draw pause
        music_control_render(boundary, TRACK_NEXT, 2);                                           // Draw next

        Rectangle tracks_boundary = {boundary.x, boundary.y + tracks_offset, boundary.width, boundary.height - tracks_offset};
        BeginScissorMode(tracks_boundary.x, tracks_boundary.y, tracks_boundary.width, tracks_boundary.height);
        {
            handle_scroll(tracks_boundary, &scroll, &scroll_offset, &panel_velocity, scrollable_area, scroll_w, panel_scroll, item_size);
            tracks_render(tracks_boundary, item_size, scroll_w, panel_scroll);
        }
        EndScissorMode();

        Vector2 edge_start_pos = {boundary.x, boundary.y + tracks_offset};
        Vector2 edge_end_pos = {boundary.x + boundary.width, edge_start_pos.y};
        DrawLineEx(edge_start_pos, edge_end_pos, HUD_EDGE_WIDTH, COLOR_TRACK_BUTTON_BACKGROUND);
    }
    EndScissorMode();

    Vector2 edge_start_pos = {boundary.x + boundary.width, boundary.y};
    Vector2 edge_end_pos = {edge_start_pos.x, boundary.y + boundary.height};
    DrawLineEx(edge_start_pos, edge_end_pos, HUD_EDGE_WIDTH, COLOR_TRACK_BUTTON_BACKGROUND);
}

/* Music Control UI renderer */
static void music_options_loc(const char* file, int line, Rectangle boundary, PlayMode icon, int icon_pos) {
    int icon_cnt = 2;
    int total_icon_cnt = 3;
    int icon_id = icon > MODE_REPEAT1 ? icon - MODE_REPEAT1 : icon - MODE_REPEAT;  // Get to 0, 1, 2

    float icon_margin = HUD_ICON_MARGIN_BASE * boundary.height / BASE_HEIGHT;
    float icon_size = HUD_ICON_SIZE_BASE * boundary.height / BASE_HEIGHT;

    float gap_size = (boundary.width - HUD_EDGE_WIDTH - icon_cnt * icon_size) / (icon_cnt + 1);
    float btn_x = boundary.x + icon_pos * icon_size + (icon_pos + 1) * gap_size;

    Color c = COLOR_HUD_BTN_BACKGROUND;
    Rectangle btn = {btn_x, boundary.y + icon_margin, icon_size, icon_size};

    if (p->mode & icon) {
        c = COLOR_HUD_BTN_HOVEROVER;
        Rectangle rec = {btn.x - icon_margin / 2, btn.y - icon_margin / 2, btn.width + icon_margin, btn.height + icon_margin};
        DrawRectangleRounded(rec, 0.3, 20, COLOR_TRACK_BUTTON_SELECTED);
    }

    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));
    int state = handle_btn(id, btn);

    if (state & BTN_HOVER) c = COLOR_HUD_BTN_HOVEROVER;
    if (state & BTN_CLICKED) {
        if (p->mode & MODE_REPEAT && icon == MODE_REPEAT) {
            p->mode = (p->mode & ~MODE_REPEAT) | MODE_REPEAT1;
        } else if (p->mode & MODE_REPEAT1 && icon == MODE_REPEAT) {
            p->mode = p->mode & ~MODE_REPEAT1;
        } else {
            if (p->mode & icon) {
                p->mode = p->mode & ~icon;
            } else {
                p->mode = p->mode | icon;
            }
        }
    }

    draw_icon(MUSIC_OPTIONS_IMAGE_FILEPATH, icon_id, total_icon_cnt, btn, c);
}

static void music_control_loc(const char* file, int line, Rectangle boundary, MusicControl icon, int icon_pos) {
    int icon_cnt = 3;
    int total_icon_cnt = 4;
    int icon_id = icon;

    float icon_margin = HUD_ICON_MARGIN_BASE * boundary.height / BASE_HEIGHT;
    float icon_size = HUD_ICON_SIZE_BASE * boundary.height / BASE_HEIGHT;

    float gap_size = (boundary.width - HUD_EDGE_WIDTH - icon_cnt * icon_size) / (icon_cnt + 1);
    float btn_x = boundary.x + icon_pos * icon_size + (icon_pos + 1) * gap_size;

    Color c = COLOR_HUD_BTN_BACKGROUND;
    Rectangle btn = {btn_x, boundary.y + 2 * icon_margin + icon_size, icon_size, icon_size};

    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));
    int state = handle_btn(id, btn);

    if (state & BTN_HOVER) c = COLOR_HUD_BTN_HOVEROVER;
    if (state & BTN_CLICKED) {
        switch (icon) {
            case TRACK_PREV:
                track_prev_handle();
                break;
            case TRACK_PLAY:
            case TRACK_PAUSE:
                music_play_pause();
                break;
            case TRACK_NEXT:
                track_next_handle(true);
                break;
        }
    }

    draw_icon(MUSIC_CONTROLS_IMAGE_FILEPATH, icon_id, total_icon_cnt, btn, c);
}

/* Helpers */
static void str_fit_width(char* text, float width, float font_size, float text_pad) {
    size_t original_len = strlen(text);
    int text_w = MeasureText(text, font_size);
    int ellipsis_w = MeasureText("...", font_size);

    while (text_w + ellipsis_w > width - 2 * text_pad && strlen(text) > 0) {
        text[strlen(text) - 1] = '\0';
        text_w = MeasureText(text, font_size);
    }

    if (strlen(text) < original_len) strcat(text, "...");
}

static char* get_track_name(const char* file_path) {
    const char* orig = GetFileName(file_path);
    char* track_name = strdup(orig);
    remove_extension(track_name);

    return track_name;
}

static Rectangle calculate_preview() {
    int w = GetScreenWidth();
    int h = GetScreenHeight();

    if (p->fullscreen) {
        return (Rectangle){0, 0, w, h};
    } else {
        float tracks_panel_w = w * PANEL_PERCENT;
        float timeline_h = h * TIMELINE_PERCENT;
        return (Rectangle){tracks_panel_w, 0, w - tracks_panel_w, h - timeline_h};
    }
}

static void draw_icon(const char* file_path, int icon_id, int icon_cnt, Rectangle dest, Color c) {
    Texture2D tex = assets_texture(file_path);
    Rectangle source = (Rectangle){tex.width / icon_cnt * icon_id, 0, tex.width / icon_cnt, tex.height};
    DrawTexturePro(tex, source, dest, CLITERAL(Vector2){0}, 0, c);
}

static void load_tracks(FilePathList files) {
    for (size_t i = 0; i < files.count; ++i) {
        if (DirectoryExists(files.paths[i])) {
            FilePathList dir_files = LoadDirectoryFiles(files.paths[i]);
            load_tracks(dir_files);
            UnloadDirectoryFiles(dir_files);
        } else {
            if (track_exists(files.paths[i])) continue;
            char* mus_file_path = strdup(files.paths[i]);
            assert(mus_file_path != NULL && "ERROR: WE NEED MORE RAM");
            track_add(mus_file_path);
        }
    }
}

/* Plugin API */
void plug_init() {
    p = malloc(sizeof(*p));
    assert(p != NULL && "ERROR: WE NEED MORE RAM");
    memset(p, 0, sizeof(*p));

    // Precaclulate hann window
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        float t = (float)i / (FFT_SIZE - 1);
        p->hann[i] = 0.5 - 0.5 * cosf(TWO_PI * t);
    }

    p->cur_track = -1;
    p->volume = 0.5f;
    p->mode = MODE_NONE;
    p->music_is_paused = false;

    p->circle = LoadShader(NULL, fragment_files[CIRCLE_FRAGMENT]);
    for (Uniform i = 0; i < COUNT_UNIFORMS; i++) {
        p->uniform_locs[i] = GetShaderLocation(p->circle, uniform_names[i]);
    }
}

void plug_clean() {
    fft_clean();

    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track* track = &p->tracks.items[i];
        DetachAudioStreamProcessor(track->music.stream, callback);
        UnloadMusicStream(track->music);
        free(track->file_path);
    }

    UnloadShader(p->circle);

    assets_unload();

    da_free(&p->tracks);
    da_free(&p->assets.images);
    da_free(&p->assets.textures);
    free(p);
}

Plug* plug_pre_reload(void) {
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track* track = &p->tracks.items[i];
        DetachAudioStreamProcessor(track->music.stream, callback);
    }

    UnloadShader(p->circle);
    assets_unload();

    return p;
}

void plug_post_reload(Plug* prev) {
    p = prev;
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track* track = &p->tracks.items[i];
        AttachAudioStreamProcessor(track->music.stream, callback);
    }

    p->circle = LoadShader(NULL, fragment_files[CIRCLE_FRAGMENT]);
    for (Uniform i = 0; i < COUNT_UNIFORMS; i++) {
        p->uniform_locs[i] = GetShaderLocation(p->circle, uniform_names[i]);
    }
}

void plug_update(void) {
    int w = GetScreenWidth();
    int h = GetScreenHeight();

    static float hud_timer = HUD_TIMER_SECS;
    static float next_timer = UI_NEXT_TRACK_TIMER_SECS;
    static int fullscreen_btn_state = BTN_NONE;
    static bool volume_expanded = false;

    Track* track = track_get_cur();

    // Update music & handle input
    if (track) {
        UpdateMusicStream(track->music);
        SetMusicVolume(track->music, p->volume);

        if (!IsMusicStreamPlaying(track->music) && !p->music_is_paused) {
            if (next_timer > 0.0f) {
                next_timer -= GetFrameTime();
            } else {
                fft_clean_in();
                track_next_handle(false);
                next_timer = UI_NEXT_TRACK_TIMER_SECS;
            }
        }

        if (IsKeyPressed(KEY_TOGGLE_PLAY)) music_play_pause();
        if (IsKeyPressed(KEY_FULLSCREEN1) || IsKeyPressed(KEY_FULLSCREEN2)) p->fullscreen = !p->fullscreen;
        if (IsKeyPressed(KEY_TRACK_NEXT)) track_next_handle(true);
        if (IsKeyPressed(KEY_TRACK_PREV)) track_prev_handle();
        if (IsKeyPressed(KEY_VOLUME_DOWN)) music_volume_down();
        if (IsKeyPressed(KEY_VOLUME_UP)) music_volume_up();
    }

    // Handle Drag&Drop
    if (IsFileDropped()) {
        FilePathList files = LoadDroppedFiles();
        load_tracks(files);
        UnloadDroppedFiles(files);

        if (track_get_cur() == NULL && p->tracks.count > 0) {
            PlayMusicStream(p->tracks.items[0].music);
            p->cur_track = 0;
        }
    }

    // Render UI
    BeginDrawing();
    {
        ClearBackground(COLOR_BACKGROUND);

        if (track) {
            size_t m = fft_proccess(GetFrameTime());
            Rectangle preview_size = calculate_preview();

            BeginScissorMode(preview_size.x, preview_size.y, preview_size.width, preview_size.height);
            {
                fft_render(preview_size, m);
                popups_render(&p->popups, preview_size, GetFrameTime());
            }
            EndScissorMode();

            if (p->fullscreen) {
                if (!(fullscreen_btn_state & BTN_HOVER) && !volume_expanded) hud_timer -= GetFrameTime();
                if (fabsf(vec2_sum(GetMouseDelta())) > 0.0f) hud_timer = HUD_TIMER_SECS;
            } else {
                track_panel_render(CLITERAL(Rectangle){0, 0, w * PANEL_PERCENT, preview_size.height}, GetFrameTime());
                timeline_render(CLITERAL(Rectangle){0, preview_size.height, w, h * TIMELINE_PERCENT}, track);
            }

            if (hud_timer > 0.0f || !p->fullscreen) {
                fullscreen_btn_state = fullscreen_btn_render(preview_size);
                volume_expanded = volume_control_render(preview_size);
                p->fullscreen ^= (bool)(fullscreen_btn_state & BTN_CLICKED);
            }
        } else {
            const char* msg = "Drag&Drop Music";
            Color c = WHITE;

            int width = MeasureText(msg, GENERAL_FONT_SIZE);
            DrawText(msg, w / 2 - width / 2, h / 2 - GENERAL_FONT_SIZE / 2, GENERAL_FONT_SIZE, c);
            popups_render(&p->popups, CLITERAL(Rectangle){.x = 0, .y = 0, .width = w, .height = h}, GetFrameTime());
        }
    }
    EndDrawing();
}