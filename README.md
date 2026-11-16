# Post-contenido Unidad 10 — Jerarquía de memoria: caché y memoria virtual en Linux

**Estudiante:** Juan Diego Rojas Rey — código 1152512
**Materia:** Arquitectura de Computadores — Ingeniería de Sistemas, UFPS
**Repositorio:** `rojas-post1-u10` (una sola entrega para las dos partes)

Laboratorio guiado de la Unidad 10. La Parte 1 mide en C la latencia de acceso a memoria según el tamaño del arreglo y el patrón de acceso (secuencial, aleatorio y con stride) para ver los niveles de la caché. La Parte 2 inspecciona la memoria virtual de un proceso en Linux (`/proc/PID/maps`, `smaps`, `pmap`), cuenta los page faults y mide el costo de un fault individual con `mmap`.

## Objetivo, prerrequisitos y entorno

- **Objetivo:** relacionar lo visto en teoría (niveles de caché, asociatividad, prefetch, paginación por demanda) con mediciones reales en una máquina Linux, y justificar cuatro decisiones técnicas.
- **Prerrequisitos:** C básico, `gcc`, conocer el modelo de caché por conjuntos y la idea de página virtual de 4 KB.
- **Entorno usado:** Ubuntu en una máquina virtual con Intel Xeon a 2,10 GHz, 8 GB de RAM y `gcc` 13.3.0. Lo que reporta el sistema (ver capturas 1 de la Parte 1):

| Caché | Tamaño | Vías | Línea | Conjuntos |
|---|---|---|---|---|
| L1d | 48 KB | 12 | 64 B | 64 |
| L1i | 32 KB | 8 | 64 B | 64 |
| L2 | 2048 KB | 16 | 64 B | 2048 |
| L3 | 266240 KB (compartida) | 20 | 64 B | 212992 |

- Los directorios de trabajo fueron `~/u10post1` y `~/u10post2`, como pide la guía. Todas las capturas son de una terminal real (xterm) con las salidas reales de los programas.
- `perf` no se pudo usar porque las herramientas instaladas no corresponden al kernel de la máquina; por eso los fallos de página se cuentan con `getrusage`, `/proc` y `/usr/bin/time -v`.

## Contenido

| Archivo | Descripción | Compilar y ejecutar |
|---|---|---|
| `cache_bench.c` | Parte 1: benchmark de latencia (secuencial, aleatorio, stride, comparativa, latencia por punteros, conflictos) | `gcc -O0 -Wall -Wextra -o cache_bench cache_bench.c` y `./cache_bench [seq\|rand\|comp\|lat\|stride\|conflicto\|todo]` |
| `prefetch_test.c` | Apoyo a la decisión 2: suma sobre 64 MB en tres patrones | `gcc -O2 -fprefetch-loop-arrays -o pf_con prefetch_test.c` y `gcc -O2 -fno-prefetch-loop-arrays -o pf_sin prefetch_test.c` |
| `mymaps.c` | Parte 2: imprime direcciones de código, datos, heap y pila y se queda esperando | `gcc -o mymaps mymaps.c` y `./mymaps &` |
| `resumen_smaps.sh` | Parte 2: resume `smaps` por región (Rss, Private_Dirty, Shared_Clean) | `./resumen_smaps.sh PID [N]` |
| `pagefault_demo.c` | Parte 2: VmRSS y page faults antes y después de tocar 100 MB | `gcc -o pagefault_demo pagefault_demo.c` |
| `mmap_fault_cost.c` | Parte 2: costo del primer y segundo acceso a páginas de `mmap` | `gcc -o mmap_fault_cost mmap_fault_cost.c` (con `-DPOPULATE` usa `MAP_POPULATE`) |
| `mmap_rss.c` | Apoyo a la decisión 4: VmRSS al reservar 100 MB y usar 10 MB | `gcc -o mmap_rss mmap_rss.c` (con `-DPOPULATE` también) |
| `capturas/parte1/`, `capturas/parte2/` | Dos capturas por cada checkpoint | |

---

# Parte 1 — Latencia de caché

## Cómo funciona `cache_bench`

