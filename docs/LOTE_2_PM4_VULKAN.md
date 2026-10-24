# Lote 2: PM4/GNM y envío a Vulkan

Fecha: 23 de octubre de 2026.

## Resultado

Se implementó un decodificador PM4, una cola con capacidad limitada y un camino
de grabación hacia Vulkan. El adaptador sceGnmSubmitCommandBuffers se registra
con el NID zwY0YV91TTI de libSceGnmDriver, verificado en el inventario local.

La prueba gráfica produce píxeles reales con shaders sintéticos propios sobre
la RTX 3050. No es todavía una escena del juego ni un traductor de shaders GCN.
No se declara compatibilidad universal con PM4 ni finalización de todo el lote
gráfico del plan.

## Recorrido implementado

1. El adaptador recibe arreglos de direcciones y tamaños en bytes.
2. Copia metadatos y DCB mediante ReadProcessMemory para rechazar memoria inválida
   sin desreferenciar punteros del invitado directamente.
3. El decodificador valida los paquetes y copia sus operaciones. Un fallo rechaza
   la llamada completa: no deja un prefijo del envío en la cola.
4. Los IB se leen mediante un MemoryReader explícito; hay límites de profundidad,
   tamaño y detección de ciclos. La modalidad chain abandona la cola del padre.
5. La cola conserva las operaciones, independientes de la vida útil de los DCB.
6. Antes de grabar dibujos, se construye un estado provisional y se resuelven
   TODOS los pipelines y recursos requeridos. Un fallo no emite dibujos parciales,
   no confirma el estado y conserva los lotes.
7. La grabación devuelve un ticket asociado a esa cola y a ese recorrido.
8. video.cpp confirma el ticket después de que vkQueueSubmit devuelve éxito.
   Un fallo previo aborta el ticket y conserva el trabajo pendiente.
9. La finalización en GPU continúa dependiendo de los fences del consumidor.
   Un ticket confirmado no significa que la GPU haya terminado.

El consumidor de producción es el recorrido existente de vídeo/presentación.
No se añadió un worker de GPU ni un bucle que espere activamente.

## Subconjunto PM4 soportado

| Paquete | Tratamiento |
| --- | --- |
| Type 2: 0x80000000 | Padding de un DWORD |
| NOP 0x10 | Cuerpo opaco, no se interpreta como paquetes |
| SET_CONFIG_REG 0x68 | Escrituras limitadas al banco config |
| SET_CONTEXT_REG 0x69 | Escrituras limitadas al banco context |
| SET_SH_REG 0x76 | Escrituras limitadas al banco shader |
| SET_UCONFIG_REG 0x79 | Escrituras limitadas al banco uconfig |
| NUM_INSTANCES 0x2F | Número de instancias persistente |
| INDEX_TYPE 0x2A | uint16/uint32 sin intercambio de bytes |
| INDEX_BASE 0x26 | Dirección de índices alineada |
| INDEX_BUFFER_SIZE 0x13 | Estado de límite de índices |
| DRAW_INDEX_AUTO 0x2D | Fuente auto-index, sin flags adicionales |
| DRAW_INDEX_2 0x27 | Índices, base explícita y máximo |
| DRAW_INDEX_OFFSET_2 0x35 | Índices con desplazamiento |
| INDIRECT_BUFFER 0x3F | Buffer indirecto y chain, sin VMID/flags adicionales |

Se comprueban tamaño de cabecera/cuerpo, rango de registros, tamaños de índices,
alineación, banderas y presupuesto. Los errores incluyen DCB, offset en DWORD,
opcode y, para un IB, dirección. Los paquetes predicados, compute o desconocidos
se rechazan explícitamente.

Límites predeterminados: 1.048.576 DWORD por llamada decodificada, incluidos IB;
65.536 operaciones; profundidad de IB 8; 64 envíos y 4.194.304 DWORD pendientes
en la cola. El adaptador admite hasta 64 DCB por llamada.

## Contrato entre cola y renderizador nativo

- GraphicsState conserva registros y estado de índices/instancias.
- DrawResolver traduce ese estado y la petición de dibujo a NativeDraw.
- NativeDraw entrega un pipeline existente, buffers, descriptores y, cuando
  corresponda, viewport, scissor, offsets y parámetros de primer vértice/instancia.
- El pipeline debe ser compatible con el render target y usar viewport/scissor
  dinámicos. El resolver debe comprobar el resto del estado y rechazar lo que
  no pueda representar; la cola no adivina shaders ni materiales.
- Los recursos Vulkan pertenecen al proveedor. Deben vivir hasta el fence de
  finalización de GPU; acknowledge no autoriza destruirlos.
- El resolver no debe reentrar en la misma cola. La grabación tiene un único
  consumidor y la cola admite productores concurrentes.
