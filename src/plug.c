#include "plug.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define N (1 << 13)
#define FREQ_STEP 1.06

typedef struct {
    float left;
    float right;
} Frame;

float in[N];
float complex out[N];

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

float log_base(float x, float b) {
    return logf(x) / logf(b);
}

float amp(float complex v) {
    float a = fabsf(crealf(v));
    float b = fabsf(cimagf(v));
    if (a < b) return b;
    return a;
}

void callback(void* bufferData, unsigned int frames) {
    Frame* frame_data = bufferData;
    for (size_t i = 0; i < frames; ++i) {
        memmove(in, in + 1, (N - 1) * sizeof(in[0]));
        in[N - 1] = frame_data[i].left;
    }
}

void plug_init(Plug* plug, const char* file_path) {
    plug->music = LoadMusicStream(file_path);

    assert(plug->music.stream.sampleSize == 16);
    assert(plug->music.stream.channels == 2);
    printf("music.frameCount = %u\n", plug->music.frameCount);
    printf("music.stream.sampleRate = %u\n", plug->music.stream.sampleRate);
    printf("music.stream.sampleSize = %u\n", plug->music.stream.sampleSize);
    printf("music.stream.channels = %u\n", plug->music.stream.channels);

    SetMusicVolume(plug->music, 0.5f);
    PlayMusicStream(plug->music);
    AttachAudioStreamProcessor(plug->music.stream, callback);

    plug->freq_bins = (size_t)log_base(N / 20, FREQ_STEP);
    printf("freq_bins = %zu\n", plug->freq_bins);
}

void plug_pre_reload(Plug* plug) {
    DetachAudioStreamProcessor(plug->music.stream, callback);
}

void plug_post_reload(Plug* plug) {
    AttachAudioStreamProcessor(plug->music.stream, callback);
}

void plug_update(Plug* plug) {
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

    int w = GetRenderWidth();
    int h = GetRenderHeight();

    BeginDrawing();
    {
        ClearBackground(CLITERAL(Color){0x0, 0x0, 0x22, 0xFF});

        fft(in, 1, out, N);
        float max_amp = 0.0f;
        for (size_t i = 0; i < N; ++i) {
            float a = amp(out[i]);
            if (max_amp < a) max_amp = a;
        }

        float cell_width = (float)w / plug->freq_bins;
        size_t m = 0;

        for (float f = 20.0f; (size_t)f < N; f *= FREQ_STEP) {
            float f1 = f * FREQ_STEP;
            float a = 0.0f;

            for (size_t q = (size_t)f; q < N && q < (size_t)f1; ++q) {
                a += amp(out[q]);
            }
            a /= (size_t)f1 - (size_t)f + 1;

            float t = a / max_amp;
            Color c = ColorAlphaBlend(RED, ColorAlpha(BLUE, t), WHITE);
            DrawRectangle(m * cell_width, h / 2 - h / 2 * t, cell_width, h / 2 * t, c);
            m += 1;
        }
    }
    EndDrawing();
}