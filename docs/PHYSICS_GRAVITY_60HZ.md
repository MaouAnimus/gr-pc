# Físicas, sincronización y gravedad 360°

Implementación y comprobaciones del 24 de octubre de 2026.

Se corrigieron servicios de tiempo y sincronización que utiliza el invitado,
y se incorporó una simulación nativa con paso fijo y colisión continua.
**El controlador nativo aún no está conectado al mundo Havok del ejecutable.**
La [evidencia Havok](HAVOK_GUEST_EVIDENCE.md) identifica código y contratos
pendientes sin presentar direcciones candidatas como funciones invocables.

## Rutas activas del runtime

- `sceKernelReadTsc` conserva el TSC real, compatible con las instrucciones
  `rdtsc` presentes dentro del ejecutable. `sceKernelGetTscFrequency` obtiene
  la relación del cristal por CPUID cuando está disponible; en otro caso
  toma la mediana de tres calibraciones de 20 ms contra QPC. Se eliminó la
  frecuencia fija de 1,6 GHz que no correspondía al contador del equipo.
  En una medición local se obtuvieron 2.904.003.878 Hz: una espera de 60 ms
  midió 60.479 µs mediante TSC y 60.480 µs mediante el reloj de proceso.
- `sceKernelGetProcessTime` usa QPC monotónico. La hora de calendario no se
  transforma en tiempo de simulación. `sceKernelUsleep` conserva precisión
  de microsegundos mediante timers de alta resolución cuando Windows los
  ofrece; cero cede ejecución sin imponer una espera de un milisegundo.
- VideoOut usa plazos absolutos de 60 Hz, calculados como `floor(n*1e9/60)`
  nanosegundos. Esto evita acumular el redondeo de 16.666 µs y el coste de
  trabajo de cada frame. Los plazos vencidos se omiten para no emitir flips
  atrasados en ráfaga. El mismo limitador se aplica en headless y Vulkan.
- Las condiciones registran la espera antes de liberar el mutex invitado,
  eliminando una ventana en la que se perdía una señal. Los adaptadores
  `scePthreadCond*` devuelven errores libkernel codificados; los POSIX
  conservan sus valores errno positivos.
- Los ocho adaptadores EventFlag usan handles de 64 bits y objetos reales:
  AND/OR, limpieza al consumir, FIFO/prioridad, timeout con tiempo restante,
  cancelación reutilizable y despertar por borrado. `ClearEventFlag`
  conserva los bits de la máscara suministrada. La limpieza del registro
  libera esos objetos y despierta las esperas pendientes. Los nombres se
  validan; `OpenEventFlag` y sus alias de propiedad no están implementados.

Windows no es un sistema de tiempo real. Los plazos pueden despertarse tarde
y el FIFO de Vulkan depende del monitor; esto no demuestra una frecuencia
física de presentación exacta ni fija por sí solo el delta de Havok. La
coherencia de TSC presupone un host con contador invariante y sincronizado;
la prueba informa esa capacidad. El equipo comprobado dispone de ella.

## Paso fijo y contrato de trabajos

`physics::FixedStepDriver` acumula tiempo con unidades enteras de nanosegundo
por 60 y llama siempre con `dt=1/60`. Por defecto realiza hasta ocho pasos por
actualización; conserva el resto pendiente sin agrandar el delta ni desechar
tiempo. Un segundo acumulado produce exactamente 60 pasos, aunque se procese
en varias actualizaciones de recuperación.

Su callback debe despachar y esperar los trabajos del mundo antes de volver.
El siguiente tick empieza después de ese retorno. Un guard atómico impide
actualizaciones concurrentes y reentrada, incluido `reset`. Si el callback
falla o lanza una excepción, el driver se detiene hasta un reset explícito;
no reintenta silenciosamente un mundo que podría haber quedado modificado.

El driver se compila en la biblioteca y se usa en las pruebas integradas.
**Todavía no existe un callback verificado hacia el mundo Havok del juego**:
el ritmo fijo sólo está garantizado para las simulaciones conectadas al driver.

## Gravedad 3D y colisión continua

`physics::step_sphere_60hz` integra velocidad y posición durante un único
paso de 1/60 con gravedad expresada como vector 3D. No fija un eje vertical.
Barre la esfera a lo largo de todo el desplazamiento, en lugar de comprobar
sólo la posición final. Detecta analíticamente caras de ambos lados de los
triángulos, aristas finitas y vértices; calcula el primer instante de contacto
y proyecta la velocidad sobre el conjunto de contactos para deslizar.

Cuando se agota el presupuesto de contactos se detiene en la última posición
segura y comunica un avance parcial. La aplicación debe tratar ese resultado
explícitamente. Los datos inválidos, geometría degenerada y penetraciones
iniciales se rechazan sin avanzar.

El alcance es una esfera contra triángulos estáticos, sin fricción ni rebote.
No resuelve cuerpos móviles, rotación, recuperación de penetración ni carga
de geometría Havok del juego. Las tolerancias dependen de las unidades y la
escala; se rechazan escalas que no permiten representar la esfera. No sustituye
la calidad de colisión o la cola TOI de Havok.

## Validación

- **123/123 suites Release aprobadas**, sin omisiones; **25/25 suites Debug**
  de físicas, tiempo, EventFlags y condiciones aprobadas. El recuento total
  incluye el resto del runtime presente en el checkout.
- Prueba de 10.000 intercambios entre hilos: la nueva regresión falla al
  compilarla con la condición original y pasa con la corrección.
- Dos trabajadores creados mediante el adaptador pthread ejecutan 100 ticks
  sintéticos y devuelven bits de finalización por EventFlags. El coordinador
  verifica ambos resultados antes del tick siguiente. Otra prueba usa
  funciones SysV resueltas desde el registro, con canarios de handles,
  patrones y timeout para detectar daños de ABI.
- Caída de 2.000 unidades/s contra un suelo sin espesor; choques de 3.000 y
  1.000.000 unidades/s contra paredes. Se verifican posición de contacto y
  velocidad, además de aristas, vértices y contactos múltiples.
- Ocho orientaciones: las seis direcciones de los ejes y dos direcciones
  oblicuas, además de caída hacia un techo. El resultado integrado coincide a
  30/60/144 Hz, incluidos cambios de gravedad y recuperación de un segundo
  pendiente. Los límites temporales acumulados conservan ese segundo exacto.
- Pruebas de error/excepción, reentrada, acceso concurrente, desbordamiento y
  retención de fracciones temporales.
- Arranque Release headless de cinco segundos: alcanzó envíos GNM y flips
  consecutivos; terminó con `guest_timeout`, salida 4, al vencer el límite
  previsto. No es prueba de una partida, de ejecución de Havok ni de CCD real.
  La captura local está en `out/evidence/20261024T134447.265357Z_physics_timing`.

Reproducción desde la raíz, después de compilar:

```powershell
rtk proxy C:/msys64/clang64/bin/ctest.exe --test-dir out/release -R '^(physics_runtime_|gravity_motion_|event_flag_|pthread_sync_)' --output-on-failure
rtk proxy python tools/boot_trace.py --configuration Release --timeout-ms 5000 --label physics_timing
```

El arranque devuelve un error por timeout aunque guarde evidencia; no se
interpreta como éxito de juego. `CUSA01130` permanece sin cambios.

Para completar Havok real hay que verificar el mundo, el ABI del paso y su
llamador, la barrera de sus workers, la actualización de gravedad del personaje
y los parámetros efectivos de CCD. Esos contratos se enumeran con direcciones
y límites en [HAVOK_GUEST_EVIDENCE.md](HAVOK_GUEST_EVIDENCE.md).
