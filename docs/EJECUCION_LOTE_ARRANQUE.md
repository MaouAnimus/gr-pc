# Ejecución del plan: lote de arranque

23 de octubre de 2026. Estado del producto: experimental, todavía no jugable.

## Resultado comprobado

Se superó la parada inicial en `sceSystemServiceParamGetInt` y se corrigieron
los siguientes problemas encontrados durante el arranque. Debug y Release
terminan ahora mediante diagnóstico en `sceFiosInitialize`, biblioteca
`libSceFios2`, NID `wAKZ-det+yo`, llamado desde `eboot.bin+0x4FDC`.

Las pruebas pasaron de 43 a **68 suites**, aprobadas en ambas configuraciones,
incluidas las pruebas Vulkan por lectura de píxeles. Las importaciones sin
resolver pasaron de 543 a 509; esa diferencia no mide el porcentaje del producto.

La CPU continúa ejecutando código x64 directamente. Las correcciones TLS son
instrucciones nativas en la imagen cargada; no se incorporó un emulador externo
ni se modificaron los archivos de `CUSA01130/`.

## Cambios implementados

| Área | Cambio y comprobación | Límite actual |
| --- | --- | --- |
| Configuración del sistema | Idioma, formatos, zona horaria, verano y perfil local; errores y punteros inválidos probados | No implementa todos los servicios de la consola |
| Importaciones | Resolución por biblioteca/NID usando los metadatos dinámicos; trampas guardan los seis argumentos enteros SysV | Las firmas pendientes siguen pendientes |
| Hilos | Creación y callbacks SysV reales en hilos Windows, join, atributos y TLS individual | Prioridades traducidas por niveles; cancelación y todos los atributos aún pendientes |
| Mutex | Almacenamiento de 8 bytes, atributos, recursión, propiedad, exclusión y espera limitada | Variables de condición todavía pendientes |
| RW locks | Lectores concurrentes, escritor exclusivo, propiedad y ciclo de vida | Otros modos y garantías avanzadas de planificación pendientes |
| Semáforos | Reserva atómica de varias unidades, espera, timeout, cancelación, límites y liberación | No representa una certificación de todos los casos del kernel |
| Equeue | Creación, espera, entrega desde productores nativos y eliminación | No se fabrican eventos de flip/GPU; faltan sus productores y filtros invitados |
| Memoria directa | Pool acotado a los 4 GiB anunciados, offsets, alineación, mapeo y liberación reales | Mapeos completos; alias, subrangos y recursos GPU pendientes |
| Archivos | Open/read/close del kernel conectados al VFS de lectura y errores comprobados | No implementa FIOS asíncrono, directorios, guardados ni el ABI completo de stat |
| Servicios provisionales | Módulos y vídeo dejan de devolver éxito sin realizar el servicio; buffer attributes escribe sus 40 bytes | Cargador PRX y enlace de buffers/presentación invitados pendientes |

Se observaron callbacks reales de `FontTextureDeleteThread`, `BackgroundFiber`,
`BackgroundFiber::Precedence` y `FIOS2Driver::UserCallback`. Crear esos hilos no
demuestra que sus sistemas estén completos ni que ya ejecuten fibras correctamente.

## TLS y pilas

El ejecutable utiliza TLS mediante `FS:0`. Mantener una base FS con FSGSBASE
falló en pruebas tras llamadas a Windows, por lo que esa implementación se
descartó. El mecanismo final utiliza una ranura TLS de Windows y reemplaza
2.752 cargas `FS:0` por cargas nativas `GS:[ranura]`, de igual longitud.

El perfil está ligado al SHA256
`7c0216e8ff7254d03141e98c2df2b69f4f2259d08f2fe2754c33c5a3f137033a`.
Se comprueban el archivo completo, los bytes de cada instrucción y la ubicación
de la ranura en Windows antes de aplicar cambios. Otra versión se rechaza y
revierte su carga. La plantilla PT_TLS tiene 4 bytes iniciales y 320 bytes totales.
Las pruebas verifican acceso nativo, aislamiento entre hilos y conservación tras
llamadas a Windows, además del rechazo de una identidad modificada.

