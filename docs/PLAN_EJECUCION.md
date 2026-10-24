# Plan operativo del port nativo

Fecha: 23 de octubre de 2026.
Objetivo del procedimiento: obtener una primera imagen auténtica del juego y
continuar hasta una versión jugable, conservando CPU directa y backends PC propios.

Este documento ordena el trabajo desde el estado actual. El alcance general está
en [PLAN_PORT_NATIVO_PC.md](PLAN_PORT_NATIVO_PC.md) y la revisión de evidencia
en [CONTRASTE_AVANCE.md](CONTRASTE_AVANCE.md).

## Seguimiento de implementación

Se ejecutó un primer lote de arranque y estabilización. Resultado: **68/68 suites
en Debug y Release**, y ambos arranques detenidos en `sceFiosInitialize`
(`wAKZ-det+yo`, `eboot.bin+0x4FDC`). El producto todavía no es jugable.
Detalles, evidencia y límites: [ejecución del lote](EJECUCION_LOTE_ARRANQUE.md).

| Tarea | Estado | Resultado y dependencia restante |
| --- | --- | --- |
| T01 Baseline | Validada para este lote | Intentos conservados por fecha, configuración, hashes y diagnóstico |
| T02 SystemService | Validada | El juego recibe idioma 20 y supera la parada anterior |
| T03 Adaptadores | En curso | Hilos, mutex, RW locks, semáforos, equeue y memoria implementados en el alcance probado; FIOS, módulos y vídeo pendientes |
| T04 Contexto | Parcial | TLS nativo y marcos grandes comprobados; pilas de fibras suministradas por el juego pendientes |
| T05 Captura PM4 | Pendiente | Depende de superar FIOS y los siguientes servicios del arranque |
| T06 Primera imagen | Pendiente | Necesita envío auténtico, shaders, recursos y presentación |
| Pasos 6–7 | Pendientes | Partida, guardado, rendimiento y contenido todavía sin validar |

## Punto de partida anterior a este lote

- Últimos registros: 43/43 pruebas en Debug y Release.
- PM4/Vulkan: dibujos sintéticos comprobados por lectura de píxeles.
- Arranque real: parada en sceSystemServiceParamGetInt, NID fZo48un7LK4,
  llamada desde eboot.bin+0x298A.
- Pendiente: escena del juego, shaders/recursos reales, servicios completos,
  integración de audio/entrada, guardado y medición de rendimiento.

La línea base histórica se conserva para comparar. El estado actualizado de
ejecución está en la tabla anterior; redactar o probar un componente no completa
por sí solo el producto.

## Secuencia de trabajo

| Paso | Prioridad | Trabajo | Criterio para avanzar |
| --- | --- | --- | --- |
| 0 | P0 | Registrar baseline, configuración y estado del código | Evidencia reproducible y diferencias identificables |
| 1 | P0 | Resolver sceSystemServiceParamGetInt con su contrato real | El arranque supera esa llamada y queda registrado el siguiente resultado |
| 2 | P0 | Sustituir adaptadores críticos provisionales | Las funciones alcanzadas realizan el servicio o devuelven un error correcto |
| 3 | P0 | Comprobar pilas, TLS y concurrencia desde código invitado | Dirección de pila y datos por hilo/fibra correctos en las pruebas necesarias |
| 4 | P0 | Alcanzar y capturar el primer envío gráfico del juego | DCB/IB/CCB y referencias a recursos identificados, con datos suficientes para replay |
| 5 | P0 | Reproducir el envío con shaders y recursos convertidos | Primera imagen auténtica, independiente de los shaders sintéticos |
| 6 | P1 | Integrar menú, controles, audio y guardado | Sección jugable y recuperación de partida tras cerrar el proceso |
| 7 | P1 | Optimizar y validar el contenido completo | Rendimiento medido, sesiones prolongadas y campaña/contenido declarado verificados |

Los pasos 2 y 3 se atienden según los bloqueos observados durante el arranque.
No es necesario implementar las 543 importaciones pendientes indiscriminadamente
antes de obtener una primera imagen. Sí es necesario respetar la semántica de
cada función que el juego llegue a utilizar.

## Primer lote: desbloquear el arranque

### T01 — Baseline operativo

Registrar versión del juego, configuración, revisión/diferencias del código,
ruta de ejecución y estado de pruebas. Conservar las trazas por intento para
evitar sobrescribir la única evidencia. No modificar los archivos originales.

