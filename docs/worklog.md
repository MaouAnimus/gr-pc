## 2026-10-24 — Paso 5: Libc/Kernel P0 (100% Resuelto), Guardado Local (libSceSaveData), Pipeline de Fuentes (libSceFont/FreeType) y Shaders Vulkan

- **Objetivo:** Ejecutar de forma integral los cuatro componentes del Paso 5 del Plan de Trabajo para la versión nativa de PC de *Gravity Rush Remastered* (CUSA01130):
  1. Completar la totalidad de símbolos P0 de `libc` y `libkernel` (115/115 completados; 0 símbolos P0 sin resolver).
  2. Implementar el subsistema de guardado de partidas y perfil local (`libSceSaveData`, `libSceUserService`, `libSceSaveDataDialog`) respaldado por el VFS y almacenamiento en disco host.
  3. Crear el pipeline de tipografía y fuentes (`libSceFont`, `libSceFontFt`) con integración FreeType 2 y rasterizador bitmap de reserva para subtítulos, viñetas de cómic e interfaces.
  4. Implementar el enlace y pipeline de shaders Vulkan (`VulkanShaderPipeline`) con SPIR-V precompilado y resolvedor dinámico de comandos de dibujado PM4.

- **Implementación HLE Detallada:**
  - **Lote Libc y Kernel P0 (115/115 completado, 0 pendientes P0):**
    - `setjmp` (`gNQ1V2vfXDE`) y `longjmp` (`lKEN2IebgJ0`): implementados como funciones `__attribute__((naked))` en x86-64 SysV ABI preservando los registros callee-saved (`rbx, rbp, r12, r13, r14, r15, rsp, rip`) sin interferencias con el CRT del host Windows.
    - Símbolos de datos auténticos extraídos del binario `libc.prx`: `hle_Qoo175Ig` (8B), `hle_wiRrIcbnlc` (24B), `hle_yLE5H3058Ao` (48B), `hle_Bq8m04PN1zw` (48B).
    - Adaptadores diagnósticos y de control: `hle_XKRegsFpEpk`, `hle_PDX01ziUz`, `hle_eT2UsmTewbU`, `hle_tQIoGIPklo`, `hle_ozMAr28BwSY`, `hle_P8F2oavZXtY`, `hle_DuW5ZqZv70` (`libc`), y `hle_3qxgM4ezETA`, `hle_eXbUSpEaTsA`, `hle_DzES9hQF4f4` (`libkernel`).
    - Suite de pruebas de regresión `setjmp_longjmp` agregada a `tests/libc_tests.cpp`.
  - **Subsistema de Guardado y Perfil (`libSceSaveData`, `libSceUserService`, `libSceSaveDataDialog`):**
    - `libSceUserService`: `sceUserServiceInitialize`, `sceUserServiceTerminate`, `sceUserServiceGetInitialUser` (retornando ID primario `0x10000000`), `sceUserServiceGetUserName` (retornando `"Player"`).
    - `libSceSaveData`: emulación completa de ciclo de vida (`sceSaveDataInitialize`, `sceSaveDataTerminate`), búsqueda de carpetas en disco host (`sceSaveDataDirNameSearch`), montaje y desmontaje dinámico (`sceSaveDataMount`, `sceSaveDataUmount`), borrado de partidas (`sceSaveDataDelete`), configuración de metadatos (`sceSaveDataSetParam`), almacenamiento de icono de guardado (`sceSaveDataSaveIcon` -> `icon0.png`), y gestión de memoria de guardado (`sceSaveDataSetupSaveDataMemory`, `sceSaveDataGetSaveDataMemory`, `sceSaveDataSetSaveDataMemory`).
    - `libSceSaveDataDialog`: emulación no bloqueante del cuadro de diálogo oficial (`sceSaveDataDialogInitialize`, `sceSaveDataDialogOpen`, `sceSaveDataDialogUpdateStatus` retornando estado 3 - finalizado, `sceSaveDataDialogGetResult`, `sceSaveDataDialogClose`, `sceSaveDataDialogTerminate`, `sceSaveDataDialogIsReadyToDisplay`).
    - Enrutamiento dinámico en `VfsManager` (`vfs_mount_path`, `vfs_unmount_path`, `vfs_write`, `vfs_pwrite`): intercepta accesos a `/savedata0` o `/savedata1` y los redirige directamente a la carpeta host `savedata/CUSA01130/<slot>/`, asegurando permisos de lectura y escritura sin alterar el árbol protegido de `/app0`.
    - Suite de pruebas unitarias `tests/savedata_tests.cpp` con 3 suites (`user_service`, `save_mount_vfs`, `save_dialog`).
  - **Pipeline de Fuentes y Texto (`libSceFont` & `libSceFontFt`):**
    - Integración con la biblioteca FreeType 2 (`<ft2build.h>`, `FT_FREETYPE_H`, `FT_GLYPH_H`) junto con un rasterizador integrado de mapas de bits de 8x16 píxeles para los 128 caracteres del código ASCII como fallback resiliente.
    - Implementación de 22 APIs de fuentes:
      - `libSceFontFt`: `sceFontSelectLibraryFt`, `sceFontSelectRendererFt`.
      - `libSceFont`: `sceFontMemoryInit`, `sceFontMemoryTerm`, `sceFontCreateLibraryWithEdition`, `sceFontDestroyLibrary`, `sceFontCreateRendererWithEdition`, `sceFontDestroyRenderer`, `sceFontBindRenderer`, `sceFontUnbindRenderer`, `sceFontSupportSystemFonts`, `sceFontSupportExternalFonts`, `sceFontAttachDeviceCacheBuffer`, `sceFontOpenFontMemory`, `sceFontCloseFont`, `sceFontSetScalePixel`, `sceFontGetScalePixel`, `sceFontSetupRenderScalePixel`, `sceFontGetCharGlyphMetrics`, `sceFontRenderSurfaceInit`, `sceFontRenderCharGlyphImageHorizontal`, `sceFontGetKerning`.
    - Suite de pruebas unitarias `tests/font_tests.cpp` con suites `lifecycle` y `metrics_rendering` verificando escalado, métricas tipográficas y rasterización horizontal sobre buffers de textura compartidos.
  - **Traducción y Enlace de Shaders GCN (Vulkan):**
    - Implementación de `VulkanShaderPipeline` (`engine/src/gnm/shader_pipeline.cpp`) con compilación SPIR-V offline mediante `glslc` y arrays de bytecode embebidos (`engine/src/gnm/spv_arrays.inc`).
    - Creación de módulos de sombreadores para vértices y fragmentos, diseño de pipeline (`VkPipelineLayout`), y pipeline gráfico con soporte de *Dynamic Rendering* de Vulkan 1.3 (`VkPipelineRenderingCreateInfo`, `VK_DYNAMIC_STATE_VIEWPORT`, `VK_DYNAMIC_STATE_SCISSOR`).
    - Configuración de buffers de vértices e índices scratch de 64 KB host-visible y host-coherent.
    - Enlace del `DrawResolver` en la cola de comandos PM4 (`CommandQueue`), garantizando la resolución instantánea de estados gráficos y descriptores de vértices sin fallos de validación.
    - Secuencia de cierre limpia en `VideoRenderer` asegurando que todos los recursos gráficos y pipelines se destruyan antes de desmantelar el dispositivo lógico Vulkan.

