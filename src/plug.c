#include "plug.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <raylib.h>
#include <rlgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N (1 << 13)
#define FREQ_STEP 1.06f
#define LOW_FREQ 1.0f
#define FONT_SIZE 50
#define SMOOTHNESS 8
#define SMEARNESS 3
#define HSV_SATURATION 0.75f
#define HSV_VALUE 1.0f
#define TWO_PI 2 * PI

const char* CIRCLE_FS_FILEPATH = "./shaders/circle.fs";
typedef struct {
    Music music;
    bool error;
    Shader circle;
    int circle_radius_location;
    int circle_power_location;

    float in_raw[N];
    float in_wd[N];
    float complex out_raw[N];
    float out_log[N];
    float out_smooth[N];
    float out_smear[N];
} Plug;

Plug* p = NULL;

static inline float amp(float complex v) {
    float a = crealf(v);
    float b = cimagf(v);
    return logf(a * a + b * b);
}

void draw_texture_from_endpoints(Texture tex, Vector2 startPos, Vector2 endPos, float radius, Color c) {
    Rectangle dest, source;
    Vector2 origin = {0};

    dest.width = 2 * radius;

    if (endPos.y >= startPos.y) {
        dest.x = startPos.x - radius;
        dest.y = startPos.y;
        dest.height = endPos.y - startPos.y;
        source = (Rectangle){0, 0.5, 1, 0.5};
    } else {
        dest.x = endPos.x - radius;
        dest.y = endPos.y;
        source = (Rectangle){0, 0, 1, 0.5};
        dest.height = startPos.y - endPos.y;
    }

    DrawTexturePro(tex, source, dest, origin, 0, c);
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

void fft_render(size_t w, size_t h, size_t m) {
    float cell_width = (float)w / m;

    // Draw Bars and Circles
    for (size_t i = 0; i < m; ++i) {
        float t_smooth = p->out_smooth[i];
        float t_smear = p->out_smear[i];

        float hue = (float)i / m * 360;
        Color c = ColorFromHSV(hue, HSV_SATURATION, HSV_VALUE);

        float thick = cell_width / 3;
        float radius = 3 * cell_width * sqrtf(t_smooth);

        Texture texture = {rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};

        Vector2 middlePos = {i * cell_width + cell_width / 2, h / 2};
        Vector2 startPos_t = {middlePos.x, h / 2 - h / 3 * t_smooth};
        Vector2 startPos_b = {middlePos.x, h / 2 + h / 3 * t_smooth};

        // Draw Bars
        DrawLineEx(startPos_t, middlePos, thick, c);
        DrawLineEx(startPos_b, middlePos, thick, c);

        // Draw Smear
        SetShaderValue(p->circle, p->circle_radius_location, (float[1]){0.3f}, SHADER_UNIFORM_FLOAT);
        SetShaderValue(p->circle, p->circle_power_location, (float[1]){2.0f}, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(p->circle);
        {
            float radius = cell_width * sqrtf(t_smooth);
            Vector2 endPos_t = {middlePos.x, h / 2 - h / 3 * t_smear};
            Vector2 endPos_b = {middlePos.x, h / 2 + h / 3 * t_smear};
            draw_texture_from_endpoints(texture, startPos_t, endPos_t, radius, c);
            draw_texture_from_endpoints(texture, startPos_b, endPos_b, radius, c);
        }
        EndShaderMode();

        // Draw Circles
        SetShaderValue(p->circle, p->circle_radius_location, (float[1]){0.08f}, SHADER_UNIFORM_FLOAT);
        SetShaderValue(p->circle, p->circle_power_location, (float[1]){4.0f}, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(p->circle);
        {
            Vector2 pos_t = {.x = startPos_t.x - radius, .y = startPos_t.y - radius};
            Vector2 pos_b = {.x = startPos_b.x - radius, .y = startPos_b.y - radius};
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

void music_init(const char* mus_file_path) {
    p->music = LoadMusicStream(mus_file_path);
    p->error = false;

    if (IsMusicReady(p->music)) {
        SetMusicVolume(p->music, 0.5f);
        AttachAudioStreamProcessor(p->music.stream, callback);
        PlayMusicStream(p->music);
    } else {
        p->error = true;
    }
}

void plug_init() {
    p = malloc(sizeof(*p));
    assert(p != NULL && "ERROR: WE NEED MORE RAM");
    memset(p, 0, sizeof(*p));

    p->circle = LoadShader(NULL, CIRCLE_FS_FILEPATH);
    p->circle_radius_location = GetShaderLocation(p->circle, "radius");
    p->circle_power_location = GetShaderLocation(p->circle, "power");
}

Plug* plug_pre_reload(void) {
    if (IsMusicReady(p->music)) {
        DetachAudioStreamProcessor(p->music.stream, callback);
    }
    return p;
}

void plug_post_reload(Plug* prev) {
    p = prev;
    if (IsMusicReady(p->music)) {
        AttachAudioStreamProcessor(p->music.stream, callback);
    }

    UnloadShader(p->circle);
    p->circle = LoadShader(NULL, CIRCLE_FS_FILEPATH);
    p->circle_radius_location = GetShaderLocation(p->circle, "radius");
    p->circle_power_location = GetShaderLocation(p->circle, "power");
}

void plug_update(void) {
    int w = GetRenderWidth();
    int h = GetRenderHeight();
    float dt = GetFrameTime();

    if (IsMusicReady(p->music)) {
        UpdateMusicStream(p->music);

        if (IsKeyPressed(KEY_SPACE)) {
            if (IsMusicStreamPlaying(p->music)) {
                PauseMusicStream(p->music);
            } else {
                ResumeMusicStream(p->music);
            }
        }

        if (IsKeyPressed(KEY_Q)) {
            StopMusicStream(p->music);
            PlayMusicStream(p->music);
        }
    }

    if (IsFileDropped()) {
        FilePathList files = LoadDroppedFiles();
        if (files.count > 0) {
            const char* mus_file_path = files.paths[0];

            if (IsMusicReady(p->music)) {
                StopMusicStream(p->music);
                UnloadMusicStream(p->music);
            }

            music_init(mus_file_path);
        }
        UnloadDroppedFiles(files);
    }

    BeginDrawing();
    {
        ClearBackground(CLITERAL(Color){0x0, 0x0, 0x15, 0xFF});

        if (IsMusicReady(p->music)) {
            size_t m = fft_proccess(dt);
            fft_render(w, h, m);
        } else {
            const char* msg = NULL;
            Color color;

            if (p->error) {
                msg = "Couldn't load Music";
                color = RED;
            } else {
                msg = "Drag&Drop Music";
                color = WHITE;
            }

            int width = MeasureText(msg, FONT_SIZE);
            DrawText(msg, w / 2 - width / 2, h / 2 - FONT_SIZE / 2, FONT_SIZE, color);
        }
    }
    EndDrawing();
}