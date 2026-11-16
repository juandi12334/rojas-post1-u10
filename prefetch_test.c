/*
 * prefetch_test.c - Efecto del prefetch por software de gcc en los tres patrones
 * Juan Diego Rojas Rey - 1152512
 *
 * Programa de apoyo para la decision tecnica 2 de la Parte 1. Los bucles de
 * cache_bench.c usan volatile (para que -O0/-O2 no los eliminen) y gcc no inserta
 * prefetch sobre accesos volatile, asi que aqui se repiten los tres patrones con
 * arrays normales, donde el compilador si puede decidir si inserta prefetch.
 *
 * Compilar: gcc -O2 -fprefetch-loop-arrays    -o pf_con prefetch_test.c
 *           gcc -O2 -fno-prefetch-loop-arrays -o pf_sin prefetch_test.c
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N_BYTES (64UL * 1024 * 1024)        /* 64 MB: mayor que la L2 */
#define N       (N_BYTES / sizeof(int))
#define PASADAS 8

static double ahora(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}

/* patron secuencial: el que el compilador puede predecir */
long suma_secuencial(const int *a, size_t n) {
    long s = 0;
    for (size_t i = 0; i < n; i++) s += a[i];
    return s;
}

/* patron aleatorio: la direccion sale de otro array (indices mezclados) */
long suma_aleatoria(const int *a, const unsigned *idx, size_t n) {
    long s = 0;
    for (size_t i = 0; i < n; i++) s += a[idx[i]];
    return s;
}

/* patron con stride de 4096 bytes (1024 ints), dando varias vueltas al array */
long suma_stride(const int *a, size_t n, size_t stride_ints) {
    long s = 0;
    for (size_t desfase = 0; desfase < stride_ints; desfase += 64)   /* otra linea cada vuelta */
        for (size_t i = desfase; i < n; i += stride_ints) s += a[i];
    return s;
}

int main(void) {
    int *a = (int *)malloc(N_BYTES);
    unsigned *idx = (unsigned *)malloc(N * sizeof(unsigned));
    if (!a || !idx) return 1;
    for (size_t i = 0; i < N; i++) { a[i] = (int)i; idx[i] = (unsigned)i; }
    srand(12345);
    for (size_t i = N - 1; i > 0; i--) {
        size_t j = (size_t)rand() % (i + 1);
        unsigned t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }

    volatile long sink = 0;
    double t0, t1;
    t0 = ahora(); for (int r = 0; r < PASADAS; r++) sink += suma_secuencial(a, N); t1 = ahora();
    printf("%-22s %8.3f ns/acceso\n", "secuencial", (t1 - t0) / ((double)PASADAS * N));

    t0 = ahora(); for (int r = 0; r < PASADAS; r++) sink += suma_aleatoria(a, idx, N); t1 = ahora();
    printf("%-22s %8.3f ns/acceso\n", "aleatorio", (t1 - t0) / ((double)PASADAS * N));

    size_t stride = 1024;                                   /* 4096 bytes */
    size_t accesos = (N / stride) * (stride / 64);
    t0 = ahora(); for (int r = 0; r < PASADAS; r++) sink += suma_stride(a, N, stride); t1 = ahora();
    printf("%-22s %8.3f ns/acceso\n", "stride 4096 B", (t1 - t0) / ((double)PASADAS * accesos));

    free(a); free(idx);
    return 0;
}
