# Caché de shaders SPIR-V

`gravity::hle::gnm::ShaderCache` conserva módulos compilados correctamente en
`.spv_cache`, o en el directorio recibido por su constructor. La ubicación de cada
entrada es `<clave SHA-256>.spv`. El archivo contiene una cabecera de 88 bytes y
el módulo SPIR-V original; para consumir el módulo se usa `load`, no se entrega
el archivo completo a Vulkan.

## Uso

```cpp
ShaderCache cache;
// Las opciones deben ser estables y contener todos los datos que afectan al
// resultado: versión del traductor/compilador, stage, entorno Vulkan, flags e
// interfaz completa de recursos y atributos.
const auto key = ShaderCache::key_for(gcn_bytes, canonical_options);
auto words = cache.load(key);
if (!words) {
    words = compile_and_validate_for_target(gcn_bytes, options);
    if (words) (void)cache.store(key, *words);
}
```

`key_for` acepta `span<const byte>` o `span<const uint8_t>`. La clave SHA-256
incluye el dominio y versión del formato de clave, longitudes explícitas, todos
los bytes GCN y todas las opciones, incluidos sus posibles bytes NUL. Una clave
vacía indica que falló el proveedor criptográfico y no se acepta para guardar.
`load` devuelve `nullopt` ante ausencia, corrupción o error de acceso; `store`
devuelve `false` sin propagar excepciones. Los errores de disco no necesitan
impedir la compilación y ejecución del shader.

## Integridad y concurrencia

- La cabecera verifica firma, formato, tamaño exacto, clave incrustada y checksum
  SHA-256 del módulo. Se rechazan truncamientos, datos sobrantes y entradas
  copiadas bajo otra clave.
- Cada módulo se limita a 64 MiB. Se comprueban magic SPIR-V, versión 1.0–1.6,
  bound de IDs entre 1 y `0x3fffff`, schema cero y longitudes de instrucciones.
- Cada escritor crea un archivo temporal exclusivo con nombre aleatorio dentro
  del mismo directorio, escribe todo el contenido, lo vacía y publica mediante
  reemplazo atómico. Los lectores admiten sustitución mientras tienen abierto
  el archivo, de modo que observan la versión anterior o la nueva completa.
- Los errores de escritura o publicación eliminan el temporal creado por esa
  operación. Los procesos no comparten estado ni requieren un mutex global.

## Límites

Esta comprobación de estructura no sustituye a SPIRV-Tools: el llamador debe
validar capacidades, IDs, tipos, instrucciones y reglas Vulkan del módulo antes
de guardarlo, y validar un hit antes de usarlo si el directorio puede ser
modificado por terceros. Un SHA-256 sin secreto detecta corrupción, pero no
autentica al autor de un archivo frente a alguien que puede recalcularlo.

El llamador también es responsable de la serialización canónica de opciones y
de incluir toda la interfaz de compilación. Cambiar un dato omitido de las
opciones no puede invalidar automáticamente una entrada. La caché no tiene
evicción ni límite de tamaño total. Un cierre abrupto del proceso puede dejar
temporales que no se leen como entradas; no se garantiza persistencia frente a
pérdida de alimentación ni semántica de reemplazo en sistemas de archivos de
red. Se utiliza la semántica de archivos locales de Windows.

## Pruebas

`shader_cache_tests` admite las suites `roundtrip`, `miss`, `identity`,
`corruption`, `truncation`, `concurrent`, `invalid` y `failures`, o `all`. Cada
suite usa un directorio temporal único y lo elimina al terminar. Cubren
persistencia entre instancias, invalidación de clave por bytecode/opciones,
alteraciones en cada campo, todos los puntos de truncamiento de una entrada,
escritores independientes simultáneos con lectores activos, límites de módulo
y limpieza de temporales tras errores de publicación.