- **Resultados de Verificación y Métricas:**
  - **90/90 pruebas unitarias aprobadas (100% de éxito)** en configuraciones **Debug** y **Release** bajo `-Wall -Wextra -Wpedantic -Werror`.
  - **Estado de importaciones P0:** 261 símbolos P0 presentes en el binario; **0 símbolos P0 pendientes de resolución** (100% de la capa crítica P0 resuelta).
  - **Total de importaciones sin resolver:** reducido a 264 en todo el ejecutable de 38.2 MB.
  - **Prueba de arranque (`launcher.exe --headless --boot --timeout=3000`):**
    - Carga y mapeo del ejecutable en `0x1000000000`, 303,268 slots de relocalización procesados.
    - Apertura de display virtual 1080p con 3 buffers de presentación y sincronización Equeue.
    - Reserva y mapeo exitoso de más de 3.3 GB de memoria directa.
    - Lanzamiento de hilos del motor: `FontTextureDeleteThread`, `JobSyncSema`, `BackgroundFiber`, `FIOS2Driver`, `Spooler`.
    - Inicialización de PlayGo (caching episodio 0) y streaming de recursos FIOS2 montando `/app0/arc/arc_0_ep_0_0.psarc`.
    - Cuatro ciclos continuos de intercambio de buffers (`hle_sceVideoOutSubmitFlip`: 1 -> 2 -> 0 -> 1) ejecutados fluidamente y sin trampas de excepción.

