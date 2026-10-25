# Correcciones de la revisión de shaders y arranque

24 de octubre de 2026.

## Traducción explícita y validación

Se corrigieron los números de operación VALU de Sea Islands, los destinos
VOP3 y el reconocimiento de `IMAGE_SAMPLE`. Las operaciones no implementadas
se rechazan con offset y opcode; ya no se sustituyen por identidad, ceros,
operaciones MAD genéricas o ejecución lineal de saltos. Esto incluye funciones
de fetch, memoria, LDS, máscaras de ejecución y exportaciones comprimidas que
todavía requieren una implementación completa.

Los registros de entrada y las asociaciones de atributos, recursos y samplers
deben declararse explícitamente. No se presume que un registro no definido
contenga cero. La generación y las entradas de caché se validan con
SPIRV-Tools para Vulkan 1.2, además de las comprobaciones de integridad de disco.

La identidad de caché incluye la versión estricta del traductor, etapa,
entorno, registros, valores de entrada e interfaz completa. Las entradas de la
versión anterior dejan de coincidir. La caché de pipelines utiliza hashes del
contenido y del formato de destino, evitando colisiones de pares de direcciones
y reutilización de un programa antiguo cuando se recicla su dirección.

Si un shader invitado no se puede traducir, se rechaza el dibujo. El pipeline
sintético sólo se usa en la ruta sin direcciones de shaders invitados; no se
presenta como traducción del juego. El escaneo del programa comprueba que
`S_ENDPGM` sea una instrucción, en lugar de confundir un literal con el final.

## Datos y sincronización Vulkan

Cada dibujo conserva una copia de sus propios vértices e índices. El ticket de
grabación retiene los recursos y el renderizador los transfiere a la ranura de
la fence tras el envío, antes de confirmar la cola. Sólo se liberan después de
completar esa fence. Los rechazos y grabaciones abortadas liberan sus copias.

La lectura del quad valida los 32 bytes que necesita; también se comprueban
sumas de índices, rangos de memoria y referencias fuera del búfer de vértices.
Las texturas estáticas y la blanca reciben una transición real de
`PREINITIALIZED` a `GENERAL`, usando un comando y una fence de subida. La copia
respeta `VkSubresourceLayout::rowPitch` y su offset. Los formatos comprimidos o
detiling no soportados se rechazan en lugar de leerlos como píxeles lineales.

## Regresiones

- Valores numéricos para las instrucciones corregidas, destinos/modificadores
  VOP3, muestreo y rechazo de instrucciones sin implementación.
- Validación semántica SPIR-V para Vulkan; no sólo magic y longitud de palabras.
- Dos dibujos indexados con geometría diferente, textura 3×2 y lectura de
  píxeles rojo/verde. Propiedad comprobada antes y después de confirmar la cola
  y completar la fence; cobertura de errores y grabaciones abortadas.
- Página protegida con sólo ocho bytes legibles de un quad: rechazo sin leer
  los otros 24 bytes ni cerrar el proceso.
- Lectura FIOS de `arc0/lua//Game/Param/globals.evs`, cabecera `FBKK`, tipo 20,
  tamaño 1.667.023 bytes, offsets no nulos, callbacks y convención SysV.

## Alcance del arranque

Dos intentos Release consecutivos de cinco segundos, con el mismo ejecutable,
leyeron los recursos Lua completos y no reprodujeron el fallo
`eboot.bin+0x148556`, lectura de `0x10`. Ambos terminaron con
`guest_timeout`/salida 4 al vencer su plazo. Los cambios concurrentes de FIOS y
normalización de rutas ya estaban presentes al empezar esta corrección; el
recurso exacto del fallo anterior no quedó capturado y no se atribuye a ciegas
a un único archivo o cambio.

Las capturas se conservan en `out/evidence/20261024T183354.742632Z_final_resource_regression_1`
y `out/evidence/20261024T183401.024133Z_final_resource_regression_2`.
Esto no certifica un arranque completo ni el renderizado de Kat o Hekseville.
El subconjunto GCN sigue siendo limitado y los contratos de recursos reales,
fetch, interpolación y simulación Havok continúan pendientes.
