#include "plug.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N (1 << 13)
#define FREQ_STEP 1.06f
#define LOW_FREQ 1.0f
#define FONT_SIZE 50

typedef struct {
    Music music;
    bool error;
} Plug;

float in_raw[N];
float in_wd[N];
float complex out_raw[N];
float out_log[N];

Plug* plug = NULL;

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

float amp(float complex v) {
    float a = crealf(v);
    float b = cimagf(v);
    return logf(a * a + b * b);
}

void callback(void* bufferData, unsigned int frames) {
    float(*fs)[2] = bufferData;

    for (size_t i = 0; i < frames; ++i) {
        memmove(in_raw, in_raw + 1, (N - 1) * sizeof(in_raw[0]));
        in_raw[N - 1] = fs[i][0];
    }
}

void music_init(const char* mus_file_path) {
    plug->music = LoadMusicStream(mus_file_path);
    plug->error = false;

    if (IsMusicReady(plug->music)) {
        SetMusicVolume(plug->music, 0.5f);
        AttachAudioStreamProcessor(plug->music.stream, callback);
        PlayMusicStream(plug->music);
    } else {
        plug->error = true;
    }
}

void plug_init() {
    plug = malloc(sizeof(*plug));
    assert(plug != NULL && "ERROR: WE NEED MORE RAM");
    memset(plug, 0, sizeof(*plug));
}

Plug* plug_pre_reload(void) {
    if (IsMusicReady(plug->music)) {
        DetachAudioStreamProcessor(plug->music.stream, callback);
    }
    return plug;
}

void plug_post_reload(Plug* prev) {
    plug = prev;
    if (IsMusicReady(plug->music)) {
        AttachAudioStreamProcessor(plug->music.stream, callback);
    }
}

void plug_update(void) {
    int w = GetRenderWidth();
    int h = GetRenderHeight();

    if (IsMusicReady(plug->music)) {
        UpdateMusicStream(plug->music);

        if (IsKeyPressed(KEY_SPACE)) {
            if (IsMusicStreamPlaying(plug->music)) {
                PauseMusicStream(plug->music);
            } else {
                ResumeMusicStream(plug->music);
            }
        }

        if (IsKeyPressed(KEY_Q)) {
            StopMusicStream(plug->music);
            PlayMusicStream(plug->music);
        }
    }

    if (IsFileDropped()) {
        FilePathList files = LoadDroppedFiles();
        if (files.count > 0) {
            const char* mus_file_path = files.paths[0];

            if (IsMusicReady(plug->music)) {
                StopMusicStream(plug->music);
                UnloadMusicStream(plug->music);
            }

            music_init(mus_file_path);
        }
        UnloadDroppedFiles(files);
    }

    BeginDrawing();
    {
        ClearBackground(CLITERAL(Color){0x0, 0x0, 0x22, 0xFF});

        if (IsMusicReady(plug->music)) {
            size_t m = 0;
            float max_amp = 1.0f;

            // Honn Windowing
            for (size_t i = 0; i < N; ++i) {
                float t = (float)i / (N - 1);
                float hann = 0.5 - 0.5 * cosf(2 * PI * t);
                in_wd[i] = in_raw[i] * hann;
            }

            fft(in_wd, 1, out_raw, N);

            for (float f = LOW_FREQ; (size_t)f < N / 2; f = ceilf(f * FREQ_STEP)) {
                float f1 = ceilf(f * FREQ_STEP);
                float ampl = 0.0f;

                for (size_t q = (size_t)f; q < N / 2 && q < (size_t)f1; ++q) {
                    float a = amp(out_raw[q]);
                    if (ampl < a) ampl = a;
                }

                if (max_amp < ampl) max_amp = ampl;
                out_log[m++] = ampl;
            }

            float cell_width = (float)w / m;
            for (size_t i = 0; i < m; ++i) {
                out_log[i] /= max_amp;  // Normalize

                float t = out_log[i];
                Color c = ColorAlphaBlend(BLUE, ColorAlpha(RED, t), WHITE);
                DrawRectangle(i * cell_width, h / 2 - h / 3 * t, cell_width, h / 3 * t, c);
                DrawRectangle(i * cell_width, h / 2 - 1, cell_width, h / 3 * t, c);
            }
        } else {
            const char* msg = NULL;
            Color color;

            if (plug->error) {
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