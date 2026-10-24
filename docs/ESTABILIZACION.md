# Estabilización del runtime

Fecha: 23 de octubre de 2026.

## Resultado

El proyecto se compila con Clang en Debug y Release. Las 37 suites CTest pasan
en ambas configuraciones, sin omisiones en esta validación. El arranque del
juego no se pudo repetir porque `CUSA01130/eboot.bin` no está en este checkout.

## Correcciones

- El cargador obtiene tamaños, direcciones y tablas desde SELF/ELF y metadatos
  dinámicos Orbis; elimina los valores específicos del volcado usados antes.
- Valida arquitectura, clase ELF, tamaño de encabezados, límites sin desbordar,
  permisos, alineación, superposición, memoria disponible y entrada ejecutable.
- Rechaza segmentos truncados, formatos no soportados y sobrescrituras de
  páginas ya comprometidas. Revierte únicamente las páginas adquiridas por una
  carga fallida, manteniendo las reservas y páginas previas del llamador.
- Procesa RELA y PLT. Valida todos los registros y cadenas antes de escribir.
  Las importaciones desconocidas y las funciones libc incompletas ya no se
  resuelven hacia una función que devuelve éxito.
- La carga estricta rechaza importaciones pendientes. La inspección debe
  solicitarse explícitamente mediante el argumento de API; deja posiciones
  pendientes a cero y enumera los nombres, sin transferir ejecución.
- Corrige el adaptador de seis argumentos System V a Win64: atributo de
  convención y orden de argumentos.
- Corrige el bloqueo al abrir archivos antes de inicializar el VFS.
  Restringe /app0 a lectura y rechaza rutas fuera de la raíz, incluidas
  rutas con .., unidades Windows y escapes por resolución canónica.
  Las consultas de archivo y directorio no implementadas devuelven error.
- Vídeo deja de convertir fallos de SDL/Vulkan en inicializaciones exitosas
  sin gráficos. El modo sin gráficos es explícito y se prueba por separado.
  Se comprueban creación de recursos, adquisición, envío y presentación;
  se cuentan las presentaciones y se detiene el recorrido tras un error.
- Audio utiliza el formato PCM solicitado y conversión WASAPI al dispositivo.
  Valida formato, manejadores, hilo propietario y bloques completos; devuelve
  busy sin consumir datos cuando no caben. NGS2 devuelve unsupported.
- El inspector comprueba inicializaciones y lecturas, libera subsistemas y
  comunica que no ejecuta el juego. Los scripts funcionan desde otra carpeta;
  -Fresh regenera la configuración si el proyecto se mueve.
- `sceSystemServiceParamGetInt` está registrado por NID para
  `libSceSystemService`. Solo implementa `Lang` (ID 1), devolviendo el valor 20
  indicado en la evidencia de ejecución adjunta; puntero nulo o parámetros no
  soportados devuelven `ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER`.
- README y estructura sustituyen las afirmaciones contradictorias de fases
  terminadas por el alcance comprobado.

## Verificación

- Debug: 37/37 suites; Release: 37/37 suites.
- El test del adaptador comprueba el valor de idioma, el registro, el puntero
  nulo y un ID no soportado; no se pudo validar el siguiente bloqueo del juego
  al no estar disponible `eboot.bin` en este checkout.
- Cinco suites nuevas: cargador, relocalizaciones, VFS, ABI y modo sin gráficos.
- Casos sintéticos: archivos truncados, offsets desbordados, tablas inválidas,
  segmentos superpuestos, permisos incompatibles, entrada fuera de código,
  reversión tras fallo, preservación de páginas previas y recarga posterior.
- Relocalizaciones: registros no soportados, índice de símbolo inválido,
  cadena sin terminador, destino fuera de rango, tabla PLT, importación
  pendiente y ausencia de escrituras parciales.
- VFS: apertura automática, lectura, rutas de escape y rechazo de escritura.
- Audio: apertura real a 48 kHz, consultas, escritura de silencio, rechazo de
  bloques incompletos/excesivos y cierre. No se verifica sonido perceptual.
- Vídeo: presentación Vulkan real de un cuadro de limpieza, comprobada con
  contador. La prueba no certifica shaders ni geometría del juego.
- La prueba de controles admite estados válidos del teclado real; ya no
  presupone que el usuario no está pulsando WASD durante la ejecución.
- Inspector Release desde otra carpeta: código 0 en inspección, código 1 en
  --strict. Registros en out/release/launcher_inspection.log y launcher_strict.log.

El diagnóstico del volcado local informa 303.268 posiciones procesadas y 669
nombres importados sin resolver. La cifra anterior de 302.608 omitía 660
posiciones de la tabla PLT. El contador incluye posiciones pendientes que
quedan a cero en inspección; no certifica resolución ni compatibilidad.

## Límites pendientes

El modo de arranque instrumentado existe, pero no se verificó la ejecución del
juego en esta validación. Tampoco se implementaron TLS completo, carga de
dependencias PRX, compatibilidad completa de exports, traducción de shaders/PM4,
dibujo de geometría, NGS2 ni persistencia de partidas. El backend gráfico
continúa siendo una base de presentación y estado. `gravity_settings.ini`
todavía no se consume. La reserva fija puede fallar si su dirección ya está
ocupada; el error se comunica sin sobrescribir memoria existente.

Estas correcciones estabilizan los recorridos probados; no convierten el
proyecto en un port jugable. El historial incremental y la siguiente
dependencia están en [worklog.md](worklog.md).
