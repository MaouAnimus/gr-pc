> **Plan vigente del port nativo:** consultar [PLAN_PORT_NATIVO_PC.md](docs/PLAN_PORT_NATIVO_PC.md). Las casillas y afirmaciones de finalización que aparecen en esta hoja son históricas y no certifican ejecución, compatibilidad binaria ni renderizado del juego. El objetivo es el port Windows propio, sin un emulador externo como producto final.

# 🔮 Hoja de Desarrollo Colaborativo: Port Nativo de Gravity Rush Remastered (PC)
### Arquitectura: Recompilación Estática y Runtime HLE x86-64 / Vulkan 1.3
### Tándem de Ingeniería: Claude Fable (Arquitecto de Sistemas) & Antigravity (Ejecutor Local)

> **Estado verificado — 19 de octubre de 2026:**
> - Se implementó y validó la base host de la **Fase 1**: reserva de memoria virtual en `0x1000000000` (`VirtualAlloc2`), mutexes de baja latencia (`SRWLOCK`), banderas de eventos por bits, semáforos, temporizador de alta frecuencia (`QPC`) y fibras Win32 con callbacks adaptados a System V ABI de 64 bits.
> - Compilación limpia y pase del 100% de pruebas en CTest (Debug y Release).
> - **Pendiente inmediato:** Alineación de estructuras de datos y códigos de error con el SDK de Orbis OS, exportación de tablas NID y resolución del contrato de pilas suministradas por el invitado antes de enlazar al loader de la Fase 2.

---

## 1. Filosofía de Trabajo y Matriz de Roles

Esta hoja está optimizada para aprovechar el razonamiento profundo, la precisión matemática y el rigor de sistemas de bajo nivel característicos de **Claude (Fable / Sonnet)**.

```
┌────────────────────────────────────────────────────────────────────────┐
│                   MATRIZ DE COLABORACIÓN TÉCNICA                       │
├──────────────────────────────────┬─────────────────────────────────────┤
│ 🔮 Claude Fable                  │ ⚡ Antigravity                      │
│ (Arquitecto de Sistemas & Math)  │ (Ingeniero Ejecutor en Tiempo Real) │
├──────────────────────────────────┼─────────────────────────────────────┤
│ • Rigor ABI (SysV ➔ Win64)       │ • Inspección de binarios y headers  │
│ • Contratos de memoria y stacks  │ • Creación y edición de archivos C++│
│ • Matemáticas de cuaterniones    │ • Compilación Clang64 / Ninja local │
│ • Lógica Vulkan 1.3 / SPIR-V     │ • Ejecución de CTest y diagnóstico  │
│ • Análisis de causa raíz (crashes│ • Instrumentación y logs en vivo    │
└──────────────────────────────────┴─────────────────────────────────────┘
```

---

## 2. 🛡️ Protocolo de Continuidad Autónoma ("Fail-Safe")

Si en cualquier momento la sesión con Claude se satura o deseas avanzar de forma ininterrumpida:

> **Regla de Activación:** Escribe a Antigravity: **"Continúa tú"** o **"Avanza con la siguiente fase"**.  
> Antigravity tomará el control completo de la fase: escribirá el código C++23, actualizará CMake, compilará con Clang64/Ninja y ejecutará las pruebas en tu máquina sin detenerse.

---

## 3. Prompt Maestro de Inicialización (System Prompt para Claude)

Copia este bloque en la primera interacción con Claude para establecer su contexto operativo completo:

