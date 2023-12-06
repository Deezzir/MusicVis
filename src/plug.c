#include "plug.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"

// Fragment Files
#define CIRCLE_FS_FILEPATH "./resources/shaders/circle.fs"

// Images
#define FULLSCREEN_IMAGE_FILEPATH "./resources/images/fullscreen.png"
#define VOLUME_IMAGE_FILEPATH "./resources/images/volume.png"
#define MUSIC_CTRL_IMAGE_FILEPATH "./resources/images/music_ctrl.png"

// Controls
#define KEY_TOGGLE_PLAY KEY_SPACE
#define KEY_FULLSCREEN KEY_F
#define KEY_TOGGLE_MUTE KEY_M

// Parameters
#define FFT_SIZE (1 << 13)
#define FREQ_STEP 1.06f
#define LOW_FREQ 1.0f
#define SMOOTHNESS 8
#define SMEARNESS 3

#define GENERAL_FONT_SIZE 50
#define TRACK_NAME_FONT_SIZE 18

#define PANEL_PERCENT 0.25f
#define TIMELINE_PERCENT 0.1f
#define SCROLL_PERCENT 0.03f
#define TRACK_ITEM_PERCENT 0.2f
#define VELOCITY_DECAY 0.9f

#define HUD_TIMER_SECS 2.0f
#define HUD_ICON_SIZE 30.0f
#define HUD_ICON_MARGIN 10.0f
#define HUD_VOLUME_SEGMENTS 6.0f
#define HUD_EDGE_WIDTH 2.0f

#define HSV_SATURATION 0.75f
#define HSV_VALUE 1.0f

#define TWO_PI 2 * PI

#define COLOR_ACCENT ColorFromHSV(200, 0.75, 0.8)
#define COLOR_BACKGROUND GetColor(0x000015FF)
#define COLOR_TRACK_PANEL_BACKGROUND ColorBrightness(COLOR_BACKGROUND, -0.2)
#define COLOR_TRACK_BUTTON_BACKGROUND ColorBrightness(COLOR_BACKGROUND, 0.15)
#define COLOR_TRACK_BUTTON_HOVEROVER ColorBrightness(COLOR_TRACK_BUTTON_BACKGROUND, 0.15)
#define COLOR_TRACK_BUTTON_SELECTED COLOR_ACCENT
#define COLOR_TIMELINE_CURSOR COLOR_ACCENT
#define COLOR_TIMELINE_BACKGROUND ColorBrightness(COLOR_BACKGROUND, -0.3)
#define COLOR_HUD_BTN_BACKGROUND ColorBrightness(COLOR_BACKGROUND, 0.15)
#define COLOR_HUD_BTN_HOVEROVER ColorBrightness(COLOR_HUD_BTN_BACKGROUND, 0.15)

typedef enum {
    CIRCLE_FRAGMENT = 0,
    COUNT_FRAGMENTS
} FragmentFile;

typedef enum {
    CIRCLE_RADIUS_UNIFORM = 0,
    CIRCLE_POWER_UNIFORM,
    COUNT_UNIFORMS
} Uniform;

static_assert(COUNT_UNIFORMS == 2, "Update list of uniform names");
const char* uniform_names[COUNT_UNIFORMS] = {
    [CIRCLE_RADIUS_UNIFORM] = "radius",
    [CIRCLE_POWER_UNIFORM] = "power"};

static_assert(COUNT_FRAGMENTS == 1, "Update list of fragment file paths");
const char* fragment_files[COUNT_FRAGMENTS] = {
    [CIRCLE_FRAGMENT] = CIRCLE_FS_FILEPATH,
};

