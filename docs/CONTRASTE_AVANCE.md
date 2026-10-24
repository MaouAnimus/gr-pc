# Contraste del análisis inicial con el estado actual

> Este contraste conserva el estado anterior a la implementación del plan.
> El [lote ejecutado posteriormente](EJECUCION_LOTE_ARRANQUE.md) añade servicios,
> TLS, pilas y sincronización: 68 suites por configuración y parada en
> `sceFiosInitialize`. También corrige el inventario: 539 nombres identificados,
> 142 pendientes; una exportación sin nombre no verifica un símbolo ni una firma.

Fecha: 23 de octubre de 2026.
Objetivo: port propio para Windows x64, ejecución directa de lógica en CPU y
subsistemas PC. No cambia la decisión de construir el producto sin un emulador externo.

## Dictamen

El proyecto ha pasado de un inspector con backends host a un prototipo de
ejecución e integración gráfica. Hay avance técnico comprobable: arranque
instrumentado, adaptadores, una matriz ABI más amplia y PM4 que produce dibujos
sintéticos reales en Vulkan. Todavía no hay menú, escena del juego ni partida.

La mayor distancia pendiente está en conectar el juego real con servicios y
recursos completos. Tener ambos extremos funcionando por separado no demuestra
que la integración esté terminada.

## Evidencia revisada

- Plan inicial del port: baseline con 14 suites, inspección y 669 nombres pendientes.
- Análisis anterior a la estabilización: 9 suites y 302.608 relocalizaciones.
- Código actual de launcher, runner, registro de adaptadores, fibras y PM4.
- Inventarios de imports y dependencias.
- Registros actuales de CTest: 43 pruebas aprobadas en Debug y 43 en Release,
  sin omisiones en esos registros. No se volvieron a ejecutar esas suites durante
  este contraste; se revisó su evidencia y el código.
- Nueva ejecución de arranque Release, sin gráficos y con límite de 5 segundos.
  Resultado conservado en out/release/contrast_boot.log.

## Antes y ahora

| Área | Análisis inicial | Estado actual | Evaluación |
| --- | --- | --- | --- |
| Arquitectura | CPU directa o recompilación estática por decidir | Decisión documentada a favor de CPU directa y adaptadores propios | Decisión tomada; rendimiento total aún por medir |
| Ejecutable | Se cargaba e inspeccionaba sin transferir ejecución | --boot ejecuta la entrada del juego y registra el punto de parada | Avance real de arranque, no arranque completo |
| ABI | Ejemplo de seis enteros y pruebas de fibras | Ocho suites de matriz ABI: pila, flotantes, mixtos, structs, callbacks, varargs y retornos | Mucho mejor cobertura sintética |
| Dependencias | Inventario preliminar y sustituciones silenciosas | 681 imports catalogados; 641 marcados como identificados y 40 sin identificar | Identificación avanzada; no equivale a implementación |
| Resolución | 669 nombres pendientes después de estabilizar | 543 pendientes en el diagnóstico actual; 138 registros de adaptadores en el código | Más conexiones, con semántica provisional en varias |
| Fibras | Layout preliminar y copias sombra de pila | Layout de 112 bytes, registro externo y pruebas adicionales; sin copia sombra | Mejora estructural; equivalencia de pilas/TLS originales aún requiere pruebas |
| Gráficos | Solo limpieza de pantalla y contadores | 13 opcodes PM4, IB, cola, adaptador GNM y dibujos Vulkan con píxeles comprobados | Canal de prueba funcional; recursos del juego pendientes |
| Archivos | PSARC y VFS de lectura | Base conservada e inventario de datos | Streaming y API completa del juego pendientes |
| Audio/entrada | Backends básicos | Siguen los backends; los imports no tienen toda la integración funcional | Sin evidencia de audio/controles durante una partida |
| Guardado | Pendiente | Pendiente | Necesario para declarar una versión jugable |
| Rendimiento | Meta 1080p/60 sin escena de referencia | Sin benchmark de una escena del juego | No hay evidencia de FPS ni ahorro global todavía |
| Producto | Sin ejecución autónoma del contenido | Sigue sin campaña ni empaquetado final | Prototipo de integración, no producto terminado |