- abort permite reintentar la grabación; el consumidor debe descartar/resetear
  el command buffer Vulkan previo antes del reintento.
- Inicializar, cambiar el dispositivo o cerrar GNM reinicia la cola y el resolver.
  Los subsistemas y el dispositivo deben prepararse antes de enviar trabajo.
- Los setters y contadores host anteriores se conservan como API de prueba;
  no sustituyen un envío de PM4.

El frame recorder deja de emitir el depth bias artístico fijo que se presentaba
como cel-shading. Las decisiones de material y pipeline corresponden al estado
real que verifique el proveedor.

## Pruebas

Se añadieron siete suites: pm4_decode, pm4_invalid, pm4_indirect, pm4_queue,
pm4_concurrency, pm4_adapter y pm4_vulkan. Junto con las 36 existentes, se validan
43 suites en Debug y Release.

Las pruebas cubren:

- Todas las familias de paquetes listadas, tamaños, tipos, flags y rangos inválidos.
- Rechazo completo de un envío con varios DCB cuando uno es inválido.
- IB válido, IB sin lector, ciclos, profundidad, presupuesto y chain.
- Capacidad limitada y 400 envíos desde cuatro productores.
- Registro del NID y llamada SysV real al adaptador; punteros/tamaños/CCB inválidos.
- Dibujos automáticos, indexados de 16 bits, de 32 bits y con offset. Cada modalidad
  se verifica por separado mediante lectura de píxeles de una imagen Vulkan.
- Copia de DCB: modificarlo después del envío no cambia el dibujo encolado.
- Estado persistente, productor entre grabación y confirmación, abort/reintento,
  tickets obsoletos y tickets de otra cola.
- Falta de recursos o excepción del resolver: no se emite el primer dibujo de
  un lote que falla después; la imagen conserva el color de limpieza y el lote
  sigue pendiente.
- Integración de sceGnmSubmitCommandBuffers -> cola global -> recordFrame ->
  envío Vulkan -> lectura de píxeles.

glslc compila los dos shaders de prueba durante el build. No se necesita un
compilador de shaders para construir la biblioteca con BUILD_TESTING=OFF.
El toolchain de desarrollo debe incluir glslc cuando se construyen las pruebas.

La prueba intenta activar VK_LAYER_KHRONOS_validation si está instalada. En este
equipo no estaba disponible; la comprobación efectiva se hizo con la GPU real,
códigos de retorno, fences y lectura de píxeles. No se declara una ejecución
con las capas de validación.

Registros: out/debug/Testing/Temporary/LastTest.log y
out/release/Testing/Temporary/LastTest.log.

## Pendiente antes de dibujar el juego

- Obtener y reproducir un command buffer auténtico de esta edición.
- Traducir shaders GCN y crear el proveedor/caché de pipelines para el juego.
- Mapear recursos reales, layouts de texturas, render targets y descriptores.
- Traducir completamente el estado de registros utilizado por esos dibujos.
- Añadir por evidencia CONTEXT_CONTROL/CLEAR_STATE, eventos, EOP, WRITE_DATA,
  WAIT_REG_MEM, barreras, DMA, dispatch y las demás instrucciones necesarias.
- Implementar el constant engine/CCB. Se rechaza cuando su tamaño es distinto de cero.
- Integrar SubmitAndFlip, SubmitDone, etiquetas y eventos con la finalización
  real de GPU y la asociación entre envíos y frames. No se añadieron stubs de
  éxito para aparentar esa semántica.

Por defecto no hay DrawResolver del juego. Si llega un dibujo sin sus recursos,
la grabación falla explícitamente y vídeo registra el motivo. Este límite evita
presentar un triángulo sintético como si fuera geometría del juego.

## Archivos y referencias

Implementación:
- engine/include/hle/gnm/pm4.h y engine/src/gnm/pm4.cpp
- engine/include/hle/gnm/command_queue.h y engine/src/gnm/command_queue.cpp
- engine/src/gnm/submission.cpp
- Integración en engine/src/gnm/gnm.cpp, engine/src/video.cpp y el registro de adaptadores.
- tests/pm4_tests.cpp, tests/pm4_vulkan_test.cpp y tests/shaders/.

Los formatos/opcodes y firmas se contrastaron con fuentes primarias; la
implementación añadida es propia y no integra un emulador externo:
- [Definiciones PM4](https://github.com/shadps4-emu/shadPS4/blob/main/src/video_core/amdgpu/pm4_cmds.h).
- [Opcodes PM4](https://github.com/shadps4-emu/shadPS4/blob/main/src/video_core/amdgpu/pm4_opcodes.h).
- [Firmas GNM](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/gnmdriver/gnmdriver.h).
- [Dibujos Vulkan y requisitos del pipeline](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDraw.html).