typedef enum {
    LOOPING = 0,
    SHUFFLE
} PlayMode;

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
    Tracks tracks;
    int cur_track;
    bool error;
    float volume;
    PlayMode mode;

    Shader circle;
    int uniform_locs[COUNT_UNIFORMS];
    bool fullscreen;

    Assets assets;

    float in_raw[FFT_SIZE];
    float in_wd[FFT_SIZE];
    float complex out_raw[FFT_SIZE];
    float out_log[FFT_SIZE];
    float out_smooth[FFT_SIZE];
    float out_smear[FFT_SIZE];
    float hann[FFT_SIZE];
} Plug;

Plug* p = NULL;

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
    // GenTextureMipmaps(&item.value);
    // SetTextureFilter(item.value, TEXTURE_FILTER_BILINEAR);
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

void fft_clean(void) {
    memset(p->in_raw, 0, sizeof(p->in_raw));
    memset(p->in_wd, 0, sizeof(p->in_wd));
    memset(p->out_raw, 0, sizeof(p->out_raw));
    memset(p->out_log, 0, sizeof(p->out_log));
    memset(p->out_smooth, 0, sizeof(p->out_smooth));
    memset(p->out_smear, 0, sizeof(p->out_smear));
}

void fft(float in[], size_t stride, float complex out[], size_t n) {
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

size_t fft_proccess(float dt) {
    size_t m = 0;
    float max_amp = 1.0f;

    // Hann Windowing
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        p->in_wd[i] = p->in_raw[i] * p->hann[i];
    }

    // Perform FFT
    fft(p->in_wd, 1, p->out_raw, FFT_SIZE);

    for (float f = LOW_FREQ; (size_t)f < FFT_SIZE / 2; f = ceilf(f * FREQ_STEP)) {
        float f1 = ceilf(f * FREQ_STEP);
        float ampl = 0.0f;

        for (size_t q = (size_t)f; q < FFT_SIZE / 2 && q < (size_t)f1; ++q) {
            float a = amp(p->out_raw[q]);
            ampl = fmaxf(ampl, a);
        }

        max_amp = fmaxf(max_amp, ampl);
        p->out_log[m++] = ampl;
    }

    for (size_t i = 0; i < m; ++i) {
        p->out_log[i] /= max_amp;                                                  // Normalize
        p->out_smooth[i] += (p->out_log[i] - p->out_smooth[i]) * SMOOTHNESS * dt;  // Smooth
        p->out_smear[i] += (p->out_smooth[i] - p->out_smear[i]) * SMEARNESS * dt;  // Smear
    }

    return m;
}

