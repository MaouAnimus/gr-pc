# Estructura del proyecto

Runtime HLE experimental para Windows x86-64 y Gravity Rush Remastered (CUSA01130). Estado: 23 de octubre de 2026. La inspección está disponible; ejecutar el juego sigue pendiente.

| Ruta | Contenido |
| --- | --- |
| CMakeLists.txt | Biblioteca, inspector, diagnóstico y 14 suites |
| launcher.cpp | Inspector con --strict y --headless |
| engine/src/memory/ | Reserva y protecciones de memoria |
| engine/src/kernel/, engine/src/fiber/ | Sincronización y fibras Windows |
| engine/src/loader/ | SELF/ELF y relocalizaciones |
| engine/src/fs/ | PSARC y VFS de lectura |
| engine/src/pad/, engine/src/audio/ | Controles y WASAPI |
| engine/src/gnm/, engine/src/video.cpp | Estado GNM y presentación Vulkan; geometría pendiente |
| engine/include/ | Contratos host |
| tests/runtime_tests.cpp | Integración de nueve subsistemas |
| tests/stabilization_tests.cpp | Cinco suites sintéticas de regresión |
| tools/ | Compilación, inspector y utilidades de auditoría |
| out/debug/, out/release/ | Binarios y registros CTest |
| docs/ | Especificaciones históricas e informe de estabilización |
| CUSA01130/ | Volcado original local, excluido de Git |

Las hojas de desarrollo son propuestas de trabajo. No certifican fases terminadas al 100 %. Faltan compatibilidad binaria Orbis, TLS, dependencias, arranque, shaders, geometría y reproducción del contenido del juego. El archivo de configuración todavía no se consume. No se ha creado una distribución portable ni particiones persistentes user/.

Ver [README](README.md) y [estabilización](docs/ESTABILIZACION.md).
