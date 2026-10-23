# Decisión Técnica de Ingeniería: Mecanismo de Ejecución de Lógica CPU

**Fecha:** 23 de octubre de 2026  
**Proyecto:** Port nativo de Gravity Rush Remastered (CUSA01130) para PC  
**Estado:** Aprobado / Referencia de Arquitectura  
**Tarea:** N03 (Prioridad P0)

---

## 1. Contexto y planteamiento del problema

El objetivo del proyecto es permitir al usuario jugar su copia legítima de *Gravity Rush Remastered* (CUSA01130) en su PC (Windows x64) utilizando un runtime propio, sin recurrir a un emulador externo como producto final.

Para ejecutar la lógica del juego compilada originalmente para PlayStation 4 (arquitectura x86-64 AMD64 bajo Orbis OS), existen dos vías teóricas en ingeniería de software:

1. **Vía A — Ejecución directa en CPU con capa de compatibilidad HLE nativa:**
   Mapear los segmentos de código y datos del ejecutable original (`eboot.bin`) directamente en el espacio de direcciones virtual de Windows x64, aplicar las tablas de relocalización, configurar la memoria (RX para código, RW para datos/BSS) y ejecutar las instrucciones de máquina x86-64 de forma nativa sobre la CPU del host, interceptando únicamente las llamadas a servicios del sistema y bibliotecas dinámicas mediante adaptadores de convención de llamadas (ABI SysV a Win64) y subsistemas de plataforma nativos de PC (Vulkan, WASAPI, SDL/DirectInput, VFS local).

2. **Vía B — Recompilación estática completa (AOT Static Recompiler):**
   Desensamblar y levantar (*lift*) la totalidad del código binario x86-64 de `eboot.bin` a una representación intermedia (como LLVM IR) o descompilarlo a código fuente C/C++, para luego recompilarlo estáticamente como un ejecutable binario PE/COFF de Windows totalmente independiente del binario original.

Este documento formaliza la auditoría técnica, los límites observados en el binario y la decisión definitiva para el proyecto.

---

## 2. Auditoría técnica comparativa

| Dimensión técnica | Vía A: Ejecución directa en CPU | Vía B: Recompilación estática AOT |
| :--- | :--- | :--- |
| **Conjunto de instrucciones** | **Idéntico (x86-64):** La CPU Jaguar de PS4 y la CPU del PC (Intel Core i7-10700F) comparten el mismo ISA base AMD64, extensiones SSE4.2, AVX, AES, CLMUL, MOVBE y POPCNT. **0% sobrecoste de emulación.** | Recompilar x86-64 a x86-64 introduce complejidad masiva sin ganancia de rendimiento, ya que el hardware destino ejecuta las instrucciones de origen de forma nativa. |
| **Saltos y llamadas indirectas** | **Soportados nativamente:** `call rax`, `jmp [rdi+rcx*8]`, tablas de vtables C++ y switch jump-tables se ejecutan directamente sobre la silicona a velocidad de reloj sin intermediarios. | **Punto crítico de fallo:** La reconstrucción estática del grafo de flujo de control (CFG) es matemáticamente indecidible ante llamadas indirectas opacas, requiriendo un dispatcher dinámico en tiempo de ejecución o tablas de fallback. |
| **Relocalizaciones y datos** | **Verificado (303.268 entradas):** El 99.8% son relocalizaciones relativas (`R_X86_64_RELATIVE`) en el segmento RW. El código está libre de código automodificable. | Separar datos globales embebidos, punteros estáticos de vtables y constantes flotantes del flujo de código exige un oráculo de desensamblado perfecto. |
| **Manejo de ABI (SysV vs Win64)** | **Resuelto nativamente por Clang:** Clang soporta `__attribute__((sysv_abi))` en Windows x64. El compilador genera automáticamente el puente entre SysV y Win64 sin necesidad de escribir ensamblador manual para cada función. | Requiere reescribir o transformar cada firma de llamada a nivel de LLVM IR o de código fuente. |
| **Memoria y direccionamiento** | **Reserva granular en Windows:** `VirtualAlloc2` reserva el mapa virtual (`0x1000000000`) respetando direccionamiento RIP-relativo de 32 bits con signo ($\pm 2 \text{ GiB}$) dentro del proceso. | Debe duplicar el mapa de memoria o reestructurar todas las referencias globales. |
| **Viabilidad y plazo** | **Viable de inmediato:** Base implementada y validada en el cargador ELF/SELF con 14/14 suites CTest pasando. | **Riesgo crítico de bloqueo:** Recompilar un binario comercial de 38.3 MB sin código fuente tomaría años de ingeniería inversa sin garantía de completar la campaña. |