Las funciones `bench_seq`, `bench_rand` y `bench_stride` siguen el esqueleto de la guía (`clock_gettime(CLOCK_MONOTONIC)`, arreglo `volatile`, compilado con `-O0`). Se cambió una cosa: la guía repite cada medición un número fijo de veces, pero con 256 MB eso tarda demasiado, así que el número de repeticiones se ajusta al tamaño del arreglo para que todas las mediciones duren un tiempo parecido. Los índices del acceso aleatorio salen de un Fisher-Yates con semilla fija, de modo que el orden es reproducible. Los tamaños de caché no están escritos en el código: se leen de `/sys/devices/system/cpu/cpu0/cache/` y con eso se etiqueta cada fila (L1, L2, L3) y se marca con `<-- salto` donde la latencia sube de forma apreciable.

Además de las funciones de la guía se agregaron tres, porque con las originales no se veía bien lo que se quería mostrar (ver "Observaciones sobre la guía"): `bench_latencia` (recorrido de punteros encadenados en un orden aleatorio), `bench_stride_dep` (recorrido por stride con accesos dependientes) y `bench_conflicto` (anillo de N líneas separadas por un stride fijo).

## Checkpoint 1 — Tamaños de caché detectados

Capturas: `capturas/parte1/cp1_a_tamanos.png` y `cp1_b_geometria.png`.

El bucle de la guía sobre `/sys/devices/system/cpu/cpu0/cache/index{0..3}/size` da L1d = 48K, L1i = 32K, L2 = 2048K y L3 = 266240K. `index0` es la L1 de datos y `index1` la de instrucciones, por eso el "nivel 1" de la guía aparece como 32K y no como 48K; para los experimentos se usa `index0`, que es la que ve un arreglo de datos. La segunda captura agrega `lscpu` y la geometría de cada nivel (vías, línea y conjuntos), que es lo que se necesita después para calcular strides.

## Checkpoint 2 — Tabla de latencias

Capturas: `cp2_a_secuencial.png` (`./cache_bench seq`) y `cp2_b_latencia.png` (`./cache_bench lat`).

La tabla secuencial de la guía, medida en ns por byte con `-O0`, queda plana (entre 1,2 y 2,2 ns por byte en todos los tamaños) y casi no muestra los saltos entre niveles. Es lo esperable: con `-O0` y `volatile`, el costo de cada acceso lo domina la instrucción de carga y el control del bucle, y el recorrido secuencial lo cubre el prefetcher de hardware. Para ver la jerarquía se agregó la tabla de latencia por punteros encadenados (cada acceso depende del anterior, así no se pueden solapar):

| Array | Nivel | ns/acceso |
|---|---|---|
| 4–48 KB | L1 | ≈ 1,9–2,5 |
| 64–1024 KB | L2 | 5,9 → 8,0 |
| 2048 KB | L2 (límite) | 17,6 |
| 4096 KB | L3 | 35,2 |
| 16384 KB | L3 | 47,0 |
| 65536 KB | L3 nominal | 138,0 |
| 262144 KB | L3 nominal | 148,4 |

Se ven los saltos en los tamaños esperados: de ≈2 ns a ≈6 ns al pasar de 48 KB a 64 KB (sale de L1), de ≈8 ns a ≈18 y ≈35 ns al llegar a los 2 MB de L2, y un último salto grande (de 47 a 138 ns) entre 16 y 64 MB. Ese último salto aparece antes de los 260 MB que reporta el sistema como L3: la máquina es virtual y la L3 es compartida con otros inquilinos, así que la porción que realmente le queda a este proceso es mucho menor que el tamaño nominal. La cifra de 138–148 ns es del orden de una lectura a DRAM.

## Checkpoint 3 — Secuencial vs aleatorio

Capturas: `cp3_a_aleatorio.png` (`./cache_bench rand`) y `cp3_b_comparativa.png` (`./cache_bench comp`).

