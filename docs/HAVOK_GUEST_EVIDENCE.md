# Havok invitado: evidencia y contrato de integración

Fecha: 24 de octubre de 2026. Objetivo: `CUSA01130/eboot.bin`.
SHA-256 verificado:
`7c0216e8ff7254d03141e98c2df2b69f4f2259d08f2fe2754c33c5a3f137033a`.
Todas las direcciones indicadas son offsets dentro del módulo, sin base de carga.

## Método y límites

Se inspeccionaron los segmentos SELF sin compresión mediante Python y se
desensamblaron sus instrucciones con LLVM 23.1.3. Esto es evidencia local,
**no evidencia REA**, y no demuestra ejecución de las rutas encontradas.
El barrido de instrucciones abarca el prefijo ejecutable hasta el final de la
PLT contigua: 16.514.784 bytes del segmento de código, ubicado en el offset de
archivo `0xb710`. Los bytes restantes del segmento se excluyeron del barrido.
Las búsquedas de cadenas abarcan los dos segmentos PT_LOAD respaldados por archivo.

Las herramientas MCP REA no estaban disponibles en la sesión. Su diagnóstico
de solo lectura (`rea-agents@6.5.0 doctor --client codex --json`) encontró
registro Codex ausente y ningún proveedor nativo utilizable: Hopper ausente y
Ghidra configurado en una instalación sin `Ghidra/application.properties`.
No se modificó configuración ni se instaló un motor para resolverlo.

Los bloques concretos se pueden volver a inspeccionar con
`rtk proxy python tools/disassemble_guest.py DIRECCION --size TAMANO`.
Esta herramienta confirma el hash y conserva las direcciones relativas.
Los hallazgos principales están completos en este documento; no dependen de
los resultados auxiliares ignorados por Git bajo `out/evidence/havok`.

## Relojes: instrucciones verificadas y consecuencias

El barrido encontró **776 instrucciones `rdtsc`**, cantidad estática que no
indica cuántas se ejecutaron. Por tanto, el código invitado puede consultar
directamente el contador del host, además del adaptador `sceKernelReadTsc`.

Un flujo verificado de monitorización Havok está en `+0xc761c3..+0xc761f3`:

1. Carga `FS:0`, aplica el offset TLS de `+0x1d771e0` y obtiene el stream.
2. Comprueba el límite de escritura y guarda la etiqueta
   `TtWaitForWorkerThreads`, ubicada en `+0x14046bc`.
3. Ejecuta `rdtsc` en `+0xc761ea`, guarda EAX en `[rsi+8]` y avanza 16 bytes.

Por separado, la PLT `+0xfbfd30` usa la GOT `+0x1d78670` para el NID
`1j3S3n-tTW4`, identificado como `sceKernelGetTscFrequency`.
Se verificaron estas seis llamadas directas y conversiones a segundos:

| Llamada a frecuencia | División posterior / uso observado |
| --- | --- |
| `+0xd78add` | `+0xd78b14`, diferencia de ticks convertida a float |
| `+0xd81baa` | `+0xd81be1`, diferencia de ticks convertida a float |
| `+0xdb7c85` | `+0xdb7cb9`, resultado guardado en `[global+0x17b0]` |
| `+0xdb7ccc` | `+0xdb7d00`, resultado guardado en `[global+0x17b4]` |
| `+0xdb7d13` | `+0xdb7d47`, diferencia de ticks convertida a float |
| `+0xdbb6bb` | Conversión que vuelve a la división en `+0xdbb640` |

Las fuentes de ticks demostradas en esos bloques son llamadas a
`sceKernelReadTsc`, PLT `+0xfbfd20`; ejemplos: `+0xd78a9e`, `+0xd81b6e` y
`+0xdbb67b`. **No se demostró flujo desde los `rdtsc` inline a esas divisiones.**

Cambiar solo el adaptador no cambia los `rdtsc` inline. Mantener contador y
frecuencia en el mismo dominio del TSC host evita introducir una escala
incompatible. El ritmo de presentación y el reloj de proceso tampoco
identifican ni fijan por sí solos el delta de simulación Havok.

