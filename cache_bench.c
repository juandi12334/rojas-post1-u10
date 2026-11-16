/*
 * cache_bench.c - Benchmark de la jerarquia de cache (Unidad 10, Parte 1)
 * Juan Diego Rojas Rey - 1152512
 *
 * Mide la latencia de acceso a memoria segun el tamano del array:
 *   - bench_seq:    acceso secuencial byte a byte (ns/byte)
 *   - bench_rand:   acceso aleatorio con indices mezclados (Fisher-Yates)
 *   - bench_stride: acceso con stride fijo sobre un array de 1 MB
 *   - bench_latencia: (adicional) latencia real por nivel con punteros encadenados
 *   - bench_stride_dep: (adicional) el recorrido con stride, pero con cada acceso
 *                   dependiendo del anterior, para que no se solapen los fallos
 *   - bench_conflicto: (adicional) recorre pocas lineas separadas por un stride
 *                   y encadenadas con punteros, para aislar los conflict misses
 *
 * Compilar: gcc -O0 -o cache_bench cache_bench.c
 * Uso:      ./cache_bench [seq|rand|comp|lat|stride|conflicto|todo]
 *           (sin argumento ejecuta todo, como en el enunciado)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REPEAT         100          /* repeticiones del enunciado (tope para arrays chicos) */
#define REPEAT_MIN     2            /* minimo de pasadas para arrays grandes */
#define ACCESOS_SEQ    200000000UL  /* accesos aproximados por medicion secuencial */
#define ACCESOS_RAND   30000000UL   /* idem aleatorio (cada acceso cuesta mucho mas) */

/* Tamanos de prueba en KB */
static const size_t tamanos_kb[] = {
    4, 8, 16, 32,                    /* region L1 */
    48,                              /* limite de la L1d de esta maquina */
    64, 128, 256, 512, 1024,         /* region L2 */
    2048,                            /* limite de la L2 */
    4096, 16*1024, 64*1024,          /* region L3 */
    256*1024                         /* mas alla de la L3: RAM */
};
#define N_TAMANOS ((int)(sizeof(tamanos_kb) / sizeof(tamanos_kb[0])))

/* ---------- utilidades ---------- */

static double dif_ns(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec);
}

/* numero de pasadas: las que hagan falta para ~'accesos' accesos en total */
static int repeticiones(size_t elementos, size_t accesos) {
    size_t r = accesos / elementos;
    if (r > REPEAT * 100UL) r = REPEAT * 100UL;
    if (r < REPEAT_MIN) r = REPEAT_MIN;
    return (int)r;
}

/* tamano en bytes de la cache del indice i (lee /sys), 0 si no existe */
static size_t tam_cache(int indice) {
    char ruta[128], buf[32];
    snprintf(ruta, sizeof ruta,
             "/sys/devices/system/cpu/cpu0/cache/index%d/size", indice);
    FILE *f = fopen(ruta, "r");
    if (!f) return 0;
    size_t v = 0;
    if (fgets(buf, sizeof buf, f)) v = strtoul(buf, NULL, 10) * 1024;  /* "48K" */
    fclose(f);
    return v;
}

/* nivel de la jerarquia en el que cabe un array de n bytes */
static const char *nivel_de(size_t n, size_t l1, size_t l2, size_t l3) {
    if (n <= l1) return "L1";
    if (n <= l2) return "L2";
    if (n <= l3) return "L3";
    return "RAM";
}

/* ---------- benchmarks ---------- */

/* Medir tiempo de acceso secuencial a array de N bytes (ns/byte) */
double bench_seq(size_t n_bytes) {
    volatile char *arr = (volatile char *)malloc(n_bytes);
    if (!arr) return -1.0;
    memset((void *)arr, 1, n_bytes);        /* toca todas las paginas antes de medir */

    int reps = repeticiones(n_bytes, ACCESOS_SEQ);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int r = 0; r < reps; r++)
        for (size_t i = 0; i < n_bytes; i++)
            (void)arr[i];                   /* acceso de lectura (volatile: no se elimina) */

    clock_gettime(CLOCK_MONOTONIC, &t1);
    free((void *)arr);

    return dif_ns(t0, t1) / ((double)reps * (double)n_bytes);   /* ns/byte */
}

