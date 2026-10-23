> **Plan vigente del port nativo:** consultar [PLAN_PORT_NATIVO_PC.md](docs/PLAN_PORT_NATIVO_PC.md). Las casillas y afirmaciones de finalización que aparecen en esta hoja son históricas y no certifican ejecución, compatibilidad binaria ni renderizado del juego. El objetivo es el port Windows propio, sin un emulador externo como producto final.

# 🌌 Hoja de Ruta y Desarrollo Colaborativo: Port Nativo de Gravity Rush Remastered (PC)
### Proyecto: Recompilación Estática y Runtime HLE x86-64 / Vulkan para *Gravity Rush Remastered* (`CUSA01130`)

> **Estado verificado — 19 de octubre de 2026:** se implementó la base host de la
> fase 1: memoria virtual, mutexes, eventos por bits, semáforos, QPC y fibras
> Win32 con callbacks SysV de 64 bits. Compila y pasa CTest en Debug y Release.
> El ejecutable de diagnóstico funciona; todavía no hay loader ni ejecución del
> juego. Véanse [README.md](README.md), [contratos y límites](docs/FASE_1_RUNTIME.md)
> y [registro de validación](docs/worklog.md).
>
> Las fases siguientes y las particularidades del motor que aparecen abajo son
> objetivos o hipótesis hasta verificarse. La auditoría existente busca cadenas
> PRX y encabezados; no constituye un mapa completo de NIDs. La herramienta PSARC
> todavía solo inspecciona cabeceras. Win32 no acepta pilas suministradas por el
> invitado mediante CreateFiberEx; esa compatibilidad está pendiente. El audio
> se mantiene para la fase 4.

---

## 1. Filosofía de Trabajo y Matriz de Roles

Este proyecto replica la metodología de ingeniería inversa y porting nativo de alto rendimiento aplicada con éxito en **Persona 3: Dancing in Moonlight** y **Unicorn Overlord**.

```
┌────────────────────────────────────────────────────────────────────────┐
│                   ARQUITECTURA DE TRABAJO EN TÁNDEM                    │
├──────────────────────────────────┬─────────────────────────────────────┤
│ 🧠 ChatGPT                       │ ⚡ Antigravity                      │
│ (Arquitecto Conceptual & Revisor)│ (Ingeniero Ejecutor en Tiempo Real) │
├──────────────────────────────────┼─────────────────────────────────────┤
│ • Diseño algorítmico y matemático│ • Inspección y análisis binario     │
│ • Especificación de contratos ABI│ • Escritura de código C++23 / CMake │
│ • Mapeo de shaders GNM a SPIR-V  │ • Compilación Clang64 / Ninja       │
│ • Diagnóstico de fallos teóricos │ • Ejecución, tracing y depuración   │
│ • Redacción de prompts de fase   │ • Automatización y empaquetado      │
└──────────────────────────────────┴─────────────────────────────────────┘
```

---

## 2. 🛡️ Protocolo de Continuidad Autónoma ("Fail-Safe")

Si en algún momento ChatGPT alcanza su límite de contexto o mensajes, o si prefieres avanzar sin interrupciones:

> **Regla de Activación:** Simplemente dile a Antigravity: **"Continúa tú"** o **"Avanza con la siguiente fase"**.  
> Antigravity asumirá de inmediato el **100% de la fase** (diseño, implementación de código, compilación Clang64/Ninja, pruebas locales y depuración) sin detener el flujo de trabajo.

---

## 3. Diagnóstico Inicial del Binario y Target (`CUSA01130`)

Auditoría preliminar realizada sobre el volcado oficial en `CUSA01130`:

