> Documento histórico de fase 1. Para el alcance y las pruebas actuales, consultar [ESTABILIZACION.md](ESTABILIZACION.md) y el [README](../README.md).

# Fase 1 — backend host de memoria, kernel y fibras

Estado al 19 de octubre de 2026: implementado el backend C++23 de Windows y su
diagnóstico. La integración binaria con Orbis y el arranque del juego siguen
pendientes. El código no necesita abrir, modificar ni distribuir el volcado.

## Alcance y contratos

### Memoria

`gravity::memory::AddressSpace` reserva por defecto 4 GiB en `0x1000000000`
usando `VirtualAlloc2`, resuelto desde `KernelBase.dll`. Reserva sin consumir
4 GiB de memoria física; cada commit carga únicamente las páginas solicitadas.
También se prueban reservas de 6 GiB y offsets superiores a 4 GiB.

Si la dirección está ocupada se devuelve el error de Windows. No se cambia
silenciosamente de base. Base y tamaño deben estar alineados; las operaciones
por rango comprueban alineación, longitud y límites sin sumar valores que
puedan desbordarse. Las protecciones disponibles excluyen páginas RWX.
`commit` no cambia los permisos de páginas que ya estaban comprometidas:
para eso se usa `protect`. La transición a RX vacía la caché de instrucciones.
Las mutaciones de un AddressSpace se serializan desde el llamador.

La dirección es una política inicial, no un mapa de memoria validado del juego.
En fase 2 habrá que conciliar segmentos, relocalizaciones, TLS y rangos reservados.

### Sincronización y reloj

- `Mutex`: almacenamiento en el objeto, sin asignaciones explícitas al heap.
  El SRWLOCK protege los datos; el ID de propietario es atómico. El tipo
  `error_check` detecta bloqueo propio y desbloqueo por otro hilo. El tipo
  `recursive` mantiene profundidad sin adquirir recursivamente el SRWLOCK.
- `EventFlag`: bits de 64 posiciones, espera por todos o cualquiera de los bits,
  borrado de bits coincidentes o de todo el patrón. `observed` contiene el patrón
  completo anterior al borrado y solo se modifica con éxito. Un timeout de cero
  sondea. Los despertares espurios no reinician el plazo. `clear` recibe bits a
  borrar; ese contrato host no presume la convención de máscaras de Orbis.
  `cancel` es permanente y despierta a todos; no equivale todavía al cancel
  reutilizable ni al borrado de un objeto Orbis.
- `Semaphore`: `CreateSemaphoreW`, conteo inicial y máximo, espera, publicación
  y error si una publicación supera el máximo.
- `ProcessClock`: QPC y frecuencia inicializados juntos; informa microsegundos
  transcurridos desde la construcción. La conversión separa segundos y resto
  para evitar multiplicar todo el contador por un millón. Este será el backend
  de `sceKernelGetProcessTime`, que aún no se exporta. El runtime deberá poseer
  un reloj único para definir la época del proceso invitado.

Los timeouts host son milisegundos de Windows; todavía no hay adaptación de
timeouts Orbis en microsegundos ni escritura de tiempo restante. No hay mutexes
robustos, prioridad heredada, semáforos ponderados, handles invitados, pthreads
invitados ni event queues. Los objetos no se destruyen con usuarios o esperas
activas. Mutexes y esperas usan identidad de hilo de Windows, no de fibra, y
bloquean el hilo: no sustituyen un planificador de trabajos.

### Fibras

`FiberScheduler::attach` convierte el hilo con `ConvertThreadToFiberEx` o toma
prestada su fibra actual si ya estaba convertido. Solo un scheduler puede estar
adjunto por hilo. `detach` requiere que todas sus fibras hayan sido finalizadas;
solo revierte la conversión cuando la hizo el propio scheduler.

Cada `Fiber` es inmóvil y usa `CreateFiberEx` con una pila reservada de 1 MiB y
64 KiB comprometidos por defecto. Se solicitan cambios de estado de coma flotante
con `FIBER_FLAG_FLOAT_SWITCH`. No se fabrican estructuras de contexto de Sony.

| Operación host | Contrato |
| --- | --- |
| `initialize(scheduler, entry, initial, options)` | Crea la fibra en estado ready; no ejecuta el callback |
| `run(target, argument, returned)` | Solo desde la raíz; entra en una fibra ready/suspended y espera hasta el próximo retorno a la raíz |
| `switch_to(target, argument, resumed)` | Solo desde una fibra activa; suspende la fuente y activa el destino |
| `return_to_thread(argument, resumed)` | Suspende la fuente y termina el `run` actual con ese argumento |
| `finalize()` | Libera una fibra ready/completed; rechaza running/suspended |