La corrección de RELA/PLT aumentó las posiciones procesadas a 303.268. Esa cifra
se mantiene: no es un porcentaje de compatibilidad. La evolución de pruebas es
9 iniciales -> 14 tras estabilización -> 36 tras Prioridad 1 -> 43 tras PM4.

## Arranque comprobado en esta revisión

La entrada 0x10000002F0 se ejecutó durante aproximadamente 1,11 segundos antes
de detenerse en una trampa intencional del runtime:

- NID: fZo48un7LK4.
- Función identificada en el catálogo: sceSystemServiceParamGetInt.
- Biblioteca: libSceSystemService; la traza presenta su identificador R.
- Dirección de llamada: eboot.bin+0x298A.
- El informe de ejecución dice FAILED, con Illegal Instruction por la trampa.
- El proceso devuelve 0 porque el launcher trata esa trampa interceptada como
  un resultado diagnóstico manejado. No significa que el juego haya arrancado
  correctamente.

No se usa el tiempo transcurrido para deducir millones de instrucciones: no se
midió aquí el número de instrucciones ni se verificó cada subsistema mencionado
en el historial de arranque.

Hay una prioridad mal alineada con la evidencia: el catálogo clasifica esta
función como stub_optional/P2, pero su ausencia detiene el arranque actual.
Debe pasar a bloqueo de arranque de máxima prioridad y recibir una implementación
con contrato contrastado, no otro éxito vacío.

## Límites que no se deben dar por resueltos

### Adaptador registrado frente a función implementada

Existen implementaciones provisionales dentro de adapter_registry.cpp:

- hle_scePthreadCreate entrega un identificador, pero no ejecuta start_routine
  ni crea el hilo.
- hle_sceSysmoduleLoadModule devuelve 0 sin cargar el módulo solicitado.
- hle_sceAudioOutOutput devuelve 0 sin enviar ese audio al backend WASAPI.
- hle_sceVideoOutRegisterBuffers y hle_sceVideoOutSubmitFlip devuelven 0 sin
  conectar completamente buffers y flips del juego con presentación real.

Por ello, reducir los pendientes de 669 a 543 no prueba que las otras
126 conexiones nuevas tengan semántica completa. El tamaño del registro y el
número de símbolos identificados tampoco miden progreso jugable.

### Fibras y TLS

Las pruebas actuales comprueban layout, ciclo de vida, conservación de variables
locales y almacenamiento TLS host. Se retiró el puntero host de la estructura
visible y la copia sombra, lo cual corrige problemas del diseño anterior.

Sin embargo, Fiber::initialize sigue utilizando CreateFiberEx y rechaza
FiberOptions::supplied_stack. El wrapper guarda los límites de la pila recibida,
pero inicializa la fibra host sin ejecutar explícitamente sobre esa pila.
Conservar canarios en una pila de Windows no prueba que RSP pertenezca al rango
de pila proporcionado por el juego.

ThreadTls usa slots de Windows. Es necesario probar el acceso TLS tal como lo
realiza código del juego, además de la lectura/escritura mediante helpers host.
Estas observaciones limitan la afirmación «integración Orbis completa»; no
invalidan los casos host que sí pasan.

### Gráficos

El tramo PM4 funciona con pruebas sintéticas y emite vkCmdDraw/vkCmdDrawIndexed.
Se verificaron por separado índices de 16/32 bits, offset, rechazo sin dibujos
parciales, reintento y copia independiente del DCB.

No hay aún proveedor de pipelines del juego ni conversión de sus shaders GCN,
texturas o render targets. Faltan también CCB, eventos, esperas, escrituras y
finalización/flip del invitado según los paquetes que aparezcan.