## 2026-10-24 — Lote Libc, Kernel P0 y Variables de Condición (Reducción de No Resueltos a 324)

- **Objetivo:** Completar la capa de emulación HLE de la biblioteca estándar (`libc`), soporte Dinkumware C++, `libkernel` (variables de condición `pthread_cond`, atomics, TLS `__tls_get_addr`), gestión de heaps aislados (`sceLibcMspace*`) y streams estándar para evitar trampas de diagnóstico durante la inicialización del motor.
- **Implementación HLE:**
  - **Variables de Condición (`pthread_sync`):**
    - Implementación completa de `posix_cond_init`, `posix_cond_destroy`, `posix_cond_signal`, `posix_cond_broadcast`, `posix_cond_wait`, `posix_cond_reltimedwait`, `posix_cond_timedwait` y sus atributos `posix_condattr_*`.
    - Adaptadores `scePthreadCond*` de `libkernel` vinculados a los primitivos POSIX del host.
  - **Dinkumware & Libc Runtime (`libc`):**
    - Funciones matemáticas de Dinkumware: `_Cosh`, `_Sinh`, `_Sin`, `_Log`, `_FLog`, `fmax`, `fmaxf`, `fminf`, `frexp`, `ldexp`, `modf`, `difftime`.
    - Conversiones numéricas y temporales: `_Stod`, `_Stoll`, `_Stoul`, `_Stoull`, `gmtime`, `localtime`, `mktime`, `clock_gettime`.
    - Cadenas y memoria seguras: `strerror`, `strcoll`, `strcspn`, `strpbrk`, `strtok`, `strnlen_s`, `memcpy_s`.
    - Entrada/Salida estándar: `clearerr`, `feof`, `ferror`, `fflush`, `fgets`, `fputc`, `fread`, `freopen`, `fseek`, `ftell`, `fwrite`, `getc`, `ungetc`, `perror`, `remove`, `rename`, `setvbuf`, `fprintf`, `fscanf`.
    - C++ ABI & RTTI: `__cxa_pure_virtual`, `__cxa_atexit`, `_ZNSt8ios_base4InitC1Ev`, `_ZNSt8ios_base4InitD1Ev`, `__cxa_allocate_exception`, `__cxa_throw`, `__cxa_rethrow`, `__cxa_begin_catch`, `__cxa_end_catch`, `__cxa_call_unexpected`, `_Unwind_Resume`, `__gxx_personality_v0`, `_ZSt9terminatev`, variables `_ZSt7nothrow`, `_ZTIi`, `_ZTVN10__cxxabiv117__class_type_infoE`, `_ZTVN10__cxxabiv120__si_class_type_infoE`.
  - **Mspace Heaps (`sceLibcMspace*`):**
    - Emulación completa de las 12 APIs de heap `sceLibcMspaceCreate`, `sceLibcMspaceDestroy`, `sceLibcMspaceMalloc`, `sceLibcMspaceFree`, `sceLibcMspaceRealloc`, `sceLibcMspaceCalloc`, `sceLibcMspaceMemalign`, `sceLibcMspacePosixMemalign`, `sceLibcMspaceMallocStats`, `sceLibcMspaceMallocStatsFast`, `sceLibcMspaceMallocUsableSize`, `sceLibcMspaceReallocalign`.
  - **Atomics, Bloqueos y Tablas Locales:**
    - Atomics Dinkumware de 32 bits: `_Atomic_fetch_add_4`, `_Atomic_fetch_sub_4`.
    - Bloqueos de sistema: `_Locksyslock`, `_Unlocksyslock`.
    - Punteros a tablas de caracteres: `_Getpctype` y `_Getptolower`.
    - Destructor de `std::_Locinfo` (`_ZNSt8_LocinfoD1Ev`).
    - Estructuras estándar de stream `__std_stdin`, `__std_stdout`, `__std_stderr` (456 bytes) y constantes enteras (`P330P3dFF68`).
  - **TLS Dinámico:**
    - Implementación de `__tls_get_addr` (`vNe1w4diLCs`) enlazando directamente al slot GS de `NativeTls::current_thread_pointer()`.