/*
 * Recorre arr[idx[i]] con indices en orden o mezclados (Fisher-Yates).
 * Con mezclar=1 es el acceso aleatorio del enunciado; con mezclar=0 es el
 * mismo codigo con indices ordenados, que sirve de referencia justa
 * (mismo numero de instrucciones por acceso) para comparar.
 * Devuelve ns por acceso a un int.
 */
static double bench_indices(size_t n_bytes, int mezclar) {
    size_t n = n_bytes / sizeof(int);
    int *arr = (int *)malloc(n * sizeof(int));
    size_t *idx = (size_t *)malloc(n * sizeof(size_t));
    if (!arr || !idx) return -1.0;

    for (size_t i = 0; i < n; i++) { arr[i] = (int)i; idx[i] = i; }
    if (mezclar) {
        /* Mezcla Fisher-Yates */
        for (size_t i = n - 1; i > 0; i--) {
            size_t j = (size_t)rand() % (i + 1);
            size_t tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp;
        }
    }

    int reps = repeticiones(n, ACCESOS_RAND);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    volatile long sum = 0;
    for (int r = 0; r < reps; r++)
        for (size_t i = 0; i < n; i++)
            sum += arr[idx[i]];
    clock_gettime(CLOCK_MONOTONIC, &t1);

    free(arr); free(idx);
    return dif_ns(t0, t1) / ((double)reps * (double)n);
}

/* Acceso aleatorio usando indices pre-mezclados (Fisher-Yates) */
double bench_rand(size_t n_bytes) { return bench_indices(n_bytes, 1); }

/* Acceso con stride fijo: expone conflictos de asociatividad */
double bench_stride(size_t n_bytes, size_t stride) {
    volatile char *arr = (volatile char *)malloc(n_bytes);
    if (!arr) return -1.0;
    memset((void *)arr, 1, n_bytes);

    size_t steps = n_bytes / stride;
    int reps = repeticiones(steps, 20000000UL);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int r = 0; r < reps; r++)
        for (size_t i = 0; i < steps; i++)
            (void)arr[(i * stride) % n_bytes];

    clock_gettime(CLOCK_MONOTONIC, &t1);
    free((void *)arr);

    return dif_ns(t0, t1) / ((double)reps * (double)steps);
}

/*
 * Version adicional para aislar los conflict misses: se usan solo 'n_lineas'
 * lineas de cache (un conjunto de trabajo diminuto, que cabe de sobra en la L1)
 * separadas 'stride' bytes, enlazadas en un anillo aleatorio y recorridas
 * siguiendo punteros. Cada acceso depende del anterior, asi que el tiempo por
 * paso es la latencia real del nivel que sirve la linea. Lo unico que cambia
 * entre llamadas es el stride, es decir, en que sets caen las lineas.
 */
double bench_conflicto(size_t n_lineas, size_t stride) {
    char *mem;
    if (posix_memalign((void **)&mem, 4096, n_lineas * stride)) return -1.0;

    size_t *orden = (size_t *)malloc(n_lineas * sizeof(size_t));
    for (size_t i = 0; i < n_lineas; i++) orden[i] = i;
    for (size_t i = n_lineas - 1; i > 0; i--) {         /* anillo aleatorio */
        size_t j = (size_t)rand() % (i + 1);
        size_t t = orden[i]; orden[i] = orden[j]; orden[j] = t;
    }
    for (size_t i = 0; i < n_lineas; i++) {
        void **actual = (void **)(mem + orden[i] * stride);
        *actual = mem + orden[(i + 1) % n_lineas] * stride;
    }

    const long pasos = 20000000L;
    void *volatile p = mem + orden[0] * stride;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long i = 0; i < pasos; i++)
        p = *(void *volatile *)p;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    free(orden); free(mem);
    return dif_ns(t0, t1) / (double)pasos;
}