La capa VK_LAYER_KHRONOS_validation no estaba disponible en la prueba registrada.
Sí hubo GPU real, fences y lectura de píxeles. No confundir ese resultado con
la validación de una captura gráfica del juego.

## Ubicación en las fases del plan

| Fase | Estado frente a su criterio de salida |
| --- | --- |
| 0 — Baseline | Avanzada: Git, manifiesto y builds registrados |
| 1 — Inventario | Avanzada: catálogo y grafo; 40 símbolos aún sin identificar |
| 2 — Ejecución nativa | Prototipo demostrado; decisión CPU tomada; contratos completos aún no certificados |
| 3 — Primer arranque | Ejecución real con parada identificada; servicios y dependencias todavía parciales |
| 4 — Recursos | Base disponible; streaming y comportamiento del juego pendientes |
| 5 — Primer renderizado | Prototipo sintético demostrado; el criterio exige una imagen del juego y sigue abierto |
| 6 — Shaders/materiales | Pendiente de conversión y pruebas con contenido real |
| 7 — Audio/controles | Backend disponible; integración de partida pendiente |
| 8 — Guardado/versión jugable | Pendiente |
| 9 — Rendimiento | Pendiente de una escena representativa |
| 10 — Producto final | Pendiente |

M0 está demostrado. M1 cuenta con pruebas importantes de ABI y ejecución.
M2 tiene un arranque diagnóstico real. M3 —primer cuadro del juego— todavía
no se ha alcanzado. No existe evidencia para asignar un porcentaje global fiable.

## Próximos pasos ordenados por impacto

1. **Continuar el arranque por evidencia.** Implementar correctamente
   sceSystemServiceParamGetInt para los parámetros que solicite esta edición,
   volver a registrar el recorrido y atender el siguiente bloqueo real.
2. **Auditar adaptadores críticos provisionales.** Priorizar hilos reales,
   carga de módulos y las rutas que aparentan éxito sin servicio. Clasificar
   cada adaptador como funcional, parcial, provisional o pendiente.
3. **Cerrar las pruebas de contexto.** Probar RSP dentro de los límites de pila
   esperados y TLS desde código invitado, con dos hilos/fibras cuando corresponda.
4. **Alcanzar y capturar el primer envío gráfico real.** Conservar sus paquetes,
   shaders y recursos con identidad de versión; no ampliar PM4 por conjeturas.
5. **Usar esa captura para el siguiente tramo de GPU.** Implementar los opcodes
   necesarios y la conversión de shaders/recursos hasta reproducir el primer
   dibujo del juego.
6. **Integrar menú y escena controlable.** Conectar presentación, audio y entrada
   reales; después demostrar guardado/cierre/reinicio.
7. **Medir rendimiento.** Evaluar los objetivos de fluidez y consumo sobre la
   escena, no sobre el tiempo de arranque ni el triángulo sintético.

El progreso de CPU y de GPU puede seguir desarrollándose por componentes, pero
el próximo hito de producto debe unirlos: lógica del juego -> envío auténtico ->
recursos/pipeline -> primera imagen. Contar nuevas pruebas o nuevos NIDs sin
alcanzar ese recorrido no cerraría la distancia principal.

## Referencias locales

- [Plan inicial](PLAN_PORT_NATIVO_PC.md).
- [Decisión CPU](DECISION_CPU_EJECUCION.md).
- [Historial](worklog.md).
- [Alcance PM4 y pruebas](LOTE_2_PM4_VULKAN.md).
- [Registro de adaptadores](../engine/src/registry/adapter_registry.cpp).
- [Fibras](../engine/src/fiber/fiber.cpp).
- [Lanzador](../launcher.cpp).
- [Nueva traza de arranque](../out/release/contrast_boot.log).

No se modificó código funcional durante esta revisión.

