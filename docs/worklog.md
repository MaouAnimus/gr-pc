## 2026-10-23 — Tareas N01 y N02 (Baseline, Manifiesto e Inventario Dinámico Completo)

- **N01 — Baseline y Manifiesto CUSA01130:**
  - Inicializado repositorio local Git (`78d277b`).
  - Generado manifiesto criptográfico de integridad de la copia del juego en `docs/evidence/manifest_cusa01130.json` (45 archivos, 9.874.426.520 bytes, hashes SHA-256 calculados sin modificar archivos originales).
  - Extraídos metadatos oficiales de `param.sfo`: Title ID `CUSA01130`, Versión `01.00`, Content ID `UP9000-CUSA01130_00-GRAVITYRUSHHD000`.

- **N02 — Exportar Imports, Dependencias e Incertidumbres:**
  - Descubierto y verificado el algoritmo matemático exacto de resolución de NIDs de Sony Orbis:
    `NID = Base64(SHA1(nombre + salt_sony)[0:8][::-1])` con `salt = 518D64A635DED8C1E6B039B1C3E55230`.
  - Corregidas atribuciones históricas erróneas (ej. `uMei1W9uyNo` es `exit`, no `calloc`; `tIhsqj0qsFE` es `free`; `2X5agFjKxMc` es `calloc`).
  - Generado `docs/evidence/imports.json` catalogando los 681 imports de `eboot.bin` con su NID, símbolo sin procesar, biblioteca decodificada (44 librerías), módulo (42 módulos), tipo de símbolo (función/objeto), tipo de relocalización (PLT vs GLOB_DAT/64), prioridad (P0/P1/P2) y clasificación de acción. 641 de 681 (94.13%) verificados con evidencia auténtica.
  - Generado `docs/evidence/dependencias.json` con el grafo completo de 44 librerías importadas, 43 PRX en DT_NEEDED, y la auditoría de los 7 PRX locales en `sce_module/` (demostrando que `libc.prx` provee 165 imports y `libSceFios2.prx` provee 41 imports directamente en el volcado).
  - Generado `docs/evidence/incertidumbres.json` documentando con precisión las 40 incógnitas pendientes (32 correspondientes a diálogos y servicios sociales/PSN opcionales P2, y 8 de infraestructura P0).

## 2026-10-23 — Estabilización

Cargador validado y con reversión, RELA/PLT completas en inspección, importaciones pendientes explícitas, VFS sin bloqueo, ABI corregida, errores de audio/vídeo visibles y cinco suites de regresión. Validación final: 14/14 Debug y Release. El juego todavía no se ejecuta. Ver [informe](ESTABILIZACION.md).

# Registro de implementación

## 23 de octubre de 2026 — Adaptador `sceSystemServiceParamGetInt`

- Promovido `fZo48un7LK4` de P2 opcional a P0 de adaptación tras la evidencia
  adjunta de que el arranque del juego se detiene en esta llamada.