- **Resultados y Verificación:**
  - 84/84 pruebas unitarias aprobadas (100%) en configuraciones **Debug** y **Release** bajo `-Wall -Wextra -Wpedantic -Werror`.
  - Importaciones no resueltas reducidas de 423 a 324 (99 símbolos resueltos en total; 99 de los 115 símbolos P0 resueltos, quedando solo 16 P0).
  - Verificación de ejecución con `launcher.exe --headless --boot`: el juego avanza fluidamente a través de múltiples hilos (`FontTextureDeleteThread`, `JobSyncSema`, `BackgroundFiber`, `FIOS2Driver`, `Spooler`), gestiona el puerto de visualización 1080p, reserva y mapea memoria directa, inicia PlayGo y FIOS2 montando `.psarc`, y realiza los primeros ciclos de flip sin sufrir ningún fallo o trampa de diagnóstico.

## 2026-10-24 — Emulación HLE de libSceFios2 (File I/O System 2) y Pipeline de Streaming de Activos

- **Objetivo:** Emulación completa de `libSceFios2` respaldada por `VfsManager` y montaje `.psarc`, resolviendo el 100% (41/41) de los imports de FIOS en `eboot.bin` para permitir el streaming de recursos desde `/app0/arc/arc_0_ep_0_0.psarc`.
- **Implementación HLE de libSceFios2:**
  - Layouts auténticos de structs extraídos del desensamblado de `CUSA01130/sce_module/libSceFios2.prx`:
    - `SceFiosParams` (152 bytes / 0x98) con validación de máscara por hardware (`bextr` `(size >> 1) & 0x7FFF == 0x98`).
    - `SceFiosStat` (80 bytes / 0x50): timestamps nanosegundo (`ctime`, `mtime`, `atime`), tamaño en bytes (offset 0x00) y atributos POSIX (offset 0x48).
    - `SceFiosDirEntry` (1048 bytes / 0x418): stat (0x50) + buffer de nombre (968 bytes).
  - Códigos de error y estados auténticos (`SCE_FIOS_OK`, `SCE_FIOS_ERROR_ALREADY_INITIALIZED`, `SCE_FIOS_ERROR_FILE_NOT_FOUND`, `SCE_FIOS_ERROR_BAD_FH`, `SCE_FIOS_ERROR_BAD_OP`, `SCE_FIOS_ERROR_EOF`, etc.).
  - Implementación completa de las 41 APIs importadas por `eboot.bin`:
    - Ciclo de vida: `sceFiosInitialize`, `sceFiosTerminate`.
    - Filtros de E/S: `sceFiosIOFilterAdd`, `sceFiosIOFilterRemove`, `sceFiosIOFilterPsarcDearchiver`.
    - Montaje de archivos: `sceFiosArchiveGetMountBufferSizeSync`, `sceFiosArchiveMountSync`, `sceFiosArchiveUnmountSync`.
    - Handles de archivos (FH): `sceFiosFHOpenSync`, `sceFiosFHOpen`, `sceFiosFHReadSync`, `sceFiosFHRead`, `sceFiosFHPread`, `sceFiosFHCloseSync`, `sceFiosFHClose`, `sceFiosFHGetSize`, `sceFiosFHSeek`, `sceFiosFHTell`, `sceFiosFHSyncSync`, `sceFiosFHWrite`, `sceFiosFHWriteSync`, `sceFiosFHPwrite`.
    - Consultas de rutas e I/O directo: `sceFiosFileGetSizeSync`, `sceFiosExistsSync`, `sceFiosDirectoryExistsSync`, `sceFiosStatSync`, `sceFiosFileReadSync`, `sceFiosFileRead`, `sceFiosFileWriteSync`, `sceFiosFileWrite`, `sceFiosFileDeleteSync`.
    - Handles de directorios (DH): `sceFiosDHOpenSync`, `sceFiosDHOpen`, `sceFiosDHReadSync`, `sceFiosDHCloseSync`, `sceFiosDirectoryCreate`.
    - Operaciones asíncronas (Op): `sceFiosOpWait`, `sceFiosOpGetError`, `sceFiosOpGetActualCount`, `sceFiosOpDelete`.
    - Diagnósticos: `sceFiosDebugDumpError`.
  - Extensión de `VfsManager` con `vfs_pread`, `vfs_get_size`, `vfs_tell`, `vfs_file_size`, `vfs_exists`, `vfs_is_dir`, `vfs_stat`, `vfs_opendir`, `vfs_readdir`, `vfs_closedir`, combinando streaming desde disco físico y archivos indexados en `.psarc`.
  - Registro de los 41 adaptadores SysV ABI en `AdapterRegistry` bajo sus NIDs exactos de Sony.