void draw_texture_from_endpoints(Texture2D tex, Vector2 start_pos, Vector2 end_pos, float radius, Color c) {
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

void fft_render(Rectangle boundary, size_t m) {
    float h = boundary.height;
    float w = boundary.width;
    float cell_width = roundf(w / m);

    // Draw Bars and Circles
    for (size_t i = 0; i < m; ++i) {
        float t_smooth = p->out_smooth[i];
        float t_smear = p->out_smear[i];

        float hue = (float)i / m * 360;
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

void fft_push(float frame) {
    memmove(p->in_raw, p->in_raw + 1, (FFT_SIZE - 1) * sizeof(p->in_raw[0]));
    p->in_raw[FFT_SIZE - 1] = frame;
}

void callback(void* bufferData, unsigned int frames) {
    float(*fs)[2] = bufferData;

    for (size_t i = 0; i < frames; ++i) {
        fft_push(fs[i][0]);
    }
}

Track* get_cur_track() {
    if (p->cur_track < 0 || (size_t)p->cur_track >= p->tracks.count) return NULL;
    return &p->tracks.items[p->cur_track];
}

void error_load_track(void) {
    fprintf(stderr, "ERROR: Couldn't load track\n");
    p->error = true;
}

char* get_track_name(const char* file_path, Rectangle item, float font_size, float text_pad) {
    const char* orig = GetFileName(file_path);
    char* track_name = strdup(orig);
    remove_extension(track_name);

    int text_w = MeasureText(track_name, font_size);
    while (text_w > item.width - 2 * text_pad) {
        track_name[strlen(track_name) - 1] = '\0';
        text_w = MeasureText(track_name, font_size);
    }

    return track_name;
}

void next_shuffle_track() {
    ;
    if (p->tracks.count == 1) return;

    int i = p->cur_track;
    while (i == p->cur_track) i = GetRandomValue(0, p->tracks.count - 1);
    Track* track = get_cur_track();
    if (track) StopMusicStream(track->music);
    PlayMusicStream(p->tracks.items[i].music);
    p->cur_track = i;
}

void track_render(Rectangle boundary, Rectangle item, int i) {
    Vector2 mouse = GetMousePosition();

    Color c;
    if (i != p->cur_track) {
        if (CheckCollisionPointRec(mouse, boundary) && CheckCollisionPointRec(mouse, item)) {
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Track* track = get_cur_track();
                if (track) StopMusicStream(track->music);
                PlayMusicStream(p->tracks.items[i].music);
                p->cur_track = i;
            }
            c = COLOR_TRACK_BUTTON_HOVEROVER;
        } else {
            c = COLOR_TRACK_BUTTON_BACKGROUND;
        }
    } else {
        c = COLOR_TRACK_BUTTON_SELECTED;
    }
    DrawRectangleRounded(item, 0.2, 20, c);

    float font_size = TRACK_NAME_FONT_SIZE + item.height * 0.1f;
    float text_pad = item.width * 0.05f;
    char* track_name = get_track_name(p->tracks.items[i].file_path, item, font_size, text_pad);
    DrawText(track_name, item.x + text_pad, item.y + item.height / 2 - font_size / 2, font_size, BLACK);
    free(track_name);
}

void tracks_render(Rectangle boundary, float item_size, float scroll_w, float panel_scroll) {
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

bool track_exists(const char* file_path) {
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track* track = &p->tracks.items[i];
        if (strcmp(track->file_path, file_path) == 0) return true;
    }
    return false;
}

void music_init(char* file_path) {
    Music music = LoadMusicStream(file_path);

    if (IsMusicReady(music)) {
        SetMusicVolume(music, p->volume);
        AttachAudioStreamProcessor(music.stream, callback);
        PlayMusicStream(music);

        da_append(&p->tracks, (CLITERAL(Track){
                                  .file_path = file_path,
                                  .music = music}));
        p->cur_track = p->tracks.count - 1;
    } else {
        free(file_path);
        error_load_track();
    }
}

void timeline_render(Rectangle boundary, Track* track) {
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

bool fullscreen_btn_render(Rectangle boundary) {
    int icon_id;
    bool clicked = false;
    Rectangle btn = {boundary.x + boundary.width - (HUD_ICON_SIZE + HUD_ICON_MARGIN), boundary.y + HUD_ICON_MARGIN, HUD_ICON_SIZE, HUD_ICON_SIZE};

    Color c;
    if (CheckCollisionPointRec(GetMousePosition(), btn)) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) clicked = true;

        c = COLOR_HUD_BTN_HOVEROVER;
        icon_id = p->fullscreen ? 3 : 1;
    } else {
        c = COLOR_HUD_BTN_BACKGROUND;
        icon_id = p->fullscreen ? 2 : 0;
    }

    Texture2D fullscreen_tex = assets_texture(FULLSCREEN_IMAGE_FILEPATH);
    Rectangle source = {fullscreen_tex.width / 4 * icon_id, 0, fullscreen_tex.width / 4, fullscreen_tex.height};
    DrawTexturePro(fullscreen_tex, source, btn, CLITERAL(Vector2){0}, 0, c);

    return clicked;
}

static float slider_get_value(float y, float hiy, float loy) {
    if (y < hiy) y = hiy;
    if (y > loy) y = loy;
    y -= hiy;
    y = 1 - y / (loy - hiy);
    return y;
}

void vert_slider_render(Rectangle boundary, float* volume, bool* expanded) {
    Vector2 mouse = GetMousePosition();

    static bool dragging = false;

    Color c = COLOR_HUD_BTN_HOVEROVER;
    float segments = HUD_VOLUME_SEGMENTS;

    float slider_w = boundary.width / segments;
    float slider_h = boundary.height / segments * 4;
    float slider_offset = (boundary.height - segments * HUD_ICON_SIZE) / 1.5;
    float slider_radius = HUD_ICON_SIZE / 3.5;
    float cutoff = boundary.height - slider_h - slider_offset;

    Rectangle slider_bar = {boundary.x + boundary.width / 2 - slider_w / 2, boundary.y + slider_offset, slider_w, slider_h};
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

    *expanded = dragging || CheckCollisionPointRec(mouse, boundary);
}

void mute(float* prev_volume) {
    if (p->volume != 0.0f) *prev_volume = p->volume;
    p->volume = p->volume == 0.0f ? *prev_volume : 0.0f;
}

void volume_control_render(Rectangle boundary) {
    Vector2 mouse = GetMousePosition();

    static bool expanded = false;
    static float prev_volume = 0.5f;

    float mouse_wheel_steep = 0.1f;
    float segments = HUD_VOLUME_SEGMENTS;

    float btn_offset = HUD_ICON_SIZE + HUD_ICON_MARGIN;
    float full_margin = HUD_ICON_MARGIN * 2;

    Rectangle volume_box = {
        boundary.x + boundary.width - (HUD_ICON_SIZE + full_margin),
        boundary.y + boundary.height - (HUD_ICON_SIZE + full_margin),
        HUD_ICON_SIZE + full_margin,
        HUD_ICON_SIZE + full_margin,
    };

    if (CheckCollisionPointRec(mouse, volume_box)) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            mute(&prev_volume);
        }
        expanded = true;
    }

    if (IsKeyPressed(KEY_TOGGLE_MUTE)) mute(&prev_volume);

    if (expanded) {
        volume_box.height = segments * HUD_ICON_SIZE + full_margin;
        volume_box.y -= (segments - 1) * HUD_ICON_SIZE;
        vert_slider_render(volume_box, &p->volume, &expanded);
        p->volume += GetMouseWheelMove() * mouse_wheel_steep;
        if (p->volume < 0.0f) p->volume = 0.0f;
        if (p->volume > 1.0f) p->volume = 1.0f;
    }

    Color c = expanded ? COLOR_HUD_BTN_HOVEROVER : COLOR_HUD_BTN_BACKGROUND;
    int icon_id = icon_id = (p->volume > 0.5f) ? 2 : ((p->volume == 0.0f) ? 0 : 1);
    Rectangle btn = {boundary.x + boundary.width - btn_offset, boundary.y + boundary.height - btn_offset, HUD_ICON_SIZE, HUD_ICON_SIZE};

    Texture2D volume_tex = assets_texture(VOLUME_IMAGE_FILEPATH);
    Rectangle source = {volume_tex.width / 3 * icon_id, 0, volume_tex.width / 3, volume_tex.height};
    DrawTexturePro(volume_tex, source, btn, CLITERAL(Vector2){0}, 0, c);

    Track* track = get_cur_track();
    if (track) SetMusicVolume(track->music, p->volume);
}