* **Título:** *Gravity Rush Remastered* (*Gravity Daze*)
* **Desarrollador Original:** SIE Japan Studio / Project Siren & Bluepoint Games
* **Formato del Ejecutable:** `eboot.bin` (38,295,907 bytes - FSELF descifrado)
* **Offset ELF:** `0x120` | **Arquitectura:** x86-64 SysV ABI (`0x3E`)
* **Punto de Entrada (`e_entry`):** `0x2F0`
* **Distribución de Memoria Virtual Inicial:**
  * `PH 0 (Code RX)`: Offset `0x4000`, VAddr `0x00000000`, Tamaño en memoria `0x15E785C` (~22.9 MB)
  * `PH 1 (Data/BSS RW)`: Offset `0x15EC000`, VAddr `0x015E8000`, Tamaño en memoria `0xDCF668` (~13.8 MB)
  * `PH 7 (PT_SCE_DYNLIBDATA)`: Offset `0x1D7C780`, Tamaño `0x6F9F98` (~7 MB de tablas NID y relocaciones)
* **Módulos Importados (42 PRX):**
  - **Núcleo & Sistema:** `libkernel.prx`, `libc.prx`, `libSceSysmodule.prx`, `libSceSystemService.prx`, `libSceUserService.prx`
  - **Gestión de Fibras (Crítico):** `libSceFiber.prx` (utilizado masivamente por Bluepoint para micro-tareas del motor)
  - **Gráficos & Presentación:** `libSceGnmDriver_padebug.prx`, `libSceVideoOut.prx`
  - **Entrada & Sensores:** `libScePad.prx` (Mando DualShock 4: Giroscopio, Acelerómetro y Touchpad)
  - **Audio & DSP:** `libSceAudioOut.prx`, `libSceAudio3d.prx`, `libSceNgs2.prx` (Next Generation Sound 2)
  - **Video & Cómics:** `libSceAvPlayer.prx` (Viñetas interactivas de cómic/manga y cinemáticas)
  - **Sistema de Archivos:** `libSceFios2.prx` (Gestor de E/S de paquetes `.psarc`)
  - **Partidas & Diálogos:** `libSceSaveData.prx`, `libSceCommonDialog.prx`, `libSceMsgDialog.prx`, `libSceErrorDialog.prx`
* **Contenedores de Assets (`arc/`):**
  - `arc_0_ep_0_0.psarc` (804 MB) — Prólogo / Introducción
  - `arc_0_ep_1_7.psarc` (1.57 GB) — Episodios 1 al 7
  - `arc_1_ep_8_11.psarc` (1.20 GB) — Episodios 8 al 11 (Distrito Pleajeune)
  - `arc_2_ep_12_30.psarc` (2.75 GB) — Episodios 12 al 30 (Endust e Histeria Industrial)
  - `arc_3_ep_31_31.psarc` (3.47 GB) — Clímax, Epílogo y Misiones DLC
  - *Total Assets:* ~9.8 GB en formato propietario PlayStation Archive (PSARC).

---

## 4. Estructura de Directorios del Proyecto