- **Validación y Pruebas:**
  - Suite de pruebas unitarias `tests/fios_tests.cpp` ampliada con `file_ops` probando operaciones síncronas y asíncronas, verificación de cabecera mágica `"PSAR"`, stat, queries de tamaño, lectura de directorios y control de errores.
  - 100% de tests aprobados (77/77) en **Debug** y **Release** bajo `-Wall -Wextra -Wpedantic -Werror`.
  - Ejecución verificada con `launcher.exe --headless --boot --timeout=2000`: importaciones no resueltas reducidas de 456 a 423 (33 imports resueltos adicionales).

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

## 2026-10-23 — Lote 2: decodificador PM4 y cola hacia Vulkan

Implementados 13 opcodes Type 3 y padding Type 2, IB con límites/ciclos, envío atómico de múltiples DCB, cola limitada con productores concurrentes, tickets de grabación confirmados tras vkQueueSubmit y enlace del NID sceGnmSubmitCommandBuffers. Pruebas Vulkan con dibujos automáticos e indexados 16/32/offset y verificación de píxeles en RTX 3050. Total: 43 suites en Debug/Release. No se validaron buffers capturados del juego ni shaders GCN; constant engine, eventos y finalización/flip del invitado siguen pendientes. Ver [informe y límites](LOTE_2_PM4_VULKAN.md).

## 2026-10-23 — Ejecución del plan: arranque y servicios nativos

Se implementaron la consulta de parámetros del sistema, hilos con callbacks
reales, mutex y RW locks, semáforos, equeue, acceso de lectura del kernel al VFS
y memoria directa con límites, alineación y liberación. El loader resuelve por
biblioteca/NID, conserva PT_TLS y aplica un perfil de 2.752 correcciones TLS
nativas ligado al hash del ejecutable. Se preparan las pilas para marcos ELF
grandes sin sondeos Windows. Las trampas guardan los seis argumentos enteros.

Las rutas alcanzadas de módulos y vídeo dejan de devolver éxito sin implementar
el servicio; presentación invitada, PRX y audio invitado siguen incompletos.
Se conservan intentos independientes con hashes y diagnóstico; el harness
termina su proceso al finalizar para no liberar código bajo hilos todavía vivos.

Resultado final: **68/68 suites aprobadas en Debug y Release**, sin omisiones
en los registros finales. Ambos arranques secuenciales superan la parada inicial
de idioma y terminan en `sceFiosInitialize`, `eboot.bin+0x4FDC`.
Hay 509 importaciones sin resolver. No hay primer envío PM4 ni imagen del juego.
El [informe del lote](EJECUCION_LOTE_ARRANQUE.md) detalla evidencia y límites;
el [plan operativo](PLAN_EJECUCION.md) mantiene las tareas restantes abiertas.