void handle_scroll(Rectangle boundary, bool* scrolling, float* scrolling_mouse_offset, float* panel_velocity, float scrollable_area, float scroll_w, float panel_scroll, float item_size) {
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

        if (*scrolling) {
            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) *scrolling = false;
        } else {
            if (CheckCollisionPointRec(mouse, scrollbar_boundary)) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    *scrolling = true;
                    *scrolling_mouse_offset = mouse.y - scrollbar_boundary.y;
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

void music_control_render(Rectangle boundary, PlayMode mode, float scroll_w) {
    Vector2 mouse = GetMousePosition();

    int icon_cnt = 2;
    int icon_id = mode;
    float panel_part = (boundary.width - scroll_w - HUD_EDGE_WIDTH) / (icon_cnt * 2);
    float btn_x = boundary.x + panel_part * (icon_id * icon_cnt + 1) - HUD_ICON_SIZE / 2;

    Color c = COLOR_HUD_BTN_HOVEROVER;
    Rectangle btn = {btn_x, boundary.y + HUD_ICON_MARGIN, HUD_ICON_SIZE, HUD_ICON_SIZE};
    Texture2D music_ctrl_tex = assets_texture(MUSIC_CTRL_IMAGE_FILEPATH);
    Rectangle source = {music_ctrl_tex.width / icon_cnt * icon_id, 0, music_ctrl_tex.width / icon_cnt, music_ctrl_tex.height};

    c = p->mode == mode ? COLOR_HUD_BTN_HOVEROVER : COLOR_HUD_BTN_BACKGROUND;
    if (CheckCollisionPointRec(mouse, btn)) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            p->mode = mode;
        else
            c = COLOR_HUD_BTN_HOVEROVER;
    }
    DrawTexturePro(music_ctrl_tex, source, btn, CLITERAL(Vector2){0}, 0, c);
}

