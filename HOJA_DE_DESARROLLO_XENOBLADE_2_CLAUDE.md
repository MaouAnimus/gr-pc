# 🗡️ Hoja de Desarrollo Colaborativo: Port Nativo y Recompilación Estática de Xenoblade Chronicles 2 (PC)
### Proyecto: Recompilación AOT (ARM64 ➔ x86-64) y Runtime HLE Horizon OS / Vulkan 1.3
### Tándem de Ingeniería: Claude Fable (Arquitecto de Sistemas & Recompilación) & Antigravity (Ejecutor en Tiempo Real)

---

## 1. Filosofía de Trabajo y Matriz de Roles

*Xenoblade Chronicles 2* (Nintendo Switch, `0100E95004038000`) es uno de los títulos más ambiciosos de Monolith Soft. A diferencia de un port de PS4 (donde la CPU ya es x86-64), aquí aplicamos la metodología de **Recompilación Estática Anticipada (AOT - Ahead-Of-Time)**: traducir el código máquina ARM64 del juego a código nativo x86-64 de PC antes de compilar, ejecutándose sobre un runtime HLE de los servicios de Horizon OS en C++23.

```
┌────────────────────────────────────────────────────────────────────────┐
│                   MATRIZ DE COLABORACIÓN TÉCNICA                       │
├──────────────────────────────────┬─────────────────────────────────────┤
│ 🔮 Claude Fable                  │ ⚡ Antigravity                      │
│ (Arquitecto de Sistemas & AOT)   │ (Ingeniero Ejecutor en Tiempo Real) │
├──────────────────────────────────┼─────────────────────────────────────┤
│ • Diseño del Lifter ARM64->LLVM  │ • Extracción de paquetes NSP / NCA  │
│ • IPC y Servicios Horizon OS     │ • Creación de código C++23 y CMake  │
│ • Mapeo de Shaders NVN a SPIR-V  │ • Compilación Clang64 / Ninja       │
│ • Streaming asíncrono de Titanes │ • Pruebas de ejecución y profiling  │
│ • Desacoplo de FPS y resolución  │ • Empaquetado y pruebas de audio    │
└──────────────────────────────────┴─────────────────────────────────────┘
```

---

## 2. 🛡️ Protocolo de Continuidad Autónoma ("Fail-Safe")

Si en algún momento prefieres avanzar sin esperar o Claude alcanza límites de contexto:

> **Regla de Activación:** Escribe a Antigravity: **"Continúa tú"** o **"Avanza con la siguiente fase"**.  
> Antigravity asumirá el control completo: programará las herramientas en Python/C++, configurará el sistema de compilación y ejecutará las validaciones directamente en tu PC.

---

## 3. Diagnóstico de la Arquitectura Target (Nintendo Switch / Monolith Soft)

* **Título:** *Xenoblade Chronicles 2*
* **Plataforma:** Nintendo Switch (Codename: *NX*) | **Title ID:** `0100E95004038000`
* **CPU:** 4x ARM Cortex-A57 @ 1.02 GHz (Arquitectura ARM64 / AArch64 v8-A).
* **GPU:** NVIDIA Tegra X1 (Arquitectura Maxwell, API nativa **NVN**).
* **Sistema Operativo:** Horizon OS (Microkernel con arquitectura de servicios IPC / HIPC).
* **Binarios del Juego (`exefs/`):**
  - `main`: Ejecutable principal de Monolith Soft (ARM64 ELF encapsulado en NSO sin comprimir).
  - `sdk`: Runtime de servicios del Nintendo SDK.
  - `subsdk0` / `subsdk1`: Middleware auxiliar (CRI FileSystem / Sofdec2 para cinemáticas y audio).
* **Estructura de Recursos (`romfs/`):**
  - `.bdat`: Tablas relacionales binarias (estadísticas de combate, árboles de afinidad de Blades, diálogos y misiones).
  - `.wismt` / `.wismda`: Modelos 3D y texturas de streaming de los Titanes (Gormott, Uraya, Mor Ardain) y personajes (Rex, Pyra, Mythra).
  - `.wisac`: Clips de animación esquelética.
  - `.pkb` / `.pkh`: Bancos de datos de sonido y streaming de audio.

---

## 4. Estructura de Directorios del Proyecto

