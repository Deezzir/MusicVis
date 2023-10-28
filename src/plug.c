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

// Parameters
#define N (1 << 13)
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
    Tracks tracks;
    int cur_track;
    bool error;
    float volume;

    Shader circle;
    int uniform_locs[COUNT_UNIFORMS];
    bool fullscreen;
    Texture2D fullscreen_tex;
    Texture2D volume_tex;

    float in_raw[N];
    float in_wd[N];
    float complex out_raw[N];
    float out_log[N];
    float out_smooth[N];
    float out_smear[N];
} Plug;

Plug* p = NULL;

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

    // Honn Windowing
    for (size_t i = 0; i < N; ++i) {
        float t = (float)i / (N - 1);
        float hann = 0.5 - 0.5 * cosf(TWO_PI * t);
        p->in_wd[i] = p->in_raw[i] * hann;
    }

    // Perform FFT
    fft(p->in_wd, 1, p->out_raw, N);

    for (float f = LOW_FREQ; (size_t)f < N / 2; f = ceilf(f * FREQ_STEP)) {
        float f1 = ceilf(f * FREQ_STEP);
        float ampl = 0.0f;

        for (size_t q = (size_t)f; q < N / 2 && q < (size_t)f1; ++q) {
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
    memmove(p->in_raw, p->in_raw + 1, (N - 1) * sizeof(p->in_raw[0]));
    p->in_raw[N - 1] = frame;
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

    BeginScissorMode(boundary.x, boundary.y, boundary.width, boundary.height);
    {
        ClearBackground(COLOR_TIMELINE_BACKGROUND);
        DrawText(time_elapsed, boundary.x + text_pad, pos_y, font_size, WHITE);
        DrawText(time_whole, boundary.x + boundary.width - text_pad - text_w, pos_y, font_size, WHITE);

        DrawLineEx(start_pos, end_pos, 2, COLOR_TIMELINE_CURSOR);          // Draw Cursor
        DrawRectangleLinesEx(boundary, 2, COLOR_TRACK_BUTTON_BACKGROUND);  // Draw Contour

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

    Rectangle source = {p->fullscreen_tex.width / 4 * icon_id, 0, p->fullscreen_tex.width / 4, p->fullscreen_tex.height};
    DrawTexturePro(p->fullscreen_tex, source, btn, CLITERAL(Vector2){0}, 0, c);

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
            if (p->volume != 0.0f) prev_volume = p->volume;
            p->volume = p->volume == 0.0f ? prev_volume : 0.0f;
        }
        expanded = true;
    }

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
    Rectangle source = {p->volume_tex.width / 3 * icon_id, 0, p->volume_tex.width / 3, p->volume_tex.height};
    DrawTexturePro(p->volume_tex, source, btn, CLITERAL(Vector2){0}, 0, c);

    Track* track = get_cur_track();
    if (track) SetMusicVolume(track->music, p->volume);
}