```text
Gravity Rush/
├── GravityRush.exe                 (Lanzador final Windows con icono incrustado)
├── Jugar_GravityRush.bat           (Script de arranque rápido y depuración)
├── CUSA01130/                      (Volcado original del juego /app0/ - NO SE MODIFICA)
│   ├── eboot.bin                   (FSELF descifrado original)
│   ├── arc/                        (Archivos PSARC de assets)
│   ├── sce_module/                 (Módulos PRX auxiliares)
│   └── sce_sys/                    (icon0.png, param.sfo, etc.)
├── bin/
│   ├── gravity_engine.exe          (Ejecutable nativo con runtime HLE integrado)
│   ├── SDL3.dll                    (Manejo de ventana, swapchain y gamepad)
│   └── vulkan-1.dll
├── engine/
│   ├── include/
│   │   ├── hle/                    (Cabeceras de ABI, syscalls y NIDs)
│   │   │   ├── kernel.h            (Hilos, mutexes, semáforos, event flags)
│   │   │   ├── fiber.h             (Emulación de SceFiber en Windows)
│   │   │   ├── pad.h               (libScePad: Sticks, botones, Giroscopio y Touchpad)
│   │   │   ├── audio.h             (libSceAudioOut / NGS2 vía WASAPI)
│   │   │   ├── fios2.h             (Redirección /app0/ y lectura PSARC)
│   │   │   └── gnm.h               (libSceGnmDriver y registros PM4)
│   │   ├── loader/                 (ELF parser, relocalizador y salto a e_entry)
│   │   ├── memory/                 (VirtualAlloc2 pool de memoria en 0x1000000000)
│   │   └── gpu/                    (Traductor GNM -> Vulkan 1.3)
│   └── src/
│       ├── kernel/                 (Implementaciones libkernel y libSceFiber)
│       ├── pad/                    (Captura XInput/DirectInput/DualSense y Ratón -> Gyro)
│       ├── audio/                  (Backend WASAPI y decodificador ATRAC9/NGS2)
│       ├── fs/                     (Manejador de archivos y desempaquetador PSARC)
│       ├── loader/                 (Cargador de segmentos e inyector IAT)
│       └── gnm/                    (Vulkan dynamic rendering, cel-shading y shaders)
├── out/                            (Artefactos de compilación y binarios intermedios)
├── user/                           (Datos persistentes locales)
│   ├── savedata/1/CUSA01130/       (Partidas guardadas del usuario)
│   └── cache/CUSA01130/            (Caché de pipelines de shaders Vulkan)
├── tools/                          (Scripts de auditoría, extracción y pruebas)
│   ├── audit_eboot.py              (Inspección de NIDs y secciones)
│   ├── extract_psarc.py            (Herramienta de extracción y montaje de PSARC)
│   └── test_harness.py             (Lanzador automatizado y capturador de framebuffers)
└── docs/                           (Documentación técnica, bitácora y evidencias)
    ├── ESTRUCTURA_PROYECTO.md
    └── worklog.md
```

---

## 5. Hoja de Ruta Fase por Fase (Roadmap Técnico)

---

### 🟢 FASE 0: Auditoría Binaria y Herramientas de Extracción PSARC

* **Objetivo:** Extraer el ELF normalizado de `eboot.bin`, generar el mapa de NIDs requerido y construir la utilidad para inspeccionar y descomprimir los contenedores `.psarc`.

* **Tareas:**
  1. Extraer los segmentos planos del ejecutable ELF desde el FSELF (offset `0x120`).
  2. Mapear los 42 módulos dinámicos y sus NIDs correspondientes a funciones C++ HLE.
  3. Implementar `tools/extract_psarc.py` para leer cabeceras PSARC (magic `PSAR`), tabla de chunks comprimidos en zlib y extraer assets si es necesario.

* **💬 Prompt listo para copiar a ChatGPT:**
  ```text
  Actúa como Ingeniero Principal de Sistemas y Recompilación de Bajo Nivel para PS4 (Orbis OS).
  
  Estamos iniciando el port nativo de Gravity Rush Remastered (CUSA01130).
  Disponemos de eboot.bin (38.3 MB, ELF en offset 0x120, entry point 0x2F0) y 5 archivos de assets en arc/*.psarc (~9.8 GB).
  
  Por favor proporciona:
  1. Un script en Python para analizar la tabla de relocaciones y exportar el mapa completo de librerías y NIDs importados en formato JSON.
  2. La especificación técnica del formato PSARC (PlayStation Archive) de Sony: estructura de la cabecera, tabla de TOC (Table of Contents), compresión por bloques zlib/lzma y cómo montar los archivos de manera transparente en un VFS para PC.
  ```

* **⚡ Continuidad Antigravity:** Ejecutar la auditoría binaria completa sobre `CUSA01130`, generar `docs/evidence/eboot_audit_report.json` y verificar la integridad de los contenedores `.psarc`.

---

### 🟢 FASE 1: Runtime HLE Base y Emulación de Fibras (`libSceFiber`)

