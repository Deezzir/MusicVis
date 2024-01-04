#ifndef HELPERS_H__
#define HELPERS_H__

// Get random integet in range [min, max)
static inline int rand_in_in_range(int min, int max) {
    return rand() % (max - min) + min;
}

static inline void ptr_swap(void** a, void** b) {
    void* tmp = *a;
    *a = *b;
    *b = tmp;
}

static inline void content_swap(void* a, void* b, size_t size) {
    char tmp[size];
    memcpy(tmp, a, size);
    memcpy(a, b, size);
    memcpy(b, tmp, size);
}

#define ASSERT assert
#define REALLOC realloc
#define FREE free

// Initial capacity of a dynamic array
#define DA_INIT_CAP 256

// Dynamic array Free
#define da_free(da) FREE((da)->items)

// Dynamic array Length
#define ARRAY_LEN(array) (sizeof(array) / sizeof(array[0]))

// Append an item to a dynamic array
#define da_append(da, item)                                                            \
    do {                                                                               \
        if ((da)->count >= (da)->capacity) {                                           \
            (da)->capacity = (da)->capacity == 0 ? DA_INIT_CAP : (da)->capacity * 2;   \
            (da)->items = REALLOC((da)->items, (da)->capacity * sizeof(*(da)->items)); \
            ASSERT((da)->items != NULL && "ERROR: WE NEED MORE RAM");                  \
        }                                                                              \
        (da)->items[(da)->count++] = (item);                                           \
    } while (0)

// Shuffle a dynamic array
#define da_shuffle(da, from, safe)                                          \
    do {                                                                    \
        for (size_t i = 0; i < (da)->count - 1; i++) {                      \
            if (i >= (size_t)(from)) {                                      \
                int j = rand_in_in_range(i, (da)->count);                   \
                ptr_swap((void**)&(da)->items[i], (void**)&(da)->items[j]); \
                da_append((safe), j);                                       \
            } else {                                                        \
                da_append((safe), i);                                       \
            }                                                               \
        }                                                                   \
    } while (0)

// Unshuffle a dynamic array
#define da_unshuffle(da, safe)                                          \
    do {                                                                \
        for (int i = count - 2; i >= 0; i--) {                          \
            int j = (safe)[i];                                          \
            ptr_swap((void**)&(da)->items[i], (void**)&(da)->items[j]); \
        }                                                               \
    } while (0)

// Remove an item by index from a dynamic array
#define da_remove(da, id)                                                                                       \
    do {                                                                                                        \
        if ((da)->count > 0 && (size_t)id < (da)->count) {                                                      \
            memmove((da)->items + id, (da)->items + (id + 1), ((da)->count - (id + 1)) * sizeof(*(da)->items)); \
            (da)->count--;                                                                                      \
        }                                                                                                       \
    } while (0)

// Get an item by index from a dynamic array
#define da_get_by_idx(da, id)                                 \
    do {                                                      \
        if (id < 0 || (size_t)id >= (da)->count) return NULL; \
        return &((da)->items[id]);                            \
    } while (0)

// Find an item by string key in a dynamic array of structs
static void* assoc_find_(void* items, size_t item_size, size_t items_count, size_t item_value_offset, const char* key) {
    for (size_t i = 0; i < items_count; i++) {
        char* item_start = (char*)items + item_size * i;
        const char* item_key = *(const char**)item_start;
        if (strcmp(item_key, key) == 0) {
            return item_start + item_value_offset;
        }
    }
    return NULL;
}

// Macro to wrap assoc_find_ for convenience
#define assoc_find(table, key)                                               \
    assoc_find_((table).items,                                               \
                sizeof((table).items[0]),                                    \
                (table).count,                                               \
                ((char*)&(table).items[0].value - (char*)&(table).items[0]), \
                (key))

// Calculate the amplitude of a complex number
static inline float amp(float complex v) {
    float a = crealf(v);
    float b = cimagf(v);
    return logf(a * a + b * b);
}

// Remove the extension from a filename
void remove_extension(char* filename) {
    char* dot = strrchr(filename, '.');
    if (dot) {
        *dot = '\0';
    }
}

// Calculate the sum of a vector(x + y)
float vec2_sum(Vector2 vec) {
    return vec.x + vec.y;
}

// Calculate the value of a slider from a y coordinate
float slider_get_value(float y, float hiy, float loy) {
    if (y < hiy) y = hiy;
    if (y > loy) y = loy;
    y -= hiy;
    y = 1 - y / (loy - hiy);
    return y;
}

#define DJB2_INIT 5381

// DJB2 hash function
uint64_t djb2(uint64_t hash, const void* buf, size_t len) {
    const uint8_t* bytes = buf;
    for (size_t i = 0; i < len; ++i) {
        hash = hash * 33 + bytes[i];
    }
    return hash;
}

uint64_t djb2_id(const char* file, int line) {
    uint64_t hash = DJB2_INIT;
    hash = djb2(hash, file, strlen(file));
    hash = djb2(hash, &line, sizeof(line));
    return hash;
}

#endif