El callback recibe `(initial, primer_argumento)`, ambos de 64 bits. Al reanudarse
una llamada suspendida, su referencia `resumed` recibe el argumento nuevo.
Un cambio A → B no crea una pila de retornos entre fibras: B puede devolver al
hilo, y el hilo puede reanudar A o B. Cada fibra conserva su pila propia.

La función de inicio de Windows conserva `WINAPI`; el puntero del callback lleva
`__attribute__((sysv_abi))`. Clang genera las llamadas en ambos sentidos, con las
reglas de registros, alineación, shadow space y red zone pertinentes. Esta prueba
solo certifica el contrato de callback utilizado, no todas las firmas de imports.

Si el callback termina normalmente, la fibra queda completed y vuelve a la raíz
con valor cero. Si lanza una excepción de C++, se captura dentro del trampoline,
se guarda un `exception_ptr` y `run` devuelve `callback_exception`. Esto es una
política host, no una afirmación sobre el comportamiento de Sony. No captura
fallos de acceso ni otras excepciones SEH. El trampoline nunca retorna a Windows,
porque retornar de un FiberProc termina el hilo.

Se rechazan llamadas desde otro hilo, auto-switch, destinos finalizados, uso
reentrante de `run` y pilas externas. Las fibras suspendidas deben reanudarse
hasta terminar antes de finalizarse. Destruir una fibra activa/suspendida, o
destruir su scheduler antes que las fibras, llama a `std::terminate` en todas
las configuraciones. Esto evita liberar una pila activa o saltarse silenciosamente
destructores locales. No se permite migración entre hilos ni reset de contextos.
Los métodos de inspección de una fibra se usan desde su hilo propietario.

## Adaptación futura a libSceFiber

Los nombres de operación corresponden conceptualmente a inicialización, Run,
Switch y ReturnToThread; todavía no se exportan funciones `sce*`. Antes de
conectar el loader hay que establecer con evidencia:

1. Layout/alineación de `SceFiber`, parámetros opcionales y códigos de error.
2. NIDs y firmas realmente importados por este ejecutable.
3. Uso real de stacks invitados, contextos sin pila externa y resets.
4. Política de finalización y retorno del callback de la biblioteca original.
5. Interacción con TLS, señales, excepciones invitadas y registros SIMD ampliados.

Si el juego proporciona `addrContext`, este backend devuelve unsupported.
Se necesitaría un backend de contexto dedicado que respete esa pila; no se puede
prometer equivalencia sustituyéndola en silencio por una pila de Windows.
Importar el nombre `libSceFiber.prx` por sí solo no demuestra la intensidad de uso
ni cómo el motor organiza sus tareas.

## Verificación

CTest separa tres suites y aplica un límite de 30 segundos a cada una.
Los checks no dependen de `assert` y siguen activos con optimización.

- Memoria: dirección real, reservas de 4/6 GiB, conflicto de dirección, límites,
  permisos consultados con VirtualQuery, decommit/recommit a cero y limpieza RAII.
- Kernel: errores de propietario, profundidad recursiva, 100.000 incrementos
  protegidos entre cuatro hilos, AND/OR y borrado de flags, plazo con despertares
  ajenos, cancelación multihilo, conteo/desbordamiento y reloj monótono.
- Fibras: argumentos con bits altos, A → B → hilo → B → A, locales de pila,
  rechazo de estados inválidos, excepción del callback, destrucción controlada,
  reutilización de objetos y de conversión externa del hilo. La prueba larga
  realiza 100.000 ciclos de suspensión/reanudación: más de 200.000 transferencias,
  y comprueba el control de redondeo MXCSR y datos flotantes.

El ejecutable `gravity_runtime_probe` reserva memoria y ejecuta un callback SysV,
obtiene 42, lo reanuda con 99 y libera sus recursos. No intenta arrancar el juego.

Los registros reproducibles de compilación y pruebas se generan con
`tools/build_runtime.ps1`. Se detallan resultados en `docs/worklog.md`.

## Referencias primarias

- [VirtualAlloc2](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc2)
- [SRW locks](https://learn.microsoft.com/en-us/windows/win32/sync/slim-reader-writer--srw--locks)
- [CreateFiberEx](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createfiberex)
- [SwitchToFiber](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-switchtofiber)
- [QueryPerformanceCounter y conversión de tiempo](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)

Implementación propia del backend a partir del contrato Win32; no se incorporó
código de emuladores. Esta fase no incluye WASAPI/NGS2: se mantiene en la fase 4.
