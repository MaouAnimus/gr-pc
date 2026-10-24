# AvPlayer: Media Foundation y texturas Vulkan

Implementación y comprobaciones del 24 de octubre de 2026.

`libSceAvPlayer` utiliza el Source Reader de Windows para decodificar vídeo
H.264 y audio a PCM de 16 bits. Los archivos se abren por el VFS, incluidos
los MP4 de los PSARC, mediante un `IStream` de lectura y búsqueda. Cada
reproductor mantiene sus objetos COM en un hilo MTA propio hasta su cierre.
Se solicitan transformaciones de hardware cuando están disponibles; estas
pruebas no identifican qué decodificador de hardware selecciona Windows.

Las consultas de vídeo/audio son no bloqueantes, con una petición anticipada
por flujo. Los timestamps del contrato HLE existente están en microsegundos;
el reloj monotónico decide cuándo entregar cada muestra. Pausa/reanudación,
reinicio, repetición y fin de ambos flujos conservan ese reloj. Los metadatos
proceden del archivo. Una fuente inexistente o incompatible devuelve error
y conserva la fuente anterior; no se generan fotogramas negros ni silencio
como sustitutos de una decodificación fallida.

El vídeo RGB32 de Windows se convierte de BGRX a RGBA opaco, respetando el
stride y la orientación. El buffer expuesto al invitado tiene alineación de
256 bytes y permanece válido hasta la próxima entrega de vídeo o el cierre.
El renderizador recibe una copia inmutable del fotograma. Antes de dibujar,
tras esperar el fence de la ranura correspondiente, copia el vídeo mediante
un staging buffer persistente a una imagen Vulkan de tiling óptimo. Cada
ranura mantiene su propia imagen y descriptor. Las barreras realizan las
transiciones de transferencia a muestreo; el resolver de GNM reconoce la
dirección del vídeo y evita la caché estática de texturas.

También se corrigió la lectura PSARC de bloques finales sin comprimir:
pueden almacenar su longitud en la tabla, además del cero usado por bloques
completos. Sin esa corrección los MP4 locales no se podían abrir por el VFS.

## Evidencia

- Release: 102 de 102 suites CTest aprobadas, sin omisiones. Debug:
  aprobadas las suites AvPlayer, PM4/Vulkan, VideoOut y la regresión PSARC.
- H.264/AAC sintético generado por Windows: colores alternantes, orientación,
  RGBA, PCM no silencioso, timestamps, pausa, reinicio, repetición, EOF,
  punteros inválidos y conservación de fuente tras error.
- Regresión PSARC independiente del volcado: un MP4 generado se empaqueta
  con manifest y bloque final sin comprimir y se reproduce desde `/app0`.
- Vídeo sintético de 1920×1080 a 60 Hz: 120 fotogramas decodificados a RGBA
  en 0,983 s, aproximadamente 122 fps en Release en este equipo.
- Muestreo Vulkan en RTX 3050: 120 texturas de 1920×1080, tres ranuras y
  lectura de píxeles en un destino de 64×64. Se verifican ambos colores en
  cada fotograma. La ejecución medida tardó 2,053 s, incluidos arranque,
  entrega temporizada y lectura de GPU. No mide scanout ni presentación en
  una ventana. La capa Khronos no estaba instalada.
- Se localizaron 66 candidatos MP4 en los manifests locales. Se decodificaron
  120 fotogramas reales de `/app0/mp4/ep00-01c.mp4`, 1920×1080 a 60 Hz,
  en 0,952 s (126,1 fps, sólo decodificación, carga inicial excluida).

Los tiempos dependen del equipo y de la configuración. Debug tiene un coste
alto en la conversión de píxeles y no alcanza tiempo real. La prueba de
rendimiento comprueba fotogramas y formato; informa la velocidad sin imponer
un umbral universal de 60 fps.

## Reproducir las comprobaciones

Tras compilar con `tools/build_runtime.ps1`, desde la raíz:

```powershell
rtk proxy C:/msys64/clang64/bin/ctest.exe --test-dir out/release -R '^avplayer_' --output-on-failure -V
```

La comprobación opcional del contenido local se ejecuta desde `out/release`
con el directorio del toolchain en PATH:

```powershell
rtk proxy ./avplayer_tests.exe content
```

El contenido del juego no se modifica ni se incluye en las pruebas sintéticas.

## Límites de integración

Se conserva el contrato HLE simplificado del proyecto. No se certifican los
layouts binarios Orbis ni los callbacks de memoria y textura del juego.
La prueba del MP4 real confirma lectura y decodificación; aún no demuestra
que el ejecutable del juego presente la intro completa ni que todos los
cómics animados utilicen AvPlayer. El audio decodificado se entrega al
consumidor HLE; no se añade una segunda salida de audio automática.

La transferencia hacia Vulkan pasa por memoria CPU; no hay interoperabilidad
sin copias entre superficies Direct3D y Vulkan. El VFS actual carga cada
entrada PSARC abierta completa en memoria. El reproductor selecciona el
primer flujo de vídeo y el primero de audio; selección de pistas alternativas,
subtítulos y cambios de formato de audio quedan fuera de este conector.
