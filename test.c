#include <stdio.h>
#include <math.h>

float pi;

int main() {
    pi = atan2f(1, 1)*4;

    size_t n = 8;
    float in[n];
    float out[n];

    for (size_t i = 0; i < n; ++i) {
        float t = (float)i/n;
        in[i] = sinf(2*pi*t) + sinf(2*pi*t*3);
    }

    for (size_t i = 0; i < n; ++i) {
        printf("%f\n", in[i]);
    }
}