- Contraste del contrato: la firma es `(param_id, int* value)` con ABI System V;
  `Lang` tiene ID 1 y un puntero de salida nulo usa
  `ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER` (`0x80A10003`). Referencias:
  [shadPS4 `systemservice.cpp`](https://github.com/shadps4-emu/shadPS4/blob/91e6afa69b0a64b0afaa0281b80f8fbb0cd88136/src/core/libraries/system/systemservice.cpp)
  y [su cabecera](https://github.com/shadps4-emu/shadPS4/blob/91e6afa69b0a64b0afaa0281b80f8fbb0cd88136/src/core/libraries/system/systemservice.h).
- Registrado el adaptador de `libSceSystemService` por NID. Solo atiende `Lang`
  con el valor 20 indicado por la evidencia adjunta; rechaza IDs no soportados
  sin escribir la salida ni reportar éxito.
- Añadida una suite para valor, puntero nulo, ID no soportado y registro. Debug
  y Release: 37/37 suites cada uno.
- No se pudo confirmar el progreso del juego: falta `CUSA01130/eboot.bin` en
  este checkout. Próximo bloqueo: disponer del ejecutable en la ruta local
  esperada y repetir el arranque instrumentado para comprobar el valor
  solicitado y capturar la siguiente llamada.

## 19 de octubre de 2026 — base de la fase 1

Solicitud: implementar el runtime base y fibras en el proyecto existente.

Se añadieron el sistema de compilación CMake, una biblioteca estática C++23,
el diagnóstico `gravity_runtime_probe`, tres suites CTest y un script PowerShell
reproducible que conserva el PATH original al terminar. El volcado del juego no
se abre ni se modifica durante la compilación o las pruebas.

Implementación:

- `engine/src/memory/address_space.cpp`: reserva fija con VirtualAlloc2, commit,
  permisos, invalidación de caché de instrucciones, decommit y liberación.
- `engine/src/kernel/kernel.cpp`: mutex con propietario/recursión, flags de
  64 bits con condiciones, semáforo acotado y reloj QPC.
- `engine/src/fiber/fiber.cpp`: conversión controlada del hilo, estados de fibra,
  argumentos de 64 bits, retorno a la raíz, captura de excepciones de callback
  y finalización sin abandonar pilas suspendidas.
- Cabeceras de contratos host, errores propios y atributo ABI SysV para Clang.

### Evidencia local

Entorno: Windows x86-64, MSYS2 Clang64. Versiones obtenidas de las herramientas
instaladas: Clang 23.1.3, CMake 4.4.4, Ninja 1.13.2, RTK 0.44.2.

| Configuración | Compilación | CTest | Diagnóstico |
| --- | --- | --- | --- |
| Debug | Correcta; backend y pruebas con advertencias tratadas como errores | 3/3 correctas; 0,13 s | Reserva, commit, fibra, devolución 42, reanudación 99, finalización y liberación correctas |
| Release | Correcta; backend y pruebas con advertencias tratadas como errores | 3/3 correctas; 0,10 s | Mismo recorrido correcto |

Registros detallados de CTest:

- `out/debug/Testing/Temporary/LastTest.log`
- `out/release/Testing/Temporary/LastTest.log`

Las suites comprueban permisos reales con VirtualQuery, reservas de 4 y 6 GiB,
conflictos y límites, 100.000 incrementos con cuatro hilos, flags y semáforos,
tiempo monótono y más de 200.000 transferencias de fibra. Verifican argumentos
de 64 bits, persistencia de variables locales, control de redondeo MXCSR,
retornos de callbacks, excepciones y reutilización de la conversión del hilo.

Se corrigió un fallo del script de arranque causado por la resolución de RTK
de un ejecutable cuyo nombre de ruta incluía espacios. El script entra ahora
temporalmente en la carpeta de salida para lanzarlo por su ruta relativa.

### Límite de lo comprobado

Estos resultados prueban el backend en Windows. No prueban ejecución de
instrucciones del juego ni compatibilidad binaria con libSceFiber. Aún no hay
exports sce*, resolvedor de NIDs, layouts invitados, códigos Orbis, uso de pilas
invitadas, backend de audio ni loader. Las limitaciones y contratos están en
`docs/FASE_1_RUNTIME.md`. Los checklists distinguen backend listo de integración
Orbis pendiente, y no declaran completada la auditoría preliminar.

---

## 23 de octubre de 2026 — Implementación de la Fase 2 (Cargador SELF y Relocalizador Orbis)

Solicitud: continuar el desarrollo del proyecto.

### Logros y Descubrimientos de Arquitectura:

1. **Ingeniería Inversa del Contenedor FSELF (`0x1D3D154F`):**
   - Se analizó la tabla física de segmentos del contenedor FSELF (`eboot.bin` de 38.3 MB) en el offset `0x20`.
   - Se determinaron los offsets físicos exactos del volcado:
     - Segmento 0 (Código RX, `vaddr = 0x0`): offset físico `0xB710`, tamaño `0x15E785C` (~22.9 MB).
     - Segmento 1 (Datos/BSS RW, `vaddr = 0x15E8000`): offset físico `0x15F6C10`, tamaño en archivo `0x790748`, tamaño en memoria `0xDCF668` (~13.8 MB).
     - Segmento 7 (`PT_SCE_DYNLIBDATA`): offset físico `0x1D8AB40`, tamaño `0x6F9F98` (~7.0 MB).
   - Se extrajo el código máquina real del punto de entrada (`0x2F0`):
     `55 48 89 e5 41 57 41 56 53 50 48 89 f3 44 8b 37`
     Correspondiente a la convención System V AMD64 (`push rbp; mov rbp, rsp; push r15; push r14; push rbx; push rax; mov rbx, rsi; mov r14d, [rdi]`).

2. **Motor de Relocalización Orbis ([reloc.cpp](file:///c:/Users/Alvaro/Downloads/Gravity%20Rush/engine/src/loader/reloc.cpp)):**
   - Se analizó la tabla completa de 302.608 relocalizaciones en `0x1D8AB40 + 0xB358`.
   - Se comprobó que el 100% de las relocalizaciones caen dentro del segmento de datos (`0x15E8000` a `0x23B7668`), sin tocar el segmento de código.
   - Distribución de relocalizaciones implementadas y aplicadas:
     - 302.106 relocalizaciones `R_X86_64_RELATIVE` (tipo 8).
     - 486 relocalizaciones `R_X86_64_64` (tipo 1).
     - 16 relocalizaciones `R_X86_64_GLOB_DAT` (tipo 6).
   - Resolvedor de NIDs de Sony Orbis codificados en base64 (`gQX+4GDQjpM` -> malloc, `rcQCUr0EaRU` -> free, `uMei1W9uyNo` -> calloc, y fallback `hle_default_stub`).

3. **Cargador de Segmentos ([elf.cpp](file:///c:/Users/Alvaro/Downloads/Gravity%20Rush/engine/src/loader/elf.cpp)):**
   - Implementación de `load_self` y `load_elf` integrados directamente con `gravity::memory::AddressSpace`.
   - Reserva granular en `0x1000000000`.
   - Mapeo alineado por páginas (4096 bytes) de los segmentos de código y datos.
   - Transición de permisos de memoria (`PAGE_EXECUTE_READ` para código tras relocalizar y vaciado de caché de instrucciones).

4. **Validación Automática CTest y Diagnóstico:**
   - Se agregó la suite `runtime_loader` a `tests/runtime_tests.cpp`.
   - 4/4 pruebas unitarias pasando al 100% en Debug y Release.
   - El lanzador `launcher.exe` carga y relocaliza las 302.608 entradas del juego en ~60 milisegundos.

---

## 23 de octubre de 2026 — Hitos N03 a N08 (Ejecución Aislada, Matriz ABI, Fibras 112B, Registro HLE y Primer Arranque Instrumentado)

### N03 — Decisión de Arquitectura CPU:
- Aprobada la **ejecución directa controlada sobre CPU host Windows x64** mediante adaptación ABI System V y sandboxing de memoria.
- Descartada formalmente la recompilación estática (AOT a C/C++) tras auditar 303.268 relocalizaciones dinámicas, llamadas indirectas computadas por tabla de saltos y generación de código en tiempo de ejecución.

### N04 — Isolated Runner con Vectored Exception Handler (VEH):
- Implementado en `engine/src/runner/runner.cpp` y [runner.h](file:///c:/Users/Alvaro/Downloads/Projects/Gravity%20Rush/engine/include/hle/runner/runner.h).
- Ejecución de punteros System V en hilo de trabajo dedicado con timeout configurable y manejador VEH (`AddVectoredExceptionHandler`).
- Mapeo preciso de violaciones de acceso (lectura/escritura/ejecución), división por cero, instrucciones ilegales y desbordamiento de pila.
- Extracción de vuelco completo de registros (`RIP`, `RSP`, `RBP`, `RAX`, `RBX`, `RCX`, `RDX`, `RSI`, `RDI`, `R8`..`R15`, `EFLAGS`), dirección de fallo y resolución de módulo y offset (`eboot.bin+0x...`).

### N05 — Matriz de Validación ABI System V AMD64:
- Suite de pruebas de regresión en `tests/abi_matrix_tests.cpp` (8 suites CTest independientes):
  1. Paso de 6 enteros en registros (`RDI`, `RSI`, `RDX`, `RCX`, `R8`, `R9`).
  2. Parámetros en pila (argumentos 7 al 12).
  3. Paso de punto flotante en registros XMM (`XMM0` al `XMM7`).
  4. Llamadas mixtas enteros + flotantes + pila.
  5. Paso y retorno de structs por valor (`_Complex`, structs de 16 y 32 bytes).
  6. Callbacks bidireccionales host -> guest -> host.
  7. Funciones varargs System V.
  8. Retorno múltiple de 128 bits (`RAX` + `RDX`).

### N06 — Layout Auténtico SceFiber 112B y Aislamiento TLS:
- Confirmado el layout binario real de `SceFiber` de 112 bytes (`0x70`) con magics `magic_start = 0xdef1649c` y `magic_end = 0xb37592a0` en [orbis_error.h](file:///c:/Users/Alvaro/Downloads/Projects/Gravity%20Rush/engine/include/hle/orbis_error.h).
- Eliminada la copia sombra de pila (`guest_stack_shadow`) que causaba degradación de rendimiento y riesgo de desincronización.
- Implementado `FiberRegistry` desacoplado thread-safe con `std::shared_mutex` para lookup de fibras host.
- Aislamiento completo de Thread Local Storage (TLS) por hilo y ranura de fibra (`ThreadTls`).

### N07 — Registro Tipado de Adaptadores HLE y Trampas Dinámicas:
- Implementado `AdapterRegistry` en `engine/src/registry/adapter_registry.cpp` y [adapter_registry.h](file:///c:/Users/Alvaro/Downloads/Projects/Gravity%20Rush/engine/include/hle/registry/adapter_registry.h).
- Mapeo de más de 120 funciones HLE de `libc`, `libkernel`, `libSceFiber`, `libScePad`, `libSceVideoOut`, `libSceAudioOut`, `libSceSysmodule`, `libSceScreenShot`.
- Generador de trampas de diagnóstico x86-64 de 32 bytes: emite código máquina dinámico (`49 BA <id>`, `48 B8 <dispatcher>`, `FF E0`) que captura el RIP llamador desde `(%rsp)`, loguea biblioteca, NID y offset de módulo, y activa la trampa `ud2` sin retornos silenciosos falsos.
- Heap privado rastreado para libc del guest (`HeapCreate` de 64 MB y registro de punteros) eliminando fallos por `STATUS_HEAP_CORRUPTION`.

### N08 — Primer Arranque Instrumentado de Gravity Rush Remastered:
- Implementado el modo de arranque `--boot` en [launcher.cpp](file:///c:/Users/Alvaro/Downloads/Projects/Gravity%20Rush/launcher.cpp) y soporte en [run_launcher.ps1](file:///c:/Users/Alvaro/Downloads/Projects/Gravity%20Rush/tools/run_launcher.ps1).
- Ejecución real del punto de entrada `eboot.bin` (`0x10000002F0`) bajo `execute_isolated_sysv`.
- **Resultados de Ejecución:**
  - Más de 1.26 segundos y millones de instrucciones x86-64 reales ejecutadas ininterrumpidamente por la CPU.
  - El motor superó con éxito:
    1. Inicialización de argumentos y CRT de Sony Orbis.
    2. Constructores globales y tablas `.ctors` / `.init_array`.
    3. Inicialización de la física Havok (`hkMonitorStream`).
    4. Constructores de C++ estándar y registro de destructores con `__cxa_atexit`.
    5. Configuración de hilos del motor (`scePthreadGetprio`, `scePthreadAttrInit`, `scePthreadAttrSetstacksize`).
    6. Cálculo y almacenamiento de tablas trigonométricas (`__kernel_sincosf`).
    7. Lectura condicional de parámetros de arranque (`/app0/args.txt`).
  - Intercepción limpia mediante trampa de diagnóstico al alcanzar `sceSystemServiceParamGetInt` (`eboot.bin+0x298A`), produciendo vuelco completo de registros y reporte diagnóstico sin colgar el host.
- Total de pruebas automáticas: **36/36 tests pasando al 100%** en Debug y Release.