/*
 * Latencia real de cada nivel: un anillo de lineas (una cada 64 bytes) en orden
 * aleatorio recorrido con punteros. Cada acceso depende del anterior (no se puede
 * adelantar ni prefetchear), asi que ns/paso = latencia del nivel donde cabe el array.
 */
double bench_latencia(size_t n_bytes) {
    size_t n_lineas = n_bytes / 64;
    char *mem;
    if (posix_memalign((void **)&mem, 4096, n_bytes)) return -1.0;
    memset(mem, 0, n_bytes);

    size_t *orden = (size_t *)malloc(n_lineas * sizeof(size_t));
    for (size_t i = 0; i < n_lineas; i++) orden[i] = i;
    for (size_t i = n_lineas - 1; i > 0; i--) {
        size_t j = (size_t)rand() % (i + 1);
        size_t t = orden[i]; orden[i] = orden[j]; orden[j] = t;
    }
    for (size_t i = 0; i < n_lineas; i++)
        *(void **)(mem + orden[i] * 64) = mem + orden[(i + 1) % n_lineas] * 64;

    long pasos = 20000000L;
    if (n_bytes >= 64UL * 1024 * 1024) pasos = 5000000L;   /* arrays enormes: menos pasos */
    void *volatile p = mem + orden[0] * 64;
    for (long i = 0; i < 1000000L && i < (long)n_lineas * 2; i++)   /* calentamiento */
        p = *(void *volatile *)p;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long i = 0; i < pasos; i++)
        p = *(void *volatile *)p;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    free(orden); free(mem);
    return dif_ns(t0, t1) / (double)pasos;
}

/*
 * Mismo recorrido que bench_stride (posicion 0, stride, 2*stride, ... dando la
 * vuelta al array) pero con punteros: cada acceso depende del anterior.
 */
double bench_stride_dep(size_t n_bytes, size_t stride) {
    char *mem;
    if (posix_memalign((void **)&mem, 4096, n_bytes)) return -1.0;
    memset(mem, 0, n_bytes);
    size_t steps = n_bytes / stride;
    for (size_t i = 0; i < steps; i++)
        *(void **)(mem + i * stride) = mem + ((i + 1) % steps) * stride;

    const long pasos = 20000000L;
    void *volatile p = mem;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long i = 0; i < pasos; i++)
        p = *(void *volatile *)p;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    free(mem);
    return dif_ns(t0, t1) / (double)pasos;
}

/* ---------- salidas ---------- */

static void tabla_latencia(size_t l1, size_t l2, size_t l3) {
    printf("Latencia por nivel (acceso dependiente, ns por acceso)\n");
    printf("%-12s  %-5s  %10s\n", "Array (KB)", "Nivel", "ns/acceso");
    double prev = 0;
    for (int i = 0; i < N_TAMANOS; i++) {
        size_t b = tamanos_kb[i] * 1024;
        double lat = bench_latencia(b);
        printf("%-12zu  %-5s  %10.3f%s\n", tamanos_kb[i], nivel_de(b, l1, l2, l3), lat,
               (prev > 0 && lat > prev * 1.5) ? "   <-- salto" : "");
        prev = lat;
    }
}

static void tabla_seq(size_t l1, size_t l2, size_t l3) {
    printf("Acceso secuencial (ns/byte)\n");
    printf("%-12s  %-5s  %10s\n", "Array (KB)", "Nivel", "ns/byte");
    double prev = 0;
    for (int i = 0; i < N_TAMANOS; i++) {
        size_t b = tamanos_kb[i] * 1024;
        double lat = bench_seq(b);
        printf("%-12zu  %-5s  %10.3f%s\n", tamanos_kb[i], nivel_de(b, l1, l2, l3), lat,
               (prev > 0 && lat > prev * 1.25) ? "   <-- salto" : "");
        prev = lat;
    }
}