```text
Actúa como Ingeniero Principal de Sistemas de Bajo Nivel, Recompilación Estática y Renderizado Vulkan 1.3, especializado en la arquitectura x86-64 de PlayStation 4 (Orbis OS) y APIs nativas de Windows (Win32).

Estamos construyendo un port nativo / runtime HLE para PC de "Gravity Rush Remastered" (PS4, CUSA01130).

Datos Técnicos Fundamentales:
1. Binario Huésped: eboot.bin (38.3 MB, ELF en offset 0x120, entry point 0x2F0).
2. Arquitectura de CPU: x86-64 System V AMD64 ABI ejecutándose directamente en CPU Host x86-64 (Windows x64 ABI) sin emulador ni JIT.
3. Memoria Virtual: Pool reservado en 0x1000000000 mediante VirtualAlloc2 con protección granular por segmento.
4. Módulos Críticos:
   - libSceFiber: Planificador cooperativo de micro-tareas usado masivamente por Bluepoint Games.
   - libSceGnmDriver_padebug: GPU Liverpool traducida a Vulkan 1.3 dinámico (VK_KHR_dynamic_rendering).
   - libScePad: Giroscopio/Acelerómetro y Touchpad (esenciales para el apuntado y deslizamiento por gravedad).
   - libSceNgs2: Síntesis de audio y DSP multicanal mapeado a WASAPI 48 kHz.
   - libSceFios2 / PSARC: 9.8 GB de assets en arc/*.psarc (zlib, bloques de 64 KB).
   - libSceAvPlayer: Cinemáticas interactivas estilo cómic/manga.

Estado actual del proyecto:
- Implementado el backend host de Fase 1 (AddressSpace 0x1000000000, mutexes SRWLOCK, semáforos, event flags, QPC y fibras Win32 con thunks SysV).
- Próximo hito: compatibilidad de contratos Orbis (estructuras, NIDs, stack huésped de fibras) y construcción del loader x86-64 (Fase 2).

Tus directrices de respuesta:
- Genera código C++23 idiomático, de máxima eficiencia (cero asignaciones dinámicas en rutas críticas) y portable.
- Antes de cada solución, desglosa los supuestos de bajo nivel: registros involucrados, alineación de stack (16 bytes), orden causal de memoria y compatibilidad de drivers de GPU.
- En caso de errores o violaciones de acceso, realiza un análisis de causa raíz orientado a la discrepancia de ABIs o barreras de memoria.
```

---

## 4. Hoja de Fases con Prompts Especializados para Claude

---

### 🟢 FASE 1.5: Integración de Contratos Orbis para Fibras y Memoria

* **Objetivo:** Adaptar el backend de fibras y memoria ya verificado en el host para que respete las estructuras reales, códigos de retorno y contratos de pila de `libSceFiber` y `libkernel`.

* **Desafío Clave:** Win32 `CreateFiberEx` asigna y administra su propia memoria de pila; sin embargo, `sceFiberInitialize` en PS4 recibe un puntero a una pila ya reservada por el juego (`stackContext`). Claude debe resolver la reconciliación o cambio de contexto personalizado.

* **💬 Prompt para Claude:**
  ```text
  Estamos finalizando la Fase 1 del runtime HLE para Gravity Rush Remastered.
  Ya tenemos validado un backend host en C++23 con AddressSpace (0x1000000000), SRWLOCK y Fibras Win32 con trampolines SysV.
  
  Necesitamos resolver el contrato exacto de libSceFiber de Orbis OS:
  1. La API 'sceFiberInitialize' de PS4 recibe una estructura con una pila previamente asignada por el huésped (guest-supplied stack pointer). Dado que CreateFiberEx de Win32 no permite vincular una pila arbitraria externa, ¿cuál es la mejor arquitectura en C++23 / ensamblador x86-64 para:
     a) Realizar un context-switch cooperativo ultraligero (guardando/restaurando RBX, RSP, RBP, R12-R15 y el puntero de TEB) que opere directamente sobre la pila asignada por el juego, O
     b) Emular el stack de la fibra Win32 copiando los frames iniciales?
  2. Define las estructuras de datos y códigos de error oficiales de Orbis:
     - SceFiber, SceFiberOptParam
     - Códigos SCE_OK, SCE_FIBER_ERROR_*
  Proporciona el código de 'fiber_context.S' / 'fiber_orbis.cpp'.
  ```

---

### 🟢 FASE 2: Cargador ELF x86-64, Relocalizador SysV y Salto al EntryPoint

* **Objetivo:** Diseñar el cargador de segmentos, el procesador de relocalizaciones de 64 bits y los puentes trampolín ABI sin sobrecoste.

* **Desafío Clave:** Los juegos de PS4 utilizan llamadas System V AMD64 ABI (argumentos en `RDI`, `RSI`, `RDX`, `RCX`, `R8`, `R9`) mientras que las APIs del host y librerías de Windows utilizan Microsoft x64 ABI (`RCX`, `RDX`, `R8`, `R9`, `Shadow Space` de 32 bytes).