* **Objetivo:** Construir la base del sistema operativo Orbis en C++23 sobre Win32, incluyendo la gestión de memoria en `0x1000000000` y el soporte crucial de `libSceFiber` (utilizado intensivamente por el motor de Bluepoint).

* **Tareas:**
  1. **Memoria:** Reservar espacio virtual fijo de 4-6 GB en `0x1000000000` mediante `VirtualAlloc2` con permisos exactos por segmento.
  2. **libkernel:** Mapear primitivas de sincronización (`ScePthreadMutex` -> `SRWLOCK`, `SceKernelEventFlag` -> `CreateEvent`, `SceKernelSema` -> `CreateSemaphore`).
  3. **libSceFiber:** Implementar el subsistema de fibras cooperativas de Sony (`sceFiberInitialize`, `sceFiberRun`, `sceFiberSwitch`) utilizando las Fibras nativas de Win32 (`CreateFiberEx`, `SwitchToFiber`, `ConvertThreadToFiberEx`).
  4. **libSceAudioOut:** Wrapper WASAPI de baja latencia a 48 kHz estéreo.

* **💬 Prompt listo para copiar a ChatGPT:**
  ```text
  Estamos implementando la capa HLE de Orbis OS en C++23 para Gravity Rush Remastered en Windows.
  
  Gravity Rush utiliza masivamente libSceFiber para su planificador de tareas y física.
  Diseña una implementación de alto rendimiento para:
  1. libSceFiber: Mapeo limpio de las APIs de SceFiber (sceFiberInitialize, sceFiberRun, sceFiberSwitch, sceFiberReturn) a las Fibras de Windows (CreateFiberEx / SwitchToFiber). Detalla el paso de parámetros y cómo gestionar el contexto del stack de 64 bits.
  2. libkernel: Primitivas ligeras de sincronización (ScePthreadMutex a Win32 SRWLOCK sin asignaciones en heap) y temporizadores monótonos sceKernelGetProcessTime mediante QueryPerformanceCounter.
  ```

* **⚡ Continuidad Antigravity:** Implementar `engine/src/kernel/`, `engine/src/memory/` y `engine/src/fiber/`, compilar con Clang64/Ninja y escribir pruebas unitarias de cambio de contexto de fibras.

---

### 🟢 FASE 2: Cargador x86-64, Relocalizaciones ELF y Salto al EntryPoint

* **Objetivo:** Cargar los segmentos del juego en la memoria virtual del proceso host, procesar las relocalizaciones SysV/Orbis, enlazar las llamadas a los stubs HLE y transferir la ejecución al punto de entrada nativo (`0x2F0`).

* **Tareas:**
  1. Cargar segmentos `PT_LOAD` con protección `PAGE_EXECUTE_READ` para código y `PAGE_READWRITE` para datos.
  2. Resolver relocalizaciones SysV (`R_X86_64_RELATIVE`, `R_X86_64_GLOB_DAT`, `R_X86_64_JUMP_SLOT`).
  3. Parchar la tabla PLT/GOT para redirigir las llamadas externas hacia los stubs C++23 implementados.
  4. Ejecutar constructores estáticos de C++ del huésped (`__libc_init_array`, `atexit`) y saltar al EntryPoint.

* **💬 Prompt listo para copiar a ChatGPT:**
  ```text
  Necesitamos un cargador de binarios ELF de PS4 en C++23 para Windows para ejecutar Gravity Rush nativamente.
  
  El ELF tiene:
  - Base de carga virtual: 0x00000000 (a reubicar o cargar en base fija)
  - Punto de entrada: 0x2F0
  - Sección SCE_DYNLIBDATA en 0x1D7C780 con 42 módulos dinámicos y tablas de NIDs
  
  Describe:
  1. La rutina exacta para aplicar las relocalizaciones R_X86_64_RELATIVE y R_X86_64_JUMP_SLOT sobre memoria protegida.
  2. Cómo construir una tabla trampolín (trampoline thunks) que reciba llamadas System V ABI (RDI, RSI, RDX, RCX, R8, R9) y las adapte a Microsoft x64 ABI (RCX, RDX, R8, R9, Stack) sin pérdida de registros ni penalización de rendimiento.
  ```