| Array | seq ns/acc | rand ns/acc | rand/seq |
|---|---|---|---|
| 4–32 KB (L1) | 1,7–2,4 | 1,7–2,4 | ≈ 1,0x |
| 1024 KB (L2) | 2,1 | 1,8 | 0,9x |
| 2048 KB | 1,3 | 3,0 | 2,3x |
| 4096 KB | 1,3 | 3,4 | 2,7x |
| 16384 KB | 1,5 | 8,1 | 5,4x |
| 65536 KB | 1,8 | 11,6 | 6,3x |
| 262144 KB | 1,9 | 18,8 | 10,0x |

Mientras el arreglo cabe en L1 y L2, recorrerlo en orden o desordenado cuesta lo mismo. Al pasar de L2 la diferencia crece de forma sostenida hasta ≈10x en 256 MB. El acceso secuencial se mantiene casi constante porque cada línea de 64 B trae 16 enteros (así que 15 de cada 16 accesos son aciertos) y además el prefetcher de hardware se adelanta; en el aleatorio cada acceso cae en una línea distinta, no hay localidad espacial y el prefetcher no puede predecir nada. Aun así el aleatorio medido (≈19 ns) es menor que la latencia dependiente (≈148 ns), porque en este caso los accesos son independientes y el procesador solapa varios fallos a la vez.

## Checkpoint 4 — Stride y asociatividad

Capturas: `cp4_a_stride.png` (`./cache_bench stride`) y `cp4_b_conflicto.png` (`./cache_bench conflicto`).

La prueba de la guía (stride de 64 B contra 4096 B sobre 1 MB) dio un cociente de 0,97x, o sea, sin diferencia. Con accesos independientes el procesador fuera de orden solapa los fallos y esconde el costo. Repitiendo con punteros encadenados el cociente pasó a 4,00x (1,85 ns contra 7,40 ns por acceso). Para aislar los conflictos de asociatividad se midió un anillo de 16 y de 64 líneas separadas por distintos strides:

| Stride | 16 líneas (ns) | 64 líneas (ns) |
|---|---|---|
| 64–512 B | ≈ 1,8–1,9 | ≈ 1,8–1,9 |
| 1024 B | 1,8 | 5,7 |
| 2048 B | 1,9 | 5,7 |
| 4096 B | 5,7 | 5,7 |
| ≥ 8192 B | 5,2–8,6 | 8,2–8,5 |

La L1d tiene 64 conjuntos de 64 B (el índice de conjunto son los bits 6–11 de la dirección) y 12 vías. Con stride 4096 B todas las líneas caen en el mismo conjunto, y 16 líneas superan las 12 vías, así que se expulsan unas a otras y la latencia sube de ≈1,8 ns (L1) a ≈5,7 ns (L2) aunque se usen solo 1 KB de datos. Con stride 2048 B esas 16 líneas se reparten en 2 conjuntos (8 por conjunto, caben en 12 vías) y no hay conflicto; con 64 líneas el conflicto aparece desde stride 1024 B (4 conjuntos, 16 líneas por conjunto, más que las 12 vías) y no con 512 B (8 conjuntos, 8 líneas por conjunto). Los resultados coinciden con el cálculo. Para strides de 8 KB o más la latencia pasa a ≈8 ns, que ya no se explica solo con la L1; probablemente intervienen conflictos en L2 y el costo de traducción (TLB), pero eso no se separó con una medición propia.

## Checkpoint 5 — Prefetch del compilador

Capturas: `cp5_a_prefetch_objdump.png` y `cp5_b_prefetch_tiempos.png`.

Se compiló `prefetch_test.c` con `-O2` con y sin `-fprefetch-loop-arrays`. `objdump -d | grep -c prefetch` da 5 instrucciones con la opción activa (`prefetchnta` y `prefetcht0`) y 0 con `-fno-prefetch-loop-arrays`. Tres corridas de cada binario sobre 64 MB:

| Patrón | con prefetch del compilador | sin prefetch del compilador |
|---|---|---|
| secuencial | 1,08–1,21 ns/acc | 0,51–0,59 ns/acc |
| aleatorio | 6,2–6,6 ns/acc | 6,2–6,8 ns/acc |
| stride 4096 B | 6,2–7,1 ns/acc | 6,0–7,0 ns/acc |