void track_panel_render(Rectangle boundary, float dt) {
    Vector2 mouse = GetMousePosition();

    size_t track_count = p->tracks.count;
    float scroll_w = boundary.width * SCROLL_PERCENT;
    float item_size = boundary.width * TRACK_ITEM_PERCENT;
    float scrollable_area = item_size * track_count;
    float panel_pad = item_size * 0.1f;

    static float panel_velocity = 0.0f;
    if (CheckCollisionPointRec(mouse, boundary)) {
        panel_velocity = panel_velocity * VELOCITY_DECAY + GetMouseWheelMove() * item_size * 8;
    }

    float max_scroll = scrollable_area - boundary.height;
    static float panel_scroll = 0.0f;
    panel_scroll -= panel_velocity * dt;

    static bool scrolling = false;
    static float scrolling_mouse_offset = 0.0f;
    if (scrolling) {
        panel_scroll = (mouse.y - boundary.y - scrolling_mouse_offset) / boundary.height * scrollable_area;
    }

    if (panel_scroll < 0) panel_scroll = 0;
    if (max_scroll < 0) max_scroll = 0;
    if (panel_scroll > max_scroll) panel_scroll = max_scroll;

    // Change background color
    BeginScissorMode(boundary.x, boundary.y, boundary.width, boundary.height);
    {
        ClearBackground(COLOR_TRACK_PANEL_BACKGROUND);

        for (size_t i = 0; i < track_count; ++i) {
            Rectangle item = {
                .x = boundary.x + panel_pad,
                .y = i * item_size + boundary.y + panel_pad - panel_scroll,
                .width = boundary.width - 2 * panel_pad - scroll_w,
                .height = item_size - 2 * panel_pad,
            };

            Color c;
            if ((int)i != p->cur_track) {
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

            const char* orig = GetFileName(p->tracks.items[i].file_path);
            char* track_name = strdup(orig);
            remove_extension(track_name);

            float font_size = TRACK_NAME_FONT_SIZE + item.height * 0.1f;
            float text_pad = item.width * 0.05f;

            int text_w = MeasureText(track_name, font_size);
            while (text_w > item.width - 2 * text_pad) {
                track_name[strlen(track_name) - 1] = '\0';
                text_w = MeasureText(track_name, font_size);
            }

            DrawText(track_name, item.x + text_pad, item.y + item.height / 2 - font_size / 2, font_size, BLACK);

            if (scrollable_area > boundary.height) {
                float t = boundary.height / scrollable_area;
                float q = panel_scroll / scrollable_area;
                Rectangle scrollbar_boundary = {
                    .x = boundary.x + boundary.width - scroll_w,
                    .y = boundary.y + boundary.height * q,
                    .width = scroll_w,
                    .height = boundary.height * t,
                };
                DrawRectangleRounded(scrollbar_boundary, 0.8, 20, COLOR_ACCENT);

                if (scrolling) {
                    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                        scrolling = false;
                    }
                } else {
                    if (CheckCollisionPointRec(mouse, scrollbar_boundary)) {
                        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                            scrolling = true;
                            scrolling_mouse_offset = mouse.y - scrollbar_boundary.y;
                        }
                    }
                }
            }
            free(track_name);
        }
    }
    EndScissorMode();
}

void plug_texture_setup() {
    Image fullscreen_img = LoadImage(FULLSCREEN_IMAGE_FILEPATH);
    p->fullscreen_tex = LoadTextureFromImage(fullscreen_img);
    UnloadImage(fullscreen_img);

    Image volume_img = LoadImage(VOLUME_IMAGE_FILEPATH);
    p->volume_tex = LoadTextureFromImage(volume_img);
    UnloadImage(volume_img);
}

void plug_init() {
    p = malloc(sizeof(*p));
    assert(p != NULL && "ERROR: WE NEED MORE RAM");
    memset(p, 0, sizeof(*p));

    p->cur_track = -1;
    p->volume = 0.5f;

    p->circle = LoadShader(NULL, fragment_files[CIRCLE_FRAGMENT]);
    for (Uniform i = 0; i < COUNT_UNIFORMS; i++) {
        p->uniform_locs[i] = GetShaderLocation(p->circle, uniform_names[i]);
    }

    plug_texture_setup();
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
    UnloadTexture(p->fullscreen_tex);
    UnloadTexture(p->volume_tex);

    da_free(&p->tracks);
    free(p);
}

Plug* plug_pre_reload(void) {
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track* track = &p->tracks.items[i];
        DetachAudioStreamProcessor(track->music.stream, callback);
    }
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

    plug_texture_setup();
}

void plug_update(void) {
    int w = GetRenderWidth();
    int h = GetRenderHeight();
    float dt = GetFrameTime();
    static float hud_timer = HUD_TIMER_SECS;

    Track* track = get_cur_track();

    if (track) {
        UpdateMusicStream(track->music);

        if (IsKeyPressed(KEY_SPACE)) {
            if (IsMusicStreamPlaying(track->music)) {
                PauseMusicStream(track->music);
            } else {
                ResumeMusicStream(track->music);
            }
        }

        if (IsKeyPressed(KEY_F)) {
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