# Gravity Rush — runtime experimental

Runtime C++23 para Windows x86-64. Carga e inspecciona el volcado local de Gravity Rush Remastered (CUSA01130). **Todavía no ejecuta el juego.**

## Compilar y comprobar

Requiere Windows 10 1803 o posterior, MSYS2 Clang64 con CMake, Ninja, zlib, SDL3, Vulkan y RTK. El toolchain predeterminado es C:\msys64\clang64\bin; se cambia con -ToolchainBin.

```powershell
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_runtime.ps1 -Configuration Debug
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_runtime.ps1 -Configuration Release
```

Añadir -Fresh permite regenerar la configuración después de mover la carpeta. Los resultados quedan en out/debug/ y out/release/. El script compila, ejecuta 14 suites CTest y un diagnóstico de memoria/fibras y del ejecutable, si está disponible. Las pruebas de hardware se marcan como omitidas si no hay dispositivo; eso no certifica reproducción ni presentación.

## Inspección

Jugar_GravityRush.bat abre el inspector; su nombre se conserva por comodidad. No inicia una partida.

```powershell
rtk proxy powershell -NoProfile -File tools/run_launcher.ps1 -Headless
rtk proxy powershell -NoProfile -File tools/run_launcher.ps1 -Headless -Strict
```

La inspección enumera importaciones pendientes y deja sus posiciones a cero. El modo estricto rechaza esas importaciones con código 1 y revierte la carga. Ningún modo salta al punto de entrada.

## Estado

| Componente | Alcance comprobable |
| --- | --- |
| Memoria, sincronización y fibras | Backend Windows con pruebas de comportamiento |
| SELF/ELF | Encabezados y segmentos validados; SELF sin compresión y ELF64 x86-64 relocatable |
| Relocalizaciones | Metadatos dinámicos Orbis, tablas RELA y PLT, validación antes de escribir |
| Archivos | PSARC/zlib y VFS de lectura bajo /app0; apertura automática sin bloqueo |
| Controles | Adaptador host con emulación de movimiento |
| Audio | PCM mono/estéreo de 16/32 bits mediante WASAPI; uso en el hilo de apertura |
| Gráficos | SDL3/Vulkan presenta un cuadro de limpieza; GNM mantiene estado y contadores |
| NGS2, shaders, geometría y arranque | Pendientes |

Los objetos, firmas y errores host no certifican compatibilidad binaria Orbis. La resolución de libc es parcial; faltan TLS, dependencias PRX y el entorno de inicio. gravity_settings.ini todavía no se lee.

El volcado local produce **303.268 posiciones procesadas y 669 nombres importados sin resolver**. El resultado anterior de 302.608 omitía la tabla PLT. Procesar una posición no significa que su importación esté implementada.

CUSA01130/ permanece intacto y excluido de Git. Las pruebas de integración lo usan si existe; las regresiones sintéticas no lo necesitan.

El objetivo es un port Windows propio, con lógica x64 y subsistemas PC, sin un emulador externo como producto final. La preparación previa y las optimizaciones se validarán mediante medidas.

Ver el [plan completo del port nativo](docs/PLAN_PORT_NATIVO_PC.md), la [estabilización](docs/ESTABILIZACION.md) y la [estructura](ESTRUCTURA_PROYECTO.md).