La diferencia clara solo está en el recorrido secuencial, y en sentido contrario al que se esperaría: con los prefetch del compilador el bucle fue unas 2 veces más lento. En ese patrón el prefetcher de hardware ya cubre el acceso, y las instrucciones de prefetch extra solo agregan trabajo. En aleatorio y stride no hay diferencia dentro del ruido de las corridas.

---

# Parte 2 — Memoria virtual en Linux

## Checkpoint 1 — Regiones de `/proc/PID/maps`

Capturas: `capturas/parte2/cp1_a_maps.png` y `cp1_b_regiones.png`.

Se ejecutó `./mymaps &` y se leyó `/proc/PID/maps` con el PID guardado en `$!`. Las direcciones que imprime el programa caen donde se espera:

| Elemento | Dirección impresa | Región en `maps` |
|---|---|---|
| `main` (.text) | `0x558bf9524229` | `558bf9524000-558bf9525000 r-xp` (código) |
| cadena (.rodata) | `0x558bf9525008` | `558bf9525000-558bf9526000 r--p` (solo lectura) |
| global inicializada (.data) | `0x558bf9527010` | `558bf9527000-558bf9528000 rw-p` |
| global sin inicializar (.bss) | `0x558bf952702c` | misma página `rw-p` que .data |
| heap (`malloc`) | `0x558c2f3412a0` | `558c2f341000-558c2f462000 rw-p [heap]` |
| variable local | `0x7ffed39d0b80` | `7ffed39b2000-7ffed39d7000 rw-p [stack]` |

Además aparecen las bibliotecas (`libc.so.6` con sus cuatro segmentos: solo lectura, ejecutable, solo lectura y datos), el cargador `ld-linux`, y las regiones especiales `[vvar]`, `[vdso]` y `[vsyscall]`. Las direcciones del ejecutable, el heap y la pila cambian en cada corrida por ASLR. La captura muestra `Stopped` al final: un proceso en segundo plano que intenta leer del terminal (`getchar`) recibe `SIGTTIN` y se detiene, pero sigue existiendo y su `maps` sigue siendo legible; después se terminó con `kill -9`.

## Checkpoint 2 — Clasificación de páginas con `smaps` y `pmap -x`

Capturas: `cp2_a_smaps.png` (`./resumen_smaps.sh $PID 10`) y `cp2_b_pmap.png` (`pmap -x $PID`).

El resumen por región, ordenado por Rss, muestra tres tipos de páginas:

- **Código y datos de solo lectura de bibliotecas** (por ejemplo el `r-xp` de `libc` con 1104 KB de Rss): Private_Dirty = 0 y Shared_Clean = Rss. Son páginas respaldadas por el archivo, limpias y compartidas con los demás procesos que usan la misma biblioteca.
- **Pila** (`[stack]`, 32 KB de Rss) y segmentos `rw-p` de datos: Private_Dirty = Rss y Shared_Clean = 0. Son páginas privadas que el proceso modificó.
- **Heap** (en `pmap -x`: 8 KB de RSS de 1156 KB reservados): se reserva mucho más de lo que se usa, solo las páginas tocadas están residentes.

`pmap -x` da la misma información en otro formato: 3736 KB de tamaño virtual total contra 1736 KB de RSS, y 132 KB de páginas sucias. Esa diferencia entre lo reservado y lo residente es justo lo que explica el Checkpoint 3.

## Checkpoint 3 — VmRSS y page faults

Capturas: `cp3_a_pagefault.png` (`./pagefault_demo`) y `cp3_b_time.png` (`/usr/bin/time -v`).

| Momento | VmSize | VmRSS | Minor faults | Major faults |
|---|---|---|---|---|
| después de `malloc(100 MB)` | 105112 kB | 1860 kB | 91 | 0 |
| después del `memset` | 105112 kB | 104260 kB | 25691 | 0 |

`malloc` reserva espacio virtual pero no memoria física: VmSize sube de inmediato y VmRSS casi no se mueve. Al recorrer el bloque con `memset`, cada página de 4 KB que se toca por primera vez produce un fallo menor, y la diferencia de fallos es 25691 − 91 = 25600 = 100 MB / 4 KB. VmRSS sube ≈102400 kB, es decir, los mismos 100 MB. No hubo fallos mayores porque no se leyó nada del disco. `/usr/bin/time -v` confirma el orden de magnitud: 26031 fallos menores en total para todo el proceso, 0 mayores, y un tamaño residente máximo de ≈103956 kB.

