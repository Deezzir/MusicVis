#include <assert.h>
#include <complex.h>
#include <math.h>
#include <raylib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_LEN(xs) sizeof(xs) / sizeof(xs[0])
#define N 256

typedef struct {
    float left;
    float right;
} Frame;

const int WIDTH = 800;
const int HEIGHT = 450;
const char* MUSIC_PATH = "The Tower of Dreams (new).ogg";

float in[N];
float complex out[N];
float max_amp;
Frame global_frames[4800 * 2];
size_t global_frames_count = 0;

float amp(float complex v) {
    float a = fabsf(crealf(v));
    float b = fabsf(cimagf(v));
    if (a < b) return b;
    return a;
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

void callback(void* bufferData, unsigned int frames) {
    if (frames < N) return;

    Frame* frame_data = bufferData;
    for (size_t i = 0; i < frames; ++i) {
        in[i] = frame_data[i].left;
    }

    fft(in, 1, out, N);

    max_amp = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        float a = amp(out[i]);
        if (max_amp < a) max_amp = a;
    }
}

int main(void) {
    InitWindow(WIDTH, HEIGHT, "Music Visualizer");
    SetTargetFPS(30);

    InitAudioDevice();
    Music music = LoadMusicStream(MUSIC_PATH);

    assert(music.stream.sampleSize == 16);
    assert(music.stream.channels == 2);
    printf("music.frameCount = %u\n", music.frameCount);
    printf("music.stream.sampleRate = %u\n", music.stream.sampleRate);
    printf("music.stream.sampleSize = %u\n", music.stream.sampleSize);
    printf("music.stream.channels = %u\n", music.stream.channels);

    PlayMusicStream(music);
    SetMusicVolume(music, 0.5f);
    AttachAudioStreamProcessor(music.stream, callback);

    while (!WindowShouldClose()) {
        UpdateMusicStream(music);

        if (IsKeyPressed(KEY_SPACE)) {
            if (IsMusicStreamPlaying(music)) {
                PauseMusicStream(music);
            } else {
                ResumeMusicStream(music);
            }
        }

        int w = GetRenderWidth();
        int h = GetRenderHeight();

        BeginDrawing();
        {
            ClearBackground(CLITERAL(Color){0x0, 0x0, 0x55, 0xFF});
            float cell_width = (float)w / N;
            for (size_t i = 0; i < N; ++i) {
                float t = amp(out[i]);
                DrawRectangle(i * cell_width, h / 2 - h / 2 * t, cell_width, h / 2 * t, RED);
            }
        }
        EndDrawing();
    }

    return 0;
}