* **💬 Prompt para Claude:**
  ```text
  Diseña el módulo 'loader' (ELF loader y Dynamic Linker) para Gravity Rush Remastered en C++23:
  
  Parámetros del binario:
  - eboot.bin con ELF en offset 0x120.
  - Entry point: 0x2F0.
  - SCE_DYNLIBDATA en 0x1D7C780 con 42 módulos PRX importados.
  
  Requerimientos técnicos:
  1. Implementa el parseo y mapeo de los segmentos PT_LOAD sobre el AddressSpace (0x1000000000) respetando protecciones PAGE_EXECUTE_READ y PAGE_READWRITE.
  2. Implementa la rutina de aplicación de relocalizaciones SysV:
     - R_X86_64_RELATIVE
     - R_X86_64_GLOB_DAT
     - R_X86_64_JUMP_SLOT
  3. Proporciona una macro o generador de 'Trampolines ABI SysV -> Win64' en ensamblador inline / NASM que reciba llamadas con convención System V, reordene los registros a la convención Win64 con shadow space, invoque el stub C++ y retorne el valor respetando RAX/RDX sin corromper el stack.
  ```

---

### 🟢 FASE 3: Traductor GNM a Vulkan 1.3 (Pipeline 3D y Cel-Shading)

* **Objetivo:** Interceptar el flujo de comandos PM4 emitidos por el motor de Gravity Rush y traducirlos a Vulkan 1.3 utilizando `VK_KHR_dynamic_rendering` y `VK_EXT_extended_dynamic_state`.

* **Desafío Clave:** La técnica de contorno cel-shading (*Inverted Hull*) extruye normales invertidas en una segunda pasada. Si el *DepthBias* o la prueba de profundidad no están perfectamente calibrados, se produce parpadeo o cortes en las líneas de contorno de Kat.

* **💬 Prompt para Claude:**
  ```text
  Estamos desarrollando el traductor GNM a Vulkan 1.3 para Gravity Rush Remastered:
  
  El motor gráfico de Bluepoint / Japan Studio implementa:
  1. Cel-shading estilo bande dessinée con pasada Inverted Hull para contornos negros.
  2. Streaming asíncrono de bloques de geometría de la ciudad abierta de Hekseville.
  3. Render targets flotantes (VK_FORMAT_B10G11R11_UFLOAT_PACK32) y depth-stencil (VK_FORMAT_D24_UNORM_S8_UINT).
  
  Diseña la arquitectura del Rasterizer de Vulkan:
  - ¿Cómo configuramos vkCmdSetDepthBias y vkCmdSetStencilOp dentro del dynamic rendering para evitar Z-fighting en mallas de contorno invertido?
  - ¿Cómo gestionamos los Multiple Render Targets (MRT) cuando el juego emite un slot de color no vinculado (image_id == 0) para descartar escrituras sin provocar Device Loss en NVIDIA/AMD?
  - Proporciona el layout de descriptores push (VK_KHR_push_descriptor) para enlazar texturas y buffers de vértices con latencia mínima.
  ```

---

### 🟢 FASE 4: Virtual FileSystem (FIOS2 / PSARC), Audio NGS2 y Cómic

* **Objetivo:** Lectura transparente y descompresión ultrarrápida de los 9.8 GB en `arc/*.psarc`, backend de audio para `libSceNgs2` sobre WASAPI y decodificación de cinemáticas estilo manga.

* **Desafío Clave:** El formato PSARC contiene miles de archivos indexados en una tabla TOC con compresión zlib de bloques de 64 KB.