---

## 3. Demostración práctica y evidencia en el proyecto

1. **Evidencia de ejecución directa probada:**
   * La suite `runtime_loader` demostró que `eboot.bin` se mapea en memoria virtual nativa en ~60 ms.
   * El código de máquina del punto de entrada (`0x2F0`):
     ```assembly
     55                      push   rbp
     48 89 e5                mov    rbp, rsp
     41 57                   push   r15
     41 56                   push   r14
     53                      push   rbx
     50                      push   rax
     48 89 f3                mov    rbx, rsi
     44 8b 37                mov    r14d, DWORD PTR [rdi]
     ```
     Corresponde a instrucciones nativas AMD64 ejecutables de inmediato por cualquier procesador x86-64 moderno.

2. **Evidencia del catálogo de importaciones (Tarea N02):**
   * El 94,13% de las dependencias dinámicas del juego (641 de 681 imports) ya han sido clasificadas y verificadas.
   * `libc.prx` (165 imports) y `libSceFios2.prx` (41 imports) ya están compilados en código binario x86-64 dentro de `CUSA01130/sce_module/`. Forzar una recompilación estática ignoraría la ventaja de que estos módulos ya están disponibles para su enlace dinámico.

3. **Límite real donde sí es necesaria la traducción/recompilación estática:**
   * La GPU de PS4 ejecuta shaders compilados para GNM / AMD GCN. La GPU del PC (NVIDIA GeForce RTX 3050) no ejecuta código binario GCN directamente; requiere SPIR-V para Vulkan.
   * **Conclusión de rendimiento:** El esfuerzo de conversión estática y precompilación *Ahead-Of-Time* (AOT) debe concentrarse donde existe una incompatibilidad arquitectónica real: **los shaders gráficos y el procesamiento de pipelines Vulkan**, no en la CPU donde el código ya es x86-64 nativo.

---

## 4. Resolución y decisión de arquitectura

1. **Se descarta la recompilación estática completa de la lógica de CPU** como requisito o meta del port. No se invertirá tiempo en un desensamblador/lifter de x86-64 a C++ para la lógica del juego.
2. **Se adopta oficialmente la Vía A: Ejecución Directa en CPU con Adaptadores Host Nativos.**
3. **Pilares de la arquitectura de ejecución:**
   * **Lógica del juego:** Ejecución directa en hardware sobre los núcleos de la CPU del host.
   * **Puente ABI:** Adaptadores tipados System V AMD64 a Windows x64 utilizando `GRAVITY_SYSV_ABI` de Clang.
   * **Aislamiento y diagnóstico:** Un subsistema de ejecución protegida (*Runner*) con Vectored Exception Handling (VEH) para capturar cualquier fallo de página o instrucción no soportada, registrando dirección, registro RIP, hilo y módulo sin colapsar el proceso silenciosamente.
   * **Substitución de PRX:** Carga y reutilización directa de los módulos PRX locales verificados (`libc.prx`, `libSceFios2.prx`) siempre que sus dependencias no colisionen con el runtime host, integrando adaptadores HLE específicos para subsistemas de gráficos, audio, entrada y guardado.
