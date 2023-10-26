#ifndef HELPERS_H__
#define HELPERS_H__

#define ASSERT assert
#define REALLOC realloc
#define FREE free

// Initial capacity of a dynamic array
#define DA_INIT_CAP 256

#define da_free(da) FREE((da)->items)

// Append an item to a dynamic array
#define da_append(da, item)                                                            \
    do {                                                                               \
        if ((da)->count >= (da)->capacity) {                                           \
            (da)->capacity = (da)->capacity == 0 ? DA_INIT_CAP : (da)->capacity * 2;   \
            (da)->items = REALLOC((da)->items, (da)->capacity * sizeof(*(da)->items)); \
            ASSERT((da)->items != NULL && "Buy more RAM lol");                         \
        }                                                                              \
                                                                                       \
        (da)->items[(da)->count++] = (item);                                           \
    } while (0)

#endif

static inline float amp(float complex v) {
    float a = crealf(v);
    float b = cimagf(v);
    return logf(a * a + b * b);
}

void remove_extension(char* filename) {
    char* dot = strrchr(filename, '.');
    if (dot) {
        *dot = '\0';
    }
}

float Vector2SumComponents(Vector2 vec) {
    return vec.x + vec.y;
}