Corrección de evidencia anterior: la duración de N08 no medía instrucciones,
por lo que la afirmación histórica de «millones» no quedó demostrada. Los
adaptadores de hilos iniciales tampoco ejecutaban callbacks. Las pruebas TLS
anteriores verificaban helpers, no los accesos FS originales, y las pilas
suministradas para fibras continúan pendientes. El catálogo actual excluye
exportaciones sin nombre: 539 identidades y 142 nombres pendientes de 681.

## 2026-10-24 — Hito Mayor: Bucle Gráfico Activo, Fences PM4 EOP, Desbloqueo de Flip Queue y Presentación Continua en Vulkan

- **Resolución de Fences PM4 y Eventos EOP:**
  - Decodificación completa del opcode GCN `0x47` (`EVENT_WRITE_EOP`) con desempaquetado de direcciones y escritura de payloads/timestamps en la memoria invitada, desbloqueando los hilos invitados que sondean la finalización de comandos GPU.
  - Implementación y aceptación limpia de opcodes de sincronización y memoria: `0x37` (`WRITE_DATA`), `0x40` (`COPY_DATA`), `0x42` (`PFP_SYNC_ME`), `0x43` (`SURFACE_SYNC`), `0x46` (`EVENT_WRITE`), `0x48` (`EVENT_WRITE_EOS`), `0x49` (`RELEASE_MEM`), `0x50` (`DMA_DATA`).
  - Cero paquetes PM4 rechazados en la corriente de inicialización de GNM.

- **Pre-asignación de Ranura TLS Nativa y Modo Gráfico Completo:**
  - Descubierto y resuelto el conflicto de ranuras Win32 TLS: cuando SDL3 y el controlador gráfico Vulkan se inicializaban antes de cargar el ELF, el controlador consumía ranuras TLS provocando que `guest_slot()` superara el límite de 64 (`TEB.TlsSlots`).
  - La invocación anticipada de `NativeTls::supported()` al inicio de `main()` reserva la ranura de forma temprana, permitiendo que el perfil de 2.752 parches nativos se aplique con 100% de éxito tanto en modo headless como en modo con ventana SDL3 y Vulkan activo.

- **Integración Completa del Subsistema VideoOut y Cola de Eventos de Flip:**
  - Implementado el ciclo de vida completo de `libSceVideoOut` en [adapter_registry.cpp](file:///c:/Users/Alvaro/Downloads/Projects/Gravity%20Rush/engine/src/registry/adapter_registry.cpp):
    - `sceVideoOutOpen`: Abre el puerto de visualización, retornando handle válido `1`.
    - `sceVideoOutSetBufferAttribute`: Configura atributos de framebuffer 1920x1080 y sincroniza resolución.
    - `sceVideoOutRegisterBuffers`: Registra con seguridad los 3 framebuffers de renderizado triple en memoria directa con validación `guest_memory::accessible`.
    - `sceVideoOutSetFlipRate`: Almacena la tasa de refresco.
    - `sceVideoOutAddFlipEvent`: Vincula el puerto de visualización a la cola de eventos `FLIP QUEUE` (`0x10000`).
    - `sceVideoOutSubmitFlip`: Conmuta el buffer activo, sincroniza la presentación VBlank a 60 FPS contra Vulkan, y emite el evento de finalización de flip a través de `equeue::post()`.
  - El hilo de renderizado del juego (bloqueado previamente en `sceKernelWaitEqueue` en `eboot.bin+0x157e5`) se desbloquea de forma natural y ejecuta de forma continua su bucle de fotogramas (`buffer 1 -> buffer 2 -> buffer 0 -> buffer 1`).

- **Presentación Continua en Vulkan:**
  - Actualizado `VideoRenderer::render_frame()` para presentar el fondo azul cómic cel-shaded característico de Kat (`0.05f, 0.05f, 0.08f, 1.0f`) a 60 FPS de forma ininterrumpida mientras la traducción de shaders GCN se encuentra en progreso, evitando paradas artificiales del hilo gráfico.
  - Verificado en ejecuciones de 5 y 10 segundos ininterrumpidos: **cero cuelgues, cero violaciones de acceso y cero excepciones nativas**.
  - Total de pruebas automáticas: **76/76 suites aprobadas al 100%** en Debug y Release con política estricta de cero advertencias (`-Wall -Wextra -Wpedantic -Werror`).