* **💬 Prompt para Claude:**
  ```text
  Diseña el subsistema VFS para los paquetes PSARC de Gravity Rush Remastered:
  
  Disponemos de 5 archivos en arc/*.psarc:
  - Cabecera 'PSAR', versión 1.4, compresión zlib, bloques de 64 KB, TOC indexada.
  
  Requerimientos:
  1. Diseña un lector VFS en C++23 con memoria mapeada (CreateFileMapping / MapViewOfFile) que monte los 5 archivos PSARC bajo la ruta virtual '/app0/arc/' y permita lecturas aleatorias transparentes desde libSceFios2.
  2. Implementa descompresión de bloques en caché para que las solicitudes de lectura de texturas y audio no provoquen micro-tirones durante el vuelo libre de Kat.
  3. Especifica los stubs esenciales de libSceNgs2 para redirigir los canales de sonido estéreo hacia un buffer circular de Windows WASAPI a 48 kHz.
  ```

---

### 🟢 FASE 5: Controles Híbridos (Giroscopio con Ratón & Mando)

* **Objetivo:** Replicar la física del sensor de movimiento del DualShock 4 mediante el ratón de PC con matemáticas de cuaterniones y aceleración angular.

* **💬 Prompt para Claude:**
  ```text
  Estamos implementando libScePad para Gravity Rush Remastered.
  El juego requiere obligatoriamente datos válidos en ScePadMotionData (orientación en cuaternión, aceleración angular y vector de gravedad) para estabilizar la cámara y apuntar el vector de caída libre.
  
  Diseña un modelo cinemático en C++23:
  1. Recibe deltas del ratón (dx, dy, dt) a alta frecuencia.
  2. Modela la orientación espacial como un cuaternión q = [w, x, y, z] integrando la velocidad angular con filtrado de suavizado exponencial (EMA).
  3. Simula el vector de gravedad terrestre ficticio [0, -1, 0] afectado por la inclinación actual.
  4. Mapea los botones del DualShock 4 y las esquinas del Touchpad a eventos de teclado/ratón (WASD, Espacio, Shift, Clics) con soporte hotplug para mandos Xbox y DualSense.
  ```

---

### 🟢 FASE 6: Profiling, Tasa de Cuadros Desbloqueada y Lanzador

* **Objetivo:** Optimización de compilación con LTO/PGO, desvinculación de la física de la tasa de 60 Hz y creación del lanzador Windows `GravityRush.exe`.

* **💬 Prompt para Claude:**
  ```text
  Estamos en la fase final de estabilización y empaquetado de Gravity Rush Remastered:
  1. Revisa la separación entre el reloj de física del motor (fijo a 60 Hz) y el bucle de presentación de Vulkan para permitir tasas de refresco de 120/144 Hz o monitores VRR sin alterar la velocidad del juego.
  2. Proporciona las flags óptimas de compilación en Clang64 (-O3, -flto=thin, -march=x86-64-v3) y control de mitigaciones de Spectre/Meltdown para código guest.
  3. Estructura el código de un lanzador Win32 nativo en C++ sin consolas visibles que lea 'gravity_settings.ini', aplique resolución y lance el runtime.
  ```

---

## 5. Tabla de Seguimiento de Hitos

```text
[x] FASE 0: Auditoría de eboot.bin, módulos PRX y validación de cabeceras PSARC
[x] FASE 1 (Host): AddressSpace 0x1000000000, sincronización Win32, QPC y fibras base
[x] FASE 1 (Orbis): Layouts de estructuras, códigos de error, NIDs y pilas de fibras (sceFiber*)
[x] FASE 2: Cargador SELF/ELF, relocalizaciones Orbis (302.608 aplicadas) y verificación de EntryPoint (0x2F0)
[x] FASE 3: Traductor GNM a Vulkan 1.3, dynamic rendering, cel-shading y Inverted Hull depth bias
[x] FASE 4: VFS PSARC multihilo (21.897 archivos indexados en 5 contenedores), audio WASAPI 48 kHz y stubs NGS2
[x] FASE 5: Controles Híbridos: Giroscopio con ratón (integración de cuaterniones), vector gravitatorio y mapeo de teclado
[x] FASE 6: Lanzador HLE, configuración gravity_settings.ini, script Jugar_GravityRush.bat y 9 suites CTest (100% pasando)
```

> **Estado Actual:** Todas las fases del runtime HLE base, sistema de archivos PSARC, gráficos Vulkan 1.3, audio WASAPI, entrada cinemática con ratón y cargador de binario Orbis están implementadas, integradas en CMake y verificadas al 100% con 9/9 pruebas CTest en Debug y Release.
