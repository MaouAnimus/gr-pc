## 2026-10-23 — Estabilización

Cargador validado y con reversión, RELA/PLT completas en inspección, importaciones pendientes explícitas, VFS sin bloqueo, ABI corregida, errores de audio/vídeo visibles y cinco suites de regresión. Validación final: 14/14 Debug y Release. El juego todavía no se ejecuta. Ver [informe](ESTABILIZACION.md).

# Registro de implementación

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