El perfil procede de un barrido lineal LLVM del código hasta la tabla PLT
contigua, excluyendo los datos posteriores del segmento. No constituye una
prueba completa de flujo de control ni cubre módulos PRX adicionales.

También se corrigió el salto sobre páginas de guarda: el código ELF puede
reservar un marco grande sin los sondeos `__chkstk` de Windows. Se prepara la
pila nativa antes de entrar al invitado, conservando una guarda inferior. La
regresión ejecuta un marco de 96 KiB sin sondeos y comprueba el resultado.
Sigue pendiente ejecutar las fibras sobre las pilas suministradas por el juego.

## Diagnóstico y evidencia

`tools/boot_trace.py` crea un directorio por intento en `out/evidence/` con
stdout, stderr, resultado, versión/SFO, hashes del juego y del ejecutable,
configuración, revisión Git, diferencias y hashes de fuentes. Distingue retorno,
timeout y función pendiente. Un fallo de arranque ya no devuelve éxito del proceso.

El harness termina su proceso después del informe: los hilos invitados pueden
seguir vivos y no es seguro liberar su código mientras ejecutan. El timeout
del launcher también termina el proceso completo. Esto es aislamiento de
diagnóstico; el cierre cooperativo del producto final continúa pendiente.

REA no estaba conectado y su diagnóstico señaló una configuración obsoleta del
proveedor. Se continuó con LLVM y el depurador local de Windows, sin cambiar la
configuración de herramientas ni instalar motores. Esas observaciones son
evidencia local, no Evidence de REA.

Registros finales:

- Release: `out/evidence/20261023T113036.193212Z_validated_batch_release/`.
- Debug: `out/evidence/20261023T113039.713211Z_validated_batch_debug/`.
- Pruebas: `out/evidence/validation_release.log`, `validation_debug.log` y
  [resumen versionado](evidence/execution_plan_validation.json).
- [Perfil TLS](evidence/tls_fixup_profile.json).

Los ensayos intermedios conservan fallos encontrados y reparados; no deben
confundirse con los dos resultados finales. Un ensayo simultáneo también
expuso una escritura nula con el antiguo adaptador de memoria. Tras sustituirlo,
los dos ensayos finales se realizaron secuencialmente para no duplicar la
reserva de varios GiB del juego.

## Inventario corregido

El inventario anterior contaba exportaciones sin nombre como símbolos
identificados (`Export_...`). Se eliminó esa equivalencia, se identificaron
nombres FIOS por su NID y se corrigió la codificación del carácter `-`.
El catálogo regenerado contiene **539 nombres identificados y 142 pendientes**
de 681 importaciones. Identificar un nombre no verifica su firma ni implementa
su servicio. Los informes de dependencias e incertidumbres se regeneraron.

## Dependencias para continuar

1. Contrastar e implementar el contrato real de `sceFiosInitialize`, su estructura
   de parámetros, callbacks y servicios de archivos asíncronos. La declaración
   pública `void()` identifica el nombre, pero no describe ese contrato.
2. Completar carga de módulos, variables de condición, ciclo de vida de hilos
   y pilas de fibras según las rutas que alcance el juego.
3. Alcanzar y conservar el primer DCB/IB/CCB auténtico. Aún no se alcanzó un
   envío PM4 del juego ni se dispone de un replay de ese envío.
4. Convertir shaders/recursos y conectar presentación, entrada, audio y guardado;
   después medir rendimiento y validar contenido.

No se han completado los hitos de primera imagen, menú, partida ni rendimiento.

## Contratos y referencias

- [OpenOrbis: parámetros del sistema](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/_types/sys_service.h)
  y [libkernel](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/libkernel.h).
- [FreeBSD: tipos de mutex](https://github.com/freebsd/freebsd-src/blob/main/include/pthread.h).
- [Microsoft: TEB y TLS](https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-teb)
  y [pila de hilos](https://learn.microsoft.com/en-us/windows/win32/procthread/thread-stack-size).
- [OpenOrbis: Fios2](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/Fios2.h).

Las referencias sirven para contrastar contratos; el backend implementado es
el del proyecto. Las decisiones inferidas del binario local se describen como
tales y permanecen ligadas a su identidad.
