#include <stdint.h>

#include <iostream>

#define N 13

volatile uint64_t A[N];
volatile uint64_t B[N];
volatile uint64_t C[N];

int main(void) {
    bool ret = true;
    for (int i = 0; i < N; i++) {
        A[i] = 4;
    }
    for (int i = 0; i < N; i++) {
        B[i] = 8;
    }
    for (int i = 0; i < N; i++) {
        C[i] = A[i] + B[i];
    }
    for (int i = 0; i < N; i++) {
        ret &= (C[i] == 12);
    }
    return ret ? 0 : 1;
}