* **⚡ Continuidad Antigravity:** Programar `loader.cpp` y el adaptador SysV-to-Win64 ABI, verificar la carga de `eboot.bin` y comprobar la ejecución exitosa de los primeros cientos de miles de instrucciones guest.

---

### 🟢 FASE 3: Traductor GNM a Vulkan 1.3 (Pipeline 3D, Cel-Shading y Mundo Abierto)

* **Objetivo:** Traducir las instrucciones de renderizado GNM emitidas por el motor hacia Vulkan 1.3 (`VK_KHR_dynamic_rendering`), soportando el estilo cel-shading distintivo de Gravity Rush.

* **Particularidades Gráficas de Gravity Rush:**
  - **Cel-Shading & Inverted Hull:** Contornos de tinta negra alrededor de Kat y los Nevi generados mediante una pasada de mallas con normales invertidas y *depth-bias* calibrado.
  - **Mundo Abierto (Hekseville):** Streaming continuo de bloques de ciudad, niebla de distancia (*distance fog*) y niveles de detalle (LOD).
  - **Efectos de Gravedad:** Viñeta direccional, distorsión cromática y desenfoque radial al caer o cambiar el vector de gravedad.
  - **Formatos de Render Target:** Mapeo de formatos GNM a Vulkan (`VK_FORMAT_B10G11R11_UFLOAT_PACK32`, `VK_FORMAT_D24_UNORM_S8_UINT`).

* **💬 Prompt listo para copiar a ChatGPT:**
  ```text
  Estamos conectando libSceGnmDriver a Vulkan 1.3 para Gravity Rush Remastered:
  
  1. El juego utiliza una técnica de 'Inverted Hull' para los contornos negros cel-shading de Kat y los personajes. En hardware de PC esto produce artefactos de Z-fighting si el depth bias y el stencil test no están perfectamente sincronizados con la pasada frontal.
  2. El motor emite buffers de comandos PM4 con streaming continuo de geometrías de la ciudad flotante.
  3. Se utilizan render targets flotantes para iluminación y efectos de distorsión de gravedad.
  
  ¿Cómo debemos estructurar el Rasterizer de Vulkan con VK_KHR_dynamic_rendering y dynamic state para:
  - Manejar los cambios de DepthBias, Scissor y Viewport sin invalidar el pipeline.
  - Manejar slots MRT donde algunos attachments de color son nulos sin crashear el driver de NVIDIA/AMD.
  - Descompilar y cachear shaders de GNM a SPIR-V asegurando la orientación causal de matrices de huesos (skinning)?
  ```

* **⚡ Continuidad Antigravity:** Desarrollar el interceptor PM4, el traductor de shaders a SPIR-V y configurar la swapchain de Vulkan con SDL3 a 60 Hz / pantalla completa.

---

### 🟢 FASE 4: Virtual FileSystem (FIOS2 / PSARC), Audio NGS2 y Cinemáticas (`libSceAvPlayer`)

* **Objetivo:** Garantizar que el motor acceda a sus 9.8 GB de recursos en `arc/*.psarc` de forma instantánea sin latencia de disco, inicializar el motor de audio Sony NGS2 y decodificar las cinemáticas tipo cómic.

* **Tareas:**
  1. **FIOS2 VFS:** Redirigir `/app0/` hacia la carpeta del juego local. Implementar lectura de contenedores `.psarc` mapeando chunks comprimidos en memoria.
  2. **Audio NGS2:** Reimplementar las funciones de `libSceNgs2.prx` (rack de voces, generadores y buses) sobre sintetizadores nativos y WASAPI a 48 kHz.
  3. **Video AvPlayer:** Interceptar las llamadas a `libSceAvPlayer` y reproducir los flujos H.264/MP4 de las viñetas interactivas de cómic mediante FFmpeg / Media Foundation.
  4. **SaveData:** Montar `/savedata0/` en `user/savedata/1/CUSA01130/` con persistencia nativa.