void track_panel_render(Rectangle boundary, float dt) {
    Vector2 mouse = GetMousePosition();

    static bool scrolling = false;
    static float scrolling_mouse_offset = 0.0f;

    size_t track_count = p->tracks.count;
    float tracks_offset = HUD_ICON_MARGIN * 2 + HUD_ICON_SIZE;
    float scroll_w = boundary.width * SCROLL_PERCENT - HUD_EDGE_WIDTH;
    float item_size = boundary.width * TRACK_ITEM_PERCENT;
    float scrollable_area = item_size * track_count;

    static float panel_velocity = 0.0f;
    if (CheckCollisionPointRec(mouse, boundary)) panel_velocity = panel_velocity * VELOCITY_DECAY + GetMouseWheelMove() * item_size * 8;

    float max_scroll = scrollable_area - boundary.height + tracks_offset;
    static float panel_scroll = 0.0f;
    panel_scroll -= panel_velocity * dt;

    if (scrolling) panel_scroll = (mouse.y - boundary.y - tracks_offset - scrolling_mouse_offset) / (boundary.height + tracks_offset) * scrollable_area;
    if (panel_scroll < 0) panel_scroll = 0;
    if (max_scroll < 0) max_scroll = 0;
    if (panel_scroll > max_scroll) panel_scroll = max_scroll;

    BeginScissorMode(boundary.x, boundary.y, boundary.width, boundary.height);
    {
        ClearBackground(COLOR_TRACK_PANEL_BACKGROUND);

        music_control_render(boundary, LOOPING, scroll_w);  // Draw loop
        music_control_render(boundary, SHUFFLE, scroll_w);  // Draw shuffle

        Rectangle tracks_boundary = {boundary.x, boundary.y + tracks_offset, boundary.width, boundary.height - tracks_offset};
        BeginScissorMode(tracks_boundary.x, tracks_boundary.y, tracks_boundary.width, tracks_boundary.height);
        {
            handle_scroll(tracks_boundary, &scrolling, &scrolling_mouse_offset, &panel_velocity, scrollable_area, scroll_w, panel_scroll, item_size);
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

void plug_init() {
    p = malloc(sizeof(*p));
    assert(p != NULL && "ERROR: WE NEED MORE RAM");
    memset(p, 0, sizeof(*p));

    for (size_t i = 0; i < FFT_SIZE; ++i) {
        float t = (float)i / (FFT_SIZE - 1);
        p->hann[i] = 0.5 - 0.5 * cosf(TWO_PI * t);
    }

    p->cur_track = -1;
    p->volume = 0.5f;
    p->mode = LOOPING;

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

    assets_unload();

    da_free(&p->tracks);
    free(p);
}

Plug* plug_pre_reload(void) {
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track* track = &p->tracks.items[i];
        DetachAudioStreamProcessor(track->music.stream, callback);
    }
    assets_unload();
    return p;
}

void plug_post_reload(Plug* prev) {
    p = prev;
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track* track = &p->tracks.items[i];
        AttachAudioStreamProcessor(track->music.stream, callback);
    }

    UnloadShader(p->circle);
    p->circle = LoadShader(NULL, fragment_files[CIRCLE_FRAGMENT]);
    for (Uniform i = 0; i < COUNT_UNIFORMS; i++) {
        p->uniform_locs[i] = GetShaderLocation(p->circle, uniform_names[i]);
    }
}

void plug_update(void) {
    int w = GetRenderWidth();
    int h = GetRenderHeight();
    float dt = GetFrameTime();
    static float hud_timer = HUD_TIMER_SECS;

    Track* track = get_cur_track();

    if (track) {
        UpdateMusicStream(track->music);
        if (roundf((GetMusicTimePlayed(track->music))) == roundf(GetMusicTimeLength(track->music))) {
            if (p->mode == SHUFFLE) {
                next_shuffle_track();
            }
        }

        if (IsKeyPressed(KEY_TOGGLE_PLAY)) {
            if (IsMusicStreamPlaying(track->music)) {
                PauseMusicStream(track->music);
            } else {
                ResumeMusicStream(track->music);
            }
        }

        if (IsKeyPressed(KEY_FULLSCREEN)) {
            p->fullscreen = !p->fullscreen;
        }
    }

    if (IsFileDropped()) {
        FilePathList files = LoadDroppedFiles();
        for (size_t i = 0; i < files.count; ++i) {
            if (track_exists(files.paths[i])) continue;
            char* mus_file_path = strdup(files.paths[i]);

            Track* cur_track = get_cur_track();
            if (cur_track) StopMusicStream(cur_track->music);

            music_init(mus_file_path);
        }
        UnloadDroppedFiles(files);
    }

    BeginDrawing();
    {
        ClearBackground(COLOR_BACKGROUND);

        if (track) {
            size_t m = fft_proccess(dt);
            Rectangle preview_size;

            if (p->fullscreen) {
                preview_size = (Rectangle){0, 0, w, h};
                BeginScissorMode(preview_size.x, preview_size.y, preview_size.width, preview_size.height);
                {
                    fft_render(preview_size, m);
                }
                EndScissorMode();

                hud_timer -= dt;
                if (fabsf(Vector2SumComponents(GetMouseDelta())) > 5.0f) hud_timer = HUD_TIMER_SECS;
            } else {
                float tracks_panel_w = w * PANEL_PERCENT;
                float timeline_h = h * TIMELINE_PERCENT;
                preview_size = (Rectangle){tracks_panel_w, 0, w - tracks_panel_w, h - timeline_h};

                BeginScissorMode(preview_size.x, preview_size.y, preview_size.width, preview_size.height);
                {
                    fft_render(preview_size, m);
                }
                EndScissorMode();

                track_panel_render(CLITERAL(Rectangle){0, 0, tracks_panel_w, preview_size.height}, dt);
                timeline_render(CLITERAL(Rectangle){0, preview_size.height, w, timeline_h}, track);
            }

            if (hud_timer > 0.0f || !p->fullscreen) {
                p->fullscreen ^= fullscreen_btn_render(preview_size);
                volume_control_render(preview_size);
            }
        } else {
            const char* msg = NULL;
            Color c;

            if (p->error) {
                msg = "Couldn't load Music";
                c = RED;
            } else {
                msg = "Drag&Drop Music";
                c = WHITE;
            }

            int width = MeasureText(msg, GENERAL_FONT_SIZE);
            DrawText(msg, w / 2 - width / 2, h / 2 - GENERAL_FONT_SIZE / 2, GENERAL_FONT_SIZE, c);
        }
    }
    EndDrawing();
}