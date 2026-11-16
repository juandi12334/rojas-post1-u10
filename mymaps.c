/*
 * mymaps.c - Programa minimo para inspeccionar el mapa de memoria de un proceso
 * Juan Diego Rojas Rey - 1152512
 *
 * Imprime direcciones de una variable global, de la pila y del heap y se queda
 * esperando una tecla con getchar(), para que el proceso siga vivo mientras se
 * consulta /proc/PID/maps.
 *
 * Compilar: gcc -o mymaps mymaps.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int global_data = 42;          /* segmento .data */
int global_bss;                /* segmento .bss (sin inicializar) */
const char *msg = "hola";      /* el puntero va en .data, la cadena "hola" en .rodata */

int main(void) {
    void *heap = malloc(1024 * 1024);  /* 1 MB en heap */
    printf("PID: %d\n", getpid());
    printf("main (.text):     %p\n", (void *)main);
    printf("cadena (.rodata): %p\n", (const void *)msg);
    printf("global (.data):   %p\n", (void *)&global_data);
    printf("global (.bss):    %p\n", (void *)&global_bss);
    printf("heap addr:        %p\n", heap);
    printf("stack var addr:   %p\n", (void *)&heap);
    printf("Presione Enter para continuar...\n");
    fflush(stdout);
    getchar();
    free(heap);
    return 0;
}