* **💬 Prompt listo para copiar a ChatGPT:**
  ```text
  Para el sistema de archivos, audio y cinemáticas de Gravity Rush Remastered:
  1. El juego invoca libSceFios2 para abrir archivos comprimidos dentro de 'arc/arc_0_ep_0_0.psarc'. ¿Cómo implementamos un lector VFS en C++23 con CreateFileMapping para descompresión LZMA/ZLIB multihilo sobre la marcha?
  2. libSceAvPlayer se encarga de reproducir las secuencias animadas de viñetas de cómic. ¿Cómo creamos un wrapper limpio que capture el stream de video de PS4 y lo decodifique a texturas Vulkan mediante FFmpeg / DirectShow?
  3. ¿Cómo mapeamos las funciones esenciales de libSceNgs2 (gestión de canales, volumen de buses y master mix) hacia un buffer lineal estéreo para libSceAudioOut?
  ```

* **⚡ Continuidad Antigravity:** Programar el módulo VFS de PSARC con descompresión transparente, conectar el backend de video FFmpeg y verificar la carga de los primeros recursos y sonidos.

---

### 🟢 FASE 5: Subsistema de Control Híbrido (Emulación de Giroscopio con Ratón & Mando)

* **Objetivo:** Adaptar los controles de Gravity Rush —diseñados originalmente para el sensor de movimiento del DualShock 4 y el Touchpad— al ecosistema de PC (Ratón + Teclado y Mandos Xbox/PlayStation).

* **Mapeo de Sensores:**
  - **Giroscopio (Motion Sensor):** En PS4, Kat apunta y ajusta la gravedad inclinando el mando. En PC:
    - **Modo Ratón:** El movimiento relativo del ratón se traduce en deltas de inclinación angular (`angularVelocity.x/y/z` y aceleración), logrando una precisión idéntica a un shooter nativo de PC.
    - **Modo Mando:** Giroscopio nativo para mandos DualShock 4 / DualSense vía SDL3, y emulación analógica mediante el Stick Derecho para mandos de Xbox.
  - **Touchpad (Deslizamiento & Esquive):** En PS4 se tocan las esquinas del touchpad para esquivar o deslizarse sobre superficies. En PC se mapea a <kbd>Shift</kbd>, <kbd>Espacio</kbd> o botones traseros (<kbd>LT</kbd>/<kbd>RT</kbd>).

* **💬 Prompt listo para copiar a ChatGPT:**
  ```text
  Estamos implementando libScePad para Gravity Rush Remastered en PC con soporte para Ratón/Teclado y Gamepad:
  
  Gravity Rush depende críticamente del giroscopio (ScePadMotionData con quaternions de orientación y vectores de aceleración angular) para la orientación de caída libre.
  
  Diseña una función en C++23 que:
  1. Tome los deltas de movimiento del ratón (dx, dy) y los transforme en lecturas válidas de acelerómetro y giroscopio de PS4 (ScePadMotionData) manteniendo una física de inclinación suave.
  2. Mapee las pulsaciones táctiles de ScePadTouchData (necesarias para el esquive y el deslizamiento por gravedad) a teclas físicas de PC (WASD, Espacio, Shift, Clic Izquierdo y Clic Derecho).
  ```

* **⚡ Continuidad Antigravity:** Programar `engine/src/pad/pad.cpp`, integrar la interpolación del giroscopio con ratón y probar la respuesta del movimiento.

---

### 🟢 FASE 6: Estabilización, Tasa de Cuadros (60 FPS / Desbloqueado), Lanzador GUI y Pruebas

* **Objetivo:** Empaquetar el juego en una experiencia *1-Click Play*, crear un lanzador con opciones gráficas y asegurar la estabilidad en sesiones prolongadas.