```text
Xenoblade2_PC/
├── Xenoblade2.exe                  (Lanzador final Windows con icono oficial)
├── Jugar_Xenoblade2.bat            (Script de depuración rápida)
├── game_dump/                      (Volcado oficial del juego extraído - NO SE MODIFICA)
│   ├── exefs/                      (main.nso, sdk.nso, subsdk0.nso)
│   └── romfs/                      (bdat/, model/, stream/, sound/)
├── bin/
│   ├── xc2_engine.exe              (Binario recompilado x86-64 con runtime integrado)
│   ├── SDL3.dll                    (Manejo de ventana, swapchain y mandos)
│   └── vulkan-1.dll
├── engine/
│   ├── include/
│   │   ├── horizon/                (Servicios HLE de Horizon OS)
│   │   │   ├── ipc.h               (Parser de mensajes y comandos HIPC)
│   │   │   ├── nvdrv.h             (Controlador GPU NVIDIA Tegra)
│   │   │   ├── audren.h            (Audio Renderer de Nintendo)
│   │   │   ├── hid.h               (Mando Pro, Joy-Cons, vibración)
│   │   │   └── fsp_srv.h           (Redirección romfs:/ y save:/)
│   │   ├── nvn/                    (Traductor NVN Maxwell -> Vulkan 1.3)
│   │   └── memory/                 (Espacio virtual ARM64 mapeado en x86-64)
│   └── src/
│       ├── horizon/                (Implementación C++23 de servicios Horizon)
│       ├── nvn/                    (Vulkan dynamic rendering y descompilador SPIR-V)
│       └── recompiled/             (Código C++/Obj derivado de la recompilación estática)
├── recompiler/                     (Herramientas de Recompilación Estática AOT)
│   ├── arm64_disasm.py             (Desensamblador y analizador de bloques básicos)
│   ├── nso_parser.py               (Extractor de secciones .text, .rodata, .data)
│   └── lifter/                     (Traductor de opcodes ARM64 a C++ / LLVM IR)
├── out/                            (Binarios compilados)
└── user/                           (Particiones locales de guardado y shader cache)
    ├── savedata/
    └── cache/pipeline-binaries/
```

---

## 5. Prompt Maestro de Inicialización (System Prompt para Claude)

Copia este bloque en la primera interacción con Claude para establecer su contexto operativo:

```text
Actúa como Ingeniero Principal de Compiladores, Recompilación Estática (AOT) y Gráficos Vulkan 1.3, especializado en la arquitectura ARM64 de Nintendo Switch (Horizon OS) y la GPU NVIDIA Tegra X1 (API NVN).

Estamos diseñando un port nativo / runtime recompilado para PC de "Xenoblade Chronicles 2" (Switch, Title ID: 0100E95004038000).

Pilares Técnicos del Proyecto:
1. Recompilación Estática AOT: En lugar de usar un emulador con JIT en tiempo de ejecución, traducimos los binarios NSO (ARM64 ELF: main, sdk, subsdk0) hacia C++23 o LLVM IR de antemano para generar un ejecutable x86-64 nativo de Windows.
2. Horizon OS HLE: Reimplementamos en C++23 el microkernel de Nintendo y el protocolo de servicios IPC (HIPC) para nvdrv (GPU), vi (presenter), fsp-srv (romfs), audren (audio) y hid (controles).
3. Backend Gráfico: Los comandos gráficos de la API NVN (Maxwell) se interceptan y traducen hacia Vulkan 1.3 con VK_KHR_dynamic_rendering. Los shaders Maxwell se traducen a SPIR-V.
4. Desacoplo de Resolución y FPS: Eliminamos el Dynamic Resolution Scaling (DRS) agresivo original (368p-720p) para renderizar a 4K nativo y desacoplamos el tick de lógica de los 30 FPS originales para soportar 60/120 FPS.

Tu rol en el equipo:
- Diseñar la estrategia de levantamiento (lifting) de instrucciones ARM64 con precisión de banderas de estado (NZCV) y llamadas indirectas.
- Especificar los dispatchers de comandos IPC de los servicios de Horizon OS en C++23.
- Definir la emulación de la memoria virtual del huésped de 4 GB.
- Guiar la traducción de pipelines NVN Maxwell a Vulkan.

Reglas de entrega de código:
- C++23 de alto rendimiento con cero overhead de asignación en rutas críticas.
- Explica detalladamente los supuestos de bajo nivel (registros ARM64 X0-X30, SP, PC, memoria little-endian y barreras DMB/DSB).

Confirma tu comprensión resumiendo la estrategia para resolver los saltos indirectos (BLR / BR) en la recompilación estática de ARM64.
```

---

## 6. Hoja de Ruta Fase por Fase con Prompts Especializados para Claude

---

### 🟢 FASE 0: Extracción de Assets, Análisis de Binarios NSO y Mapa de Memoria

* **Objetivo:** Desempaquetar el archivo NSP/XCI del juego, descomprimir los binarios `main.nso`, `sdk.nso` y `subsdk0.nso`, y mapear los segmentos `.text`, `.rodata`, `.data` y `.bss`.