Aceptación: otro intento con los mismos datos reproduce la parada conocida
o documenta con claridad cualquier diferencia.

### T02 — Contrato de sceSystemServiceParamGetInt

Investigar firma, identificadores de parámetros, valores de salida y códigos
de error. Registrar el parámetro que pide esta edición. Implementar el
adaptador mínimo correcto y registrarlo por biblioteca/NID.

Pruebas: parámetro utilizado, puntero de salida inválido y parámetro no
soportado según el contrato contrastado. No devolver éxito con una salida
inventada ni usar valores universales para todos los parámetros.

Aceptación: la llamada recibe una respuesta válida y el juego progresa
hasta el siguiente bloqueo, un envío gráfico o un cierre documentado.

### T03 — Clasificación de adaptadores alcanzados

Revisar especialmente hilos, módulos, semáforos, audio y vídeo. Para cada función,
separar identificación del símbolo, registro de dirección e implementación
semántica. Etiquetar: funcional, parcial, provisional o pendiente.

Los adaptadores que devuelven éxito sin crear un hilo, cargar un módulo o
enviar audio no se consideran funcionales. La clasificación P2 de una función
deja de ser válida si su ausencia bloquea el arranque; elevarla a P0.

Aceptación: las rutas alcanzadas tienen efectos comprobables y los servicios
pendientes quedan identificados sin ocultar sus errores.

## Segundo lote: unir ejecución y gráficos

### T04 — Contexto de ejecución

Verificar RSP dentro de los límites de la pila cuando el contrato lo requiera.
Probar acceso TLS desde código invitado y su aislamiento. Comprobar cambios
entre hilos/fibras y conservación del estado requerido.

Aceptación: la evidencia prueba el contrato necesario; la mera conservación
de canarios en una pila de Windows no sustituye esa comprobación.

### T05 — Captura y replay del primer envío

Instrumentar los límites de envío reales. Copiar de forma controlada los buffers,
identificar referencias a shaders/recursos y conservar el estado relevante.
Registrar opcode y ubicación cuando el decodificador no pueda continuar.

Aceptación: captura reproducible del juego, asociada a su versión, y un caso
de replay que ya no dependa de recorrer todo el arranque para depurarlo.

### T06 — Primer dibujo auténtico

A partir de la captura, implementar los paquetes realmente necesarios,
sincronización, conversión de shaders, recursos y proveedor de pipelines.
Mantener sus objetos vivos hasta la finalización real en GPU.

Aceptación: una imagen procedente del juego. No cuentan un color de limpieza,
un triángulo de prueba ni una imagen que ignore las operaciones pendientes.

## Ciclo obligatorio por tarea

1. Identificar el bloqueo y conservar la evidencia.
2. Contrastar el contrato y delimitar la corrección.
3. Implementar el cambio más pequeño que respete ese contrato.
4. Ejecutar las pruebas relevantes; al cerrar el lote, validar Debug y Release.
5. Repetir el arranque o replay y comparar con la evidencia anterior.
6. Registrar resultado, limitación y próximo bloqueo; actualizar este seguimiento.

Si una prueba falla, reparar o explicar la regresión antes de cerrar la tarea.
Si un contrato es desconocido, mantenerlo como incógnita y buscar evidencia:
no rellenarlo con un éxito vacío. Las decisiones rutinarias se resuelven dentro
del alcance ya acordado del port.

El código de salida 0 del launcher no demuestra que el arranque fuera correcto:
una trampa diagnóstica interceptada puede terminar así. Revisar el informe de
ejecución y el punto alcanzado.

## Seguimiento y cierre

Por tarea se registrará: estado, dependencia, archivos cambiados, evidencia,
prueba y resultado, limitación y siguiente acción. Estados: pendiente, en curso,
validada o bloqueada por una dependencia concreta.

Los hitos a mostrar son: bloqueo de arranque superado -> primer envío real ->
primer dibujo real -> menú/escena -> partida con guardado -> rendimiento ->
contenido completo. No se asignará un porcentaje global a partir del número
de pruebas o de adaptadores registrados.

La meta inicial de rendimiento sigue siendo 1080p/60 en el equipo del usuario.
Solo se evaluará como resultado cuando exista una escena representativa;
separar CPU, GPU, streaming, compilación y presentación para decidir qué optimizar.