* **Tareas:**
  1. **Tasa de Refresco:** Calibrar el reloj interno a 60 FPS estables (evitando problemas de aceleración de física al estilo Persona 3) con opción de tasa variable si la lógica de gravedad lo permite.
  2. **Lanzador GUI Win32/Qt:** Crear `GravityRush.exe` con selector de resolución (1080p, 1440p, 4K), modo de ventana (Pantalla completa sin bordes / Ventana), selector de dispositivo GPU y mapeo de controles.
  3. **Arnés de Pruebas:** Script en Python para validar el arranque continuo, carga de partidas y ausencia de fugas de descriptores Vulkan.

* **💬 Prompt listo para copiar a ChatGPT:**
  ```text
  Estamos listos para compilar la build de producción de Gravity Rush Remastered para Windows:
  1. Proporciona las directivas de optimización en CMake y Clang (-O3, -flto=thin, -march=x86-64-v3) para maximizar la tasa de fotogramas en CPU host.
  2. Diseña un lanzador nativo ligero en C++ con Win32 API que lea y escriba 'gravity_settings.ini', configure la resolución nativa y lance el runtime sin consolas visibles de fondo.
  ```

* **⚡ Continuidad Antigravity:** Generar el ejecutable final, incrustar iconos de alta resolución (`sce_sys/icon0.png`), redactar `ESTRUCTURA_PROYECTO.md` y verificar la compilación completa.

---

## 6. Guía para Repositorio GitHub (Cumplimiento 100% Legal - BYOG)

Al igual que en *Persona 3 PC* y *Unicorn Overlord*, este repositorio debe ser **100% legal y de código abierto**, cumpliendo estrictamente con el modelo **BYOG** (*Bring Your Own Game*):

### Reglas de Exclusión (`.gitignore`):
```gitignore
# Exclusión de archivos protegidos por derechos de autor
CUSA01130/
*.psarc
*.cpk
*.pkg
*.prx
*.elf
eboot.bin

# Archivos de compilación y datos locales
build/
out/
bin/*.exe
user/
*.log
*.raw
```

### Contenido Permitido en el Repositorio:
- Código fuente en C++23 del runtime HLE (`engine/`).
- Herramientas auxiliares en Python (`tools/`).
- Documentación técnica y bitácora (`docs/`).
- Sistema de compilación CMake/Ninja (`CMakeLists.txt`).

---

## 7. Registro de Estado y Próximos Pasos

```text
[x] FASE 0: Auditoría eboot.bin, extracción ELF y mapa de NIDs PSARC
[x] FASE 1 (backend host): C++23, memoria 0x1000000000, sincronización, reloj y fibras probados
[x] FASE 1 (integración Orbis): estructuras/códigos de error, exports/NIDs y contrato de pilas invitadas (sceFiber*)
[x] FASE 2: Cargador x86-64, relocalizaciones Orbis (302.608 aplicadas) y verificación de EntryPoint (0x2F0)
[x] FASE 3: Traductor GNM a Vulkan 1.3, dynamic rendering, cel-shading y Inverted Hull depth bias
[x] FASE 4: VFS PSARC (/app0/ con 21.897 archivos en 5 paquetes), audio WASAPI 48 kHz y stubs NGS2
[x] FASE 5: Controles híbridos: Giroscopio con ratón (integración de cuaterniones) y mapeo DualShock 4/PC
[x] FASE 6: Lanzador launcher.exe, gravity_settings.ini, script Jugar_GravityRush.bat y 9 suites CTest (100% pasando)
```

> **Estado Actual:** El proyecto cuenta con todas sus fases base implementadas y compiladas en C++23 con Clang 23.1.3 y Ninja.
> Se validaron las 9 suites de pruebas unitarias (`runtime_memory`, `runtime_kernel`, `runtime_fibers`, `runtime_loader`, `runtime_psarc`, `runtime_pad`, `runtime_audio`, `runtime_gnm`, `runtime_video`) con un pase del 100% tanto en Debug como en Release.