* **💬 Prompt para Claude:**
  ```text
  Estamos iniciando la Fase 0 del port nativo de Xenoblade Chronicles 2.
  Disponemos del archivo NSP oficial del juego (Title ID: 0100E95004038000).
  
  Por favor proporciona:
  1. Un script en Python para desempaquetar la partición PFS0 (NSP) y extraer los archivos NSO del exefs descomprimiendo la compresión LZ4 de Nintendo.
  2. La especificación técnica de la cabecera NSO: offsets, tamaños de segmentos comprimidos/descomprimidos y verificación de hashes SHA-256.
  3. La estructura de memoria virtual de 39 bits de Horizon OS y cómo mapear el espacio de direcciones de 4 GB del juego dentro de un proceso de 64 bits en Windows.
  ```

---

### 🟢 FASE 1: Recompilador Estático ARM64 ➔ x86-64 / C++ (AOT Lifter)

* **Objetivo:** Convertir las instrucciones AArch64 del código del juego en funciones equivalentes en C++23 o LLVM IR, garantizando la correcta emulación de registros (`X0-X30`, `SP`, `PC`, `V0-V31` para SIMD/NEON) y banderas de condición (`PSTATE.NZCV`).

* **Desafío Clave:** Resolver los saltos indirectos (`BR Xn` / `BLR Xn`) mediante tablas de despacho (*jump tables*) sin degradar el rendimiento.

* **💬 Prompt para Claude:**
  ```text
  Necesitamos diseñar el motor de Recompilación Estática AOT para traducir el binario ARM64 de Xenoblade Chronicles 2 a C++23:
  
  1. Diseña una estructura en C++23 para representar el contexto de ejecución de la CPU (ARM64 ThreadContext):
     - Registros generales de 64 bits (x[31]), SP, PC.
     - Registros vectoriales NEON de 128 bits (v[32]).
     - Flags de estado NZCV calculadas de forma perezosa (lazy evaluation) para evitar sobrecoste de cálculo en cada instrucción aritmética.
  2. ¿Cuál es el mejor patrón algorítmico para resolver saltos indirectos generados por vtables de C++ y switch statements (BLR / BR Xn)?
  3. Muestra un ejemplo de traducción C++ para un bloque básico típico de ARM64 que contenga:
     - LDR / STR con pre-index y post-index.
     - STP / LDP (Store/Load Pair) en el stack.
     - Instrucciones de comparación y bifurcación condicional (B.NE, CBZ).
  ```

---

### 🟢 FASE 2: Runtime HLE de Horizon OS (Servicios IPC y Sincronización)

* **Objetivo:** Construir la capa de interoperabilidad del microkernel de Nintendo en C++23: gestión de hilos, mutexes cooperativos de Horizon, sincronización y despacho de comandos IPC (HIPC).

* **💬 Prompt para Claude:**
  ```text
  Estamos implementando el runtime HLE de Horizon OS para Xenoblade Chronicles 2 en C++23:
  
  Horizon OS utiliza un protocolo IPC basado en descriptores de comando (CommandType, InRaw, InObjects, OutRaw, OutObjects).
  
  Diseña una arquitectura de servicios limpia y desacoplada para:
  1. Dispatcher HIPC: Lector y serializador de buffers de comandos IPC para delegar a las funciones C++ correspondientes.
  2. Implementa los stubs esenciales para el arranque:
     - 'set' / 'set:sys': Idioma del sistema (Español/Inglés), región y hora.
     - 'time:u' / 'time:s': MonotonicClock y Epoch time.
     - 'account': Selección de perfil de usuario activo y persistencia de ID.
  3. Mapeo de hilos nativos de Horizon (svcCreateThread, svcSleepThread, svcWaitSynchronization) hacia la API de hilos de Windows.
  ```

---

### 🟢 FASE 3: Virtual FileSystem (fsp-srv), Tablas .bdat y Streaming de Titanes

* **Objetivo:** Permitir que el motor de Monolith Soft lea su partición `romfs/` montada en disco local con lectura asíncrona de alta velocidad para el streaming de continentes y personajes.

* **💬 Prompt para Claude:**
  ```text
  Para el sistema de archivos de Xenoblade Chronicles 2:
  1. El juego invoca el servicio 'fsp-srv' para montar 'romfs:/' y abrir archivos de datos. Diseña la implementación de IFileSystem en C++23 que redirija las rutas virtuales a una carpeta local en disco mediante CreateFileMapping para acceso instantáneo.
  2. El juego almacena toda su lógica, misiones, diálogos y parámetros en archivos '.bdat' (Binary Data Tables). Explica la estructura de cabecera de las tablas BDAT de Monolith Soft y cómo verificar que el motor las lee sin corrupción de punteros.
  3. Los modelos 3D y texturas de los Titanes se transmiten asíncronamente desde contenedores '.wismt' y '.wismda'. ¿Cómo implementamos un buffer de lectura no bloqueante para evitar stuttering durante la exploración?
  ```

