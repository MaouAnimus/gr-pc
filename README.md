# Gravity Rush — runtime experimental

Runtime C++23 para Windows x86-64. Carga e inspecciona el volcado local de Gravity Rush Remastered (CUSA01130). **Dispone de arranque instrumentado, pero todavía no es jugable.**

## Compilar y comprobar

Requiere Windows 10 1803 o posterior, MSYS2 Clang64 con CMake, Ninja, zlib, SDL3, Vulkan, SPIRV-Tools, glslc (para las pruebas) y RTK. El toolchain predeterminado es C:\msys64\clang64\bin; se cambia con -ToolchainBin.

```powershell
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_runtime.ps1 -Configuration Debug
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_runtime.ps1 -Configuration Release
```

Añadir -Fresh permite regenerar la configuración después de mover la carpeta. Los resultados quedan en out/debug/ y out/release/. El script compila, ejecuta 68 suites CTest y un diagnóstico de memoria/fibras y del ejecutable, si está disponible. Las pruebas de hardware se marcan como omitidas si no hay dispositivo; eso no certifica reproducción ni presentación.

## Inspección

Jugar_GravityRush.bat abre el inspector; su nombre se conserva por comodidad. No inicia una partida.

```powershell
rtk proxy powershell -NoProfile -File tools/run_launcher.ps1 -Headless
rtk proxy powershell -NoProfile -File tools/run_launcher.ps1 -Headless -Strict
```

La inspección enumera importaciones pendientes y deja sus posiciones a cero. El modo estricto rechaza esas importaciones con código 1 y revierte la carga. La inspección no salta al punto de entrada. El modo --boot de la Prioridad 1 permite un arranque instrumentado; sigue sin ser una partida jugable.

## Estado

| Componente | Alcance comprobable |
| --- | --- |
| Memoria, sincronización y fibras | Backend Windows con pruebas de comportamiento |
| SELF/ELF | Encabezados y segmentos validados; SELF sin compresión y ELF64 x86-64 relocatable |
| Relocalizaciones | Metadatos dinámicos Orbis, tablas RELA y PLT, validación antes de escribir |
| Archivos | PSARC/zlib y VFS de lectura bajo /app0; apertura automática sin bloqueo |
| Controles | Adaptador host con emulación de movimiento |
| Audio | PCM mono/estéreo de 16/32 bits mediante WASAPI; uso en el hilo de apertura |
| Gráficos | Decodificador PM4 y cola hacia Vulkan; dibujos sintéticos verificados mediante lectura de píxeles |
| NGS2, shaders y geometría del juego | Pendientes; no hay todavía un proveedor de pipelines del juego |

Los objetos, firmas y errores host no certifican compatibilidad binaria Orbis. La Prioridad 1 incorporó runner, matriz ABI, pruebas de fibras/TLS y registro de adaptadores. La compatibilidad completa y las dependencias PRX siguen pendientes. gravity_settings.ini todavía no se lee.

El diagnóstico actual procesa **303.268 posiciones y registra 509 nombres sin resolver** con el registro de adaptadores inicializado. El informe histórico de estabilización registraba 669. El resultado anterior de 302.608 omitía la tabla PLT. Procesar una posición no significa que su importación esté implementada.

El [lote de ejecución del plan](docs/EJECUCION_LOTE_ARRANQUE.md) implementa servicios de arranque, hilos y sincronización reales, memoria alineada, TLS nativo y preparación de pilas. Los arranques Debug y Release superan la consulta de idioma y se detienen en `sceFiosInitialize`. No se ha obtenido una imagen del juego.

Para conservar un intento con sus hashes y diagnóstico:

```powershell
rtk proxy python tools/boot_trace.py --configuration Release --label arranque
```

Devuelve 1 si el arranque no retorna correctamente, aunque haya guardado la evidencia. El launcher devuelve 3 por importación pendiente, 4 por timeout y 5 por fallo fatal en un hilo invitado.

CUSA01130/ permanece intacto y excluido de Git. Las pruebas de integración lo usan si existe; las regresiones sintéticas no lo necesitan.

El objetivo es un port Windows propio, con lógica x64 y subsistemas PC, sin un emulador externo como producto final. La preparación previa y las optimizaciones se validarán mediante medidas.

Ver el [plan completo del port nativo](docs/PLAN_PORT_NATIVO_PC.md), la [estabilización](docs/ESTABILIZACION.md) y la [estructura](ESTRUCTURA_PROYECTO.md).

El [informe del decodificador PM4 y la cola Vulkan](docs/LOTE_2_PM4_VULKAN.md) detalla el subconjunto soportado, las pruebas y los límites actuales.

El [conector AvPlayer con Media Foundation](docs/AVPLAYER_MEDIA_FOUNDATION.md) decodifica H.264 y PCM, entrega vídeo temporizado a texturas Vulkan y documenta las pruebas con MP4 reales del juego a 1080p60 y los límites de integración pendientes.

La [base de físicas y gravedad 360°](docs/PHYSICS_GRAVITY_60HZ.md) corrige relojes y sincronización guest e incorpora paso fijo de 60 Hz y CCD de esfera contra triángulos. Su conexión al mundo Havok del juego sigue pendiente y se distingue de las pruebas nativas.

Shaders: incorporados el decodificador Sea Islands, un subconjunto GCN→SPIR-V y la [caché en disco](docs/SHADER_DISK_CACHE.md). Las [correcciones de la revisión](docs/SHADER_REVIEW_FIXES.md) validan módulos, rechazan instrucciones sin soporte y preservan geometría por dibujo. El enlace completo de recursos gráficos del juego sigue pendiente; todavía no se certifica el renderizado de Kat ni Hekseville.