## Workers y barrera: observación frente a candidato

El código incluye las cadenas `hkpWorld`, `hkpWorldCinfo`,
`hkpContinuousSimulation` y `hkpMultiThreadedSimulation`. Las numerosas
versiones históricas de Havok presentes son datos de compatibilidad;
no establecen la versión del SDK que usa el mundo en ejecución.

`+0x4a7b60..+0x4a7b95` es un inicializador verificado de opciones: escribe
valores y coloca `HavokWorkerThread` (`+0x106aebf`) en `[rdi+0x20]`.
Esto no identifica el callback del worker.

La rutina con prólogo en `+0xc75960` guarda argumentos RDI/RSI/RDX y registra
`TtPhysics 2012`. Su bloque final registra `TtWaitForWorkerThreads` y contiene:

| Instrucción | Operación verificada |
| --- | --- |
| `+0xc761fc` | Llama `+0x4a6220` con RDI=r15, ESI=0 |
| `+0xc7620a` | Llama `+0x4a63e0` con RDI=r15 y una opción guardada |
| `+0xc7620f..+0xc76215` | Lee la vtable de r14 y llama `[vtable+0x40]` |

Los wrappers `+0x4a6220` y `+0x4a63e0` usan mutex con un intento de spin.
Sus PLT `+0xfbf7e0`, `+0xfbf7f0` y `+0xfbf800` corresponden a
`posix_pthread_mutex_trylock`, `posix_pthread_mutex_lock` y
`posix_pthread_mutex_unlock`.

El dispatch de `+0xc76215` es **candidato a espera de workers** por su contexto.
Su receptor, destino virtual, ABI completo, propiedad e instancia real están
sin verificar. No es un punto de entrada seguro para invocarlo desde el host.

## Colisión continua: código enlazado, configuración pendiente

La cadena `>hkpContinuousSimulation` está en `+0x140354f`. El diagnóstico de
cola TOI llena de `+0x140360e` menciona
`hkpWorldCinfo::m_sizeOfToiEventQueue`; `+0xc647b9` referencia la ruta fuente
de `hkpContinuousSimulation.cpp` en `+0x1403692`.
Esto confirma código de simulación continua enlazado; no confirma que esté
activo en el mundo del juego ni en la calidad de colisión del personaje.

La rutina con prólogo en `+0xc62bd0` registra un monitor de física continua.
Llama `+0xc8b510` en `+0xc62c82`, conserva el float devuelto y lo pasa por
XMM0 a `+0xc622a0` en `+0xc62ccf`, dentro del monitor `TtTOIs`.
Se desconoce si ese valor es delta o tiempo objetivo. No debe sustituirse por
1/60 sin resolver su contrato. Las etiquetas de reflexión `gravity`,
`physicsDeltaTime` y `simulationType` tampoco prueban offsets de objetos vivos.

## Contratos que faltan para conectar la simulación real

- Identificar la entrada real del paso de mundo y su llamador del juego;
  verificar ABI, tipo del delta, unidad, frecuencia y tratamiento de errores.
- Obtener y validar el puntero al mundo, su vida útil y la propiedad del lock;
  distinguir el hilo coordinador, colas y callbacks de trabajo.
- Resolver la barrera de finalización y demostrar que ningún job modifica el
  mundo durante cambios de gravedad, configuración o siguiente paso.
- Identificar el setter de gravedad y su espacio de coordenadas; comprobar la
  actualización del controlador del personaje y sus contactos en 360°.
- Verificar modo de simulación, calidad de colisión y límites TOI efectivos
  del personaje y del mundo antes de cambiar CCD o introducir subpasos.
- Observar caídas rápidas contra geometría real, paredes finas y cambios de
  gravedad; comprobar contactos y penetración entre pasos, no solo presentación.

No se verificó una entrada invocable `hkpWorld::stepDeltaTime` ni un mundo
vivo. Este análisis **no aplica paso fijo ni CCD al juego** y no acredita
navegación gravitacional o ausencia de tunneling durante una partida.