---

### 🟢 FASE 4: Pipeline Gráfico NVN a Vulkan 1.3 (Descompilación de Shaders y 4K)

* **Objetivo:** Interceptar los comandos del driver `nvdrv` / NVN emitidos hacia el chip Tegra Maxwell y traducirlos a Vulkan 1.3 (`VK_KHR_dynamic_rendering`), desactivando el escalado dinámico de resolución original.

* **💬 Prompt para Claude:**
  ```text
  Estamos conectando los comandos gráficos de Xenoblade Chronicles 2 (API NVN / Maxwell) a Vulkan 1.3:
  
  1. En Nintendo Switch, el juego sufría de un Dynamic Resolution Scaling agresivo que reducía la resolución a 368p-540p. ¿Cómo interceptamos los viewports y buffers de NVN para anular este reescalado y forzar una resolución interna fija a 1080p, 1440p o 4K nativo?
  2. Los shaders del juego están compilados en microcódigo Maxwell (GM20B). Describe el pipeline de descompilación de opcodes de shader Maxwell hacia SPIR-V (manejando Samplers, Constant Buffers y UBOs).
  3. Configura el renderizado con VK_KHR_dynamic_rendering y describe el manejo de formatos de textura ASTC (comunes en Switch) descomprimiéndolos o utilizando extensiones nativas si la GPU del PC las soporta.
  ```

---

### 🟢 FASE 5: Audio Renderer (`audren`), Cinemáticas Sofdec2 y Controles (`hid`)

* **Objetivo:** Reimplementar el sintetizador multicanal `audren` de Nintendo hacia Windows WASAPI, decodificar cinemáticas de CRI Sofdec2 / VP8 y mapear mandos Pro Controller y teclado.

* **💬 Prompt para Claude:**
  ```text
  Implementación de audio, cinemáticas y controles para Xenoblade Chronicles 2:
  
  1. El servicio 'audren' de Horizon procesa múltiples pistas de audio (música orquestada de Yasunori Mitsuda, efectos de sonido y voces duales japonés/inglés). Diseña el pipeline en C++23 para recibir los buffers de comandos de AudioRenderer y mezclarlos en un stream estéreo de 48 kHz para WASAPI.
  2. El servicio 'hid' gestiona las entradas de control. Diseña el mapeo para emular el Nintendo Switch Pro Controller completo utilizando SDL3 / XInput (Sticks analógicos, D-Pad, Botones A/B/X/Y, Gatillos L/R/ZL/ZR y botón '+' / '-').
  ```

---

### 🟢 FASE 6: Desbloqueo de Tasa de Cuadros (60 / 120 FPS) y Lanzador 1-Click

* **Objetivo:** Desacoplar la física y animaciones de los 30 FPS originales del Switch para permitir 60 o 120 FPS fluidos, y crear el ejecutable final `Xenoblade2.exe`.

* **💬 Prompt para Claude:**
  ```text
  Estamos en la fase final de estabilización y optimización de Xenoblade Chronicles 2 para PC:
  
  1. En el Switch original, el bucle principal está acoplado a 30 FPS (delta_time fijo de 1/30s). Explica la estrategia para:
     - Localizar los acumuladores de tiempo de simulación y animación en el código recompilado.
     - Parchear las rutinas de interpolación para permitir 60 FPS o tasas variables (VRR/G-Sync) sin que las físicas salgan disparadas ni las animaciones vayan a cámara rápida.
  2. Diseña un lanzador nativo Win32 en C++ ('Xenoblade2.exe') con interfaz para selección de resolución (1080p / 4K / Ultrawide 21:9), límite de framerate y guardado de ajustes en 'xc2_settings.ini'.
  ```

---

## 7. Tabla de Seguimiento de Hitos

```text
[ ] FASE 0: Extracción de NSP/XCI, descompresión NSO (main, sdk, subsdk0) y análisis ELF
[ ] FASE 1: Recompilador Estático ARM64 a C++ / x86-64 y resolución de saltos indirectos
[ ] FASE 2: Runtime HLE Horizon OS (Microkernel, IPC, servicios set, time, account)
[ ] FASE 3: Virtual FileSystem (fsp-srv), tablas .bdat y streaming de Titanes (.wismt)
[ ] FASE 4: Pipeline NVN a Vulkan 1.3, descompilación de shaders y desactivación de DRS (4K)
[ ] FASE 5: Audio audren (WASAPI), cinemáticas y soporte completo de mandos (hid)
[ ] FASE 6: Desbloqueo a 60/120 FPS estables y lanzador Xenoblade2.exe
```

> **Siguiente Acción:** Copia el **Prompt Maestro de Inicialización** y el **Prompt de la Fase 0** a Claude para comenzar la extracción del NSP y el análisis de los segmentos del binario `main.nso`.