static void tabla_rand(size_t l1, size_t l2, size_t l3) {
    printf("Acceso aleatorio (ns/acceso a un int)\n");
    printf("%-12s  %-5s  %10s\n", "Array (KB)", "Nivel", "ns/acceso");
    double prev = 0;
    for (int i = 0; i < N_TAMANOS; i++) {
        size_t b = tamanos_kb[i] * 1024;
        double lat = bench_rand(b);
        printf("%-12zu  %-5s  %10.3f%s\n", tamanos_kb[i], nivel_de(b, l1, l2, l3), lat,
               (prev > 0 && lat > prev * 1.25) ? "   <-- salto" : "");
        prev = lat;
    }
}

static void tabla_comparativa(size_t l1, size_t l2, size_t l3) {
    printf("Secuencial vs aleatorio (mismo codigo, indices en orden o mezclados)\n");
    printf("%-12s  %-5s  %12s  %12s  %10s\n", "Array (KB)", "Nivel",
           "seq ns/acc", "rand ns/acc", "rand/seq");
    for (int i = 0; i < N_TAMANOS; i++) {
        size_t b = tamanos_kb[i] * 1024;
        double s = bench_indices(b, 0);
        double r = bench_indices(b, 1);
        printf("%-12zu  %-5s  %12.3f  %12.3f  %9.1fx\n", tamanos_kb[i],
               nivel_de(b, l1, l2, l3), s, r, r / s);
    }
}

static void tabla_stride(void) {
    printf("\n%-12s  %10s\n", "Stride", "ns/acceso");
    double t_line = bench_stride(1024 * 1024, 64);
    double t_page = bench_stride(1024 * 1024, 4096);
    printf("%-12s  %10.3f\n", "64 B", t_line);
    printf("%-12s  %10.3f\n", "4096 B", t_page);
    printf("Cociente 4096 B / 64 B: %.2fx\n", t_page / t_line);

    /* mismo recorrido pero con cada acceso dependiendo del anterior */
    double d_line = bench_stride_dep(1024 * 1024, 64);
    double d_page = bench_stride_dep(1024 * 1024, 4096);
    printf("\nCon accesos dependientes (punteros encadenados):\n");
    printf("%-12s  %10s\n", "Stride", "ns/acceso");
    printf("%-12s  %10.3f\n", "64 B", d_line);
    printf("%-12s  %10.3f\n", "4096 B", d_page);
    printf("Cociente 4096 B / 64 B: %.2fx\n", d_page / d_line);
}

static void tabla_conflicto(void) {
    static const size_t strides[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192,
                                     16384, 32768, 65536, 131072};
    printf("Latencia por paso con punteros encadenados (ns), 16 y 64 lineas\n");
    printf("%-10s  %10s  %10s\n", "Stride", "16 lineas", "64 lineas");
    for (size_t i = 0; i < sizeof(strides) / sizeof(strides[0]); i++) {
        double a = bench_conflicto(16, strides[i]);
        double b = bench_conflicto(64, strides[i]);
        printf("%-10zu  %10.3f  %10.3f\n", strides[i], a, b);
    }
}

int main(int argc, char **argv) {
    const char *modo = (argc > 1) ? argv[1] : "todo";
    size_t l1 = tam_cache(0), l2 = tam_cache(2), l3 = tam_cache(3);
    srand(12345);

    if (!strcmp(modo, "seq") || !strcmp(modo, "todo")) tabla_seq(l1, l2, l3);
    if (!strcmp(modo, "rand") || !strcmp(modo, "todo")) {
        if (!strcmp(modo, "todo")) printf("\n");
        tabla_rand(l1, l2, l3);
    }
    if (!strcmp(modo, "comp")) tabla_comparativa(l1, l2, l3);
    if (!strcmp(modo, "lat")) tabla_latencia(l1, l2, l3);
    if (!strcmp(modo, "stride") || !strcmp(modo, "todo")) tabla_stride();
    if (!strcmp(modo, "conflicto") || !strcmp(modo, "todo")) {
        printf("\n");
        tabla_conflicto();
    }
    return 0;
}