## Checkpoint 4 — Costo de un page fault con `mmap`

Capturas: `cp4_a_mmap.png` y `cp4_b_repeticiones.png`.

`mmap_fault_cost` reserva 200 páginas con `mmap` anónimo, toca cada una por primera vez (con fault) y luego otra vez (sin fault), y resta. Una corrida típica: primer toque ≈1617 ns, segundo toque ≈32 ns y overhead atribuible al page fault ≈1585 ns. En cinco corridas seguidas el overhead fue 1658, 2006, 2115, 1623 y 1519 ns, es decir, entre 1,5 y 2,1 µs por fallo (varía por tratarse de una máquina virtual compartida). Esos ≈1,6 µs son alrededor de 50 veces el costo de un acceso que ya está en memoria y se deben al paso por el kernel: excepción, búsqueda de la región, asignación y puesta a cero de una página física y actualización de la tabla de páginas.

## Checkpoint 5 — `MAP_POPULATE` frente a paginación por demanda

Capturas: `cp5_a_populate.png` y `cp5_b_rss.png`.

| | demand paging | `MAP_POPULATE` |
|---|---|---|
| llamada `mmap` (200 páginas) | ≈ 2,0 µs | ≈ 225 µs |
| primer toque | ≈ 1714 ns | ≈ 41,9 ns |
| overhead por page fault | ≈ 1,6 µs | ≈ 12,9 ns |

Con `MAP_POPULATE` el costo no desaparece, se mueve: el primer acceso ya no falla (≈42 ns, parecido al segundo toque), pero la llamada a `mmap` pasó de ≈2 µs a ≈225 µs porque el kernel prepara todas las páginas dentro de la llamada. Con `mmap_rss` sobre 100 MB: con demand paging `mmap` tarda 0,003 ms, VmRSS está en 1920 kB y sube a 12160 kB al usar 10 MB; con `MAP_POPULATE` tarda 41,95 ms y VmRSS queda en 104320 kB desde el principio, se use o no la memoria.

---

# Decisiones técnicas

## Decisión técnica 1 — ¿Qué stride maximiza los conflictos de asociatividad?

Se eligió un stride igual al tamaño de la caché dividido entre el número de vías, que en la L1d de esta máquina es 48 KB / 12 = 4096 B. En una caché asociativa por conjuntos, el conjunto lo determina una parte de la dirección (aquí los bits 6–11, porque hay 64 conjuntos de líneas de 64 B). Dos direcciones separadas por un múltiplo de 4096 B tienen el mismo índice de conjunto, así que, si se accede a más líneas con ese stride que vías hay (12), se expulsan entre sí aunque el resto de la caché esté vacía. Un stride de 64 B, en cambio, recorre los conjuntos de forma uniforme. La medición lo confirmó: con 16 líneas, el stride 2048 B se queda en ≈1,9 ns y el de 4096 B sube a ≈5,7 ns; con 64 líneas el salto aparece desde 1024 B. Un stride mayor que 4096 B ya no maximiza el efecto en L1 (sigue cayendo en el mismo conjunto, pero entran en juego L2 y el TLB), así que 4096 B es el más limpio para demostrarlo.

## Decisión técnica 2 — ¿`-fno-prefetch-loop-arrays` desactiva el prefetcher de hardware?

No. La opción solo le indica a `gcc` que no inserte instrucciones `prefetch` en los bucles que recorren arreglos; el prefetcher de hardware es un circuito del procesador que detecta patrones de acceso por su cuenta y no se controla desde el compilador (se maneja con registros del procesador o desde el BIOS). Se comprobó en el binario: con `-fprefetch-loop-arrays` aparecen 5 instrucciones `prefetchnta`/`prefetcht0` en `objdump` y con `-fno-prefetch-loop-arrays` aparecen 0. Aun así el recorrido secuencial sin prefetch del compilador siguió siendo muy rápido (≈0,55 ns por acceso), lo que indica que el hardware seguía anticipando las líneas; incluso fue casi el doble de rápido que la versión con prefetch de software, porque esas instrucciones agregan trabajo a un patrón que el hardware ya resolvía. Por eso, para medir latencia "pura" sin ayuda del prefetcher, no basta con esa bandera: hay que usar un patrón que el hardware no pueda predecir, como el aleatorio o el de punteros encadenados.

