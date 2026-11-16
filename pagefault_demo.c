/*
 * pagefault_demo.c - Asignacion diferida (demand paging) y page faults
 * Juan Diego Rojas Rey - 1152512
 *
 * Reserva 100 MB con malloc, muestra VmSize/VmRSS antes y despues de tocar
 * todas las paginas con memset, y la cantidad de page faults (getrusage).
 *
 * Nota: la guia usaba system("grep ... /proc/self/status"), pero ahi "self" es
 * el proceso grep que lanza system(), no este programa. Por eso se arma la ruta
 * con el PID real (getpid).
 *
 * Compilar: gcc -o pagefault_demo pagefault_demo.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>

static void mostrar_estado(const char *etiqueta) {
    char cmd[128];
    printf("--- %s ---\n", etiqueta);
    fflush(stdout);                                   /* que no se mezcle con la salida de grep */
    snprintf(cmd, sizeof cmd,
             "grep -E 'VmSize|VmRSS|VmPeak' /proc/%d/status", (int)getpid());
    system(cmd);
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    printf("minor faults acumulados: %ld   major faults: %ld\n\n",
           ru.ru_minflt, ru.ru_majflt);
}

int main(void) {
    size_t N = 100 * 1024 * 1024;  /* 100 MB */
    char *buf = (char *)malloc(N);
    if (!buf) { perror("malloc"); return 1; }

    /* Antes de tocar la memoria: reservada (VmSize) pero casi no residente (VmRSS) */
    mostrar_estado("despues de malloc(100 MB), antes de tocar la memoria");

    /* Forzar page faults escribiendo en cada pagina */
    memset(buf, 0xAA, N);

    /* Despues: VmRSS debe haber crecido ~100 MB */
    mostrar_estado("despues del memset");

    free(buf);
    return 0;
}
