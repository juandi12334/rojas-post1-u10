/*
 * mmap_rss.c - Cuanta memoria fisica se gasta al reservar 100 MB y usar solo 10 MB
 * Juan Diego Rojas Rey - 1152512
 *
 * Programa de apoyo para la decision tecnica 4 de la Parte 2.
 * Compilar: gcc -o mmap_rss mmap_rss.c
 *           gcc -DPOPULATE -o mmap_rss_pop mmap_rss.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#define RESERVA (100UL * 1024 * 1024)
#define USO     (10UL * 1024 * 1024)

static void rss(const char *etiqueta) {
    char cmd[128];
    fflush(stdout);
    snprintf(cmd, sizeof cmd, "grep VmRSS /proc/%d/status | sed 's/^/%s: /'", (int)getpid(), etiqueta);
    system(cmd);
}

int main(void) {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef POPULATE
    flags |= MAP_POPULATE;
    printf("Modo: MAP_POPULATE\n");
#else
    printf("Modo: demand paging (por defecto)\n");
#endif
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    char *p = mmap(NULL, RESERVA, PROT_READ | PROT_WRITE, flags, -1, 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    printf("mmap de 100 MB tardo %.3f ms\n",
           ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e6);
    rss("tras mmap de 100 MB");
    memset(p, 1, USO);                       /* solo se usan 10 MB */
    rss("tras usar 10 MB    ");
    munmap(p, RESERVA);
    return 0;
}