## Decisión técnica 3 — ¿`smaps` o `pmap -x` para clasificar las páginas?

Se eligió `/proc/PID/smaps` como fuente principal y `pmap -x` como vista de apoyo. `smaps` separa, para cada región, `Rss`, `Shared_Clean`, `Shared_Dirty`, `Private_Clean`, `Private_Dirty` y `Referenced`, y esas columnas son las que permiten decir si una página es compartida o privada y si fue modificada, que es exactamente la clasificación que pide la guía. `pmap -x` lee los mismos datos pero los resume en tres columnas (Kbytes, RSS y Dirty), y su columna Dirty junta las páginas sucias compartidas y las privadas, de modo que no se distingue entre ambas; además muestra el heap simplemente como `[ anon ]`. En cambio `pmap` es más cómodo de leer y trae un total al final (3736 KB virtuales y 1736 KB residentes en esta corrida). Como `smaps` es muy largo, se escribió `resumen_smaps.sh`, que deja solo las columnas relevantes por región y las ordena por Rss, y se contrastó el resultado con `pmap -x` para comprobar que los valores coinciden (por ejemplo, 1104 KB de RSS para el segmento de código de `libc` en ambos).

## Decisión técnica 4 — `MAP_POPULATE` o paginación por demanda

Se concluyó que depende de si se va a usar toda la región y de cuándo se quiere pagar el costo. Con paginación por demanda, `mmap` es casi instantáneo (≈2 µs para 200 páginas, 0,003 ms para 100 MB) y solo se paga un fallo (≈1,6 µs) por cada página que realmente se toca; si de 100 MB se usan solo 10, VmRSS queda en ≈12 MB. Con `MAP_POPULATE` el kernel prepara todas las páginas dentro de la llamada (≈225 µs para 200 páginas, ≈42 ms para 100 MB) y los accesos posteriores ya no fallan (≈42 ns en el primer toque); la contrapartida es que VmRSS salta a ≈104 MB aunque no se use la memoria. Por lo tanto, `MAP_POPULATE` conviene cuando se sabe que se va a recorrer toda la región y se quiere evitar latencia impredecible durante el cálculo (por ejemplo, un buffer que se llena de inmediato o código con requisitos de tiempo), y la paginación por demanda conviene cuando la región es grande y se usa de forma parcial o dispersa, porque no consume memoria física que no se necesita.

---

# Observaciones sobre la guía

- **Tabla secuencial con `-O0`:** la versión de la guía no muestra saltos claros entre niveles (queda entre 1,2 y 2,2 ns por byte). Se dejó tal cual y se agregó la tabla de latencia por punteros, que sí los muestra.
- **Stride con accesos independientes:** la prueba de la guía dio 0,97x; se agregó la versión con dependencias (4,00x) y la de conflictos por conjunto.
- **Repeticiones fijas:** se cambiaron por repeticiones adaptativas para que los arreglos grandes no tarden demasiado.
- **`/proc/self` en `pagefault_demo`:** al usarlo con `system("grep ... /proc/self/status")`, el `self` pasa a ser el proceso `grep` y no el programa, así que se construye la ruta con `getpid()`.
- **`./mymaps &` en una shell interactiva:** el proceso queda detenido por `SIGTTIN` al hacer `getchar`; no impide leer `maps`, pero un `kill` normal queda pendiente, por eso se terminó con `kill -9`.
- **`perf` y `/usr/bin/time`:** `perf` no está disponible para este kernel y `/usr/bin/time` no venía instalado (se instaló el paquete `time`).
- **L3 en máquina virtual:** el tamaño de L3 que reporta el sistema (260 MB) es compartido, por eso el salto a latencia de DRAM aparece con arreglos menores a ese tamaño.
