/*
 * mmap_fault_cost.c - Costo de un minor page fault individual
 * Juan Diego Rojas Rey - 1152512
 *
 * Mapea 200 paginas anonimas y mide con clock_gettime el primer toque de cada
 * una (con page fault) y un segundo toque (sin fault).
 *
 * Compilar: gcc -o mmap_fault_cost mmap_fault_cost.c
 * Variante: gcc -DPOPULATE -o mmap_fault_cost_pop mmap_fault_cost.c
 *           (agrega MAP_POPULATE al mmap: las paginas se preparan dentro de mmap)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define N_PAGES 200

int main(void) {
    long page_size = sysconf(_SC_PAGESIZE);
    size_t total = (size_t)N_PAGES * page_size;

    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef POPULATE
    flags |= MAP_POPULATE;
#endif

    /* mmap anonimo: reserva direcciones virtuales (con POPULATE tambien las paginas) */
    struct timespec m0, m1;
    clock_gettime(CLOCK_MONOTONIC, &m0);
    char *base = mmap(NULL, total, PROT_READ | PROT_WRITE, flags, -1, 0);
    clock_gettime(CLOCK_MONOTONIC, &m1);
    if (base == MAP_FAILED) { perror("mmap"); return 1; }
    double ns_mmap = (m1.tv_sec - m0.tv_sec) * 1e9 + (m1.tv_nsec - m0.tv_nsec);

    struct timespec t0, t1;
    double sum_first = 0, sum_second = 0;

    for (int i = 0; i < N_PAGES; i++) {
        char *page = base + (size_t)i * page_size;

        /* Primer toque: sin POPULATE la pagina aun no tiene marco fisico -> minor page fault */
        clock_gettime(CLOCK_MONOTONIC, &t0);
        page[0] = 1;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        sum_first += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);

        /* Segundo toque: la pagina ya esta mapeada, no hay fault */
        clock_gettime(CLOCK_MONOTONIC, &t0);
        page[0] = 2;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        sum_second += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
    }

#ifdef POPULATE
    printf("Modo: MAP_POPULATE (paginas preparadas dentro de mmap)\n");
#else
    printf("Modo: demand paging (por defecto)\n");
#endif
    printf("Llamada mmap (%d paginas):                 %.1f ns\n", N_PAGES, ns_mmap);
    printf("Costo promedio primer toque (con fault):  %.1f ns\n", sum_first / N_PAGES);
    printf("Costo promedio segundo toque (sin fault): %.1f ns\n", sum_second / N_PAGES);
    printf("Overhead atribuible al page fault:        %.1f ns\n",
           (sum_first - sum_second) / N_PAGES);

    munmap(base, total);
    return 0;
}
