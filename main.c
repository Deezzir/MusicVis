#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include <raylib.h>

#define ARRAY_LEN(xs) sizeof(xs)/sizeof(xs[0])

typedef struct {
    float left;
    float right;
} Frame;

const int WIDTH = 800;
const int HEIGHT = 450;
const char* MUSIC_PATH = "The Tower of Dreams (new).ogg";

Frame global_frames[4800*2];
size_t global_frames_count = 0;

void callback(void *bufferData, unsigned int frames) {
    size_t capacity = ARRAY_LEN(global_frames);

    if (frames <= capacity - global_frames_count) {
        memcpy(global_frames + global_frames_count, bufferData, sizeof(Frame)*frames);
        global_frames_count += frames;
    } else if (frames <= capacity) {
        memmove(global_frames, global_frames + frames, sizeof(Frame)*(capacity - frames));
        memcpy(global_frames + (capacity - frames), bufferData, sizeof(Frame)*frames);
    } else {
        memcpy(global_frames, bufferData, sizeof(Frame)*capacity);
        global_frames_count = capacity;
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
            ClearBackground(CLITERAL(Color) {0x0, 0x0, 0x55, 0xFF});
            float cell_width = (float)w / global_frames_count;
            for (size_t i = 0; i < global_frames_count; ++i) {
                float t = global_frames[i].left;
                if (t > 0.0f) {
                    DrawRectangle(i*cell_width, h/2 - h/2*t, 1, h/2*t, RED);
                } else {
                    DrawRectangle(i*cell_width, h/2, 1, h/2*t, RED);
                } 
            }
        }
        EndDrawing();
    }

    return 0;
}