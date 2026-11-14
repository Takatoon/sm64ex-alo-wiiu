# Estado de las optimizaciones de Wii U

Rama: `feature/wiiu-geometry-optimization`.
Base: `takatoon/feature/wiiu-minimap`, commit `bc4f6118`, verificado en GitHub
antes de crear la rama. `main` no se modifica.

## Cambios aplicados

| Commit | Cambio | Resultado / estado |
| --- | --- | --- |
| `80107af9` | Instrumentación de geometría, tiempos por fase, prueba A/B y prevención del sueño durante la prueba. | Herramientas de diagnóstico; no se contabilizan como una mejora de rendimiento. |
| `7bbcffd2` | Preparar proporción de pantalla, indicadores y luces modificadas una vez por carga de vértices. | Geometría equivalente. Sin medición aislada de FPS para este cambio. |
| `e903d4f6` | Reutilizar la preparación del material entre comandos de vértices y triángulos. | Evita consultas repetidas de shader, texturas y estado. Invalidación conservadora al cambiar otros comandos. Sin resultado aislado de FPS. |
| `f15f6c7d` | Escribir atributos directamente en el búfer de GX2, evitando la copia intermedia. | Mejora confirmada en una vista inicial: dos bloques de copia a 46,4 FPS frente a dos directos a 59,4 FPS. La preparación baja aproximadamente un 4,4 %. No garantiza 60 FPS en todas las vistas. |
| `16beeed5` | Preparar constantes del material una sola vez; conservar valores dinámicos por vértice y redondeo del LOD. | Ahorro de aproximadamente 0,07–0,10 ms por imagen, un 1–1,3 %. En la vista lenta ambos métodos siguen a 40 FPS. |
| `fd0d2f97` | Formato experimental RGBA8 para colores y niebla, con lectores de atributos independientes para float y compacto. | Primera prueba: ~1 ms / 14 % menos preparación y ~25 % menos datos. En una pareja completa de la vista lenta, ~40 → 55 FPS. El usuario no aprecia cambios de color; faltan comprobaciones visuales más amplias. |

Los resultados proceden de comparaciones en la misma cámara con Galaxy,
1920×1080 y registros activados. Son resultados de esas escenas, no una promesa
para todos los modelos o niveles. El contador puede rondar 59,4 FPS por el coste
de los registros incluso al estar cerca del límite de pantalla.

La corrección de `kill_enable` ya está en la base publicada; se conserva y no
se presenta como una optimización nueva de esta rama.

## Prueba actual: selector manual de color

- Registros: `revision=9`, `control=manual`.
- El HUD de TV muestra **F** para Float o **R** para RGBA8, debajo del lugar del contador de FPS. Se muestra aunque el contador esté desactivado.
- Una pulsación corta de **−** cambia de formato al soltar el botón. El cambio se aplica al inicio del siguiente par de imágenes, sin alterar una imagen pendiente.
- Empieza en **R**. La selección se conserva al moverse y cambiar de escena; no hay temporizador ni espera de 90 segundos para comparar el aspecto.
- **− + izquierda/derecha** conserva el atajo de modelos de DynOS y no cambia el formato de color.
- La alternancia automática de las pruebas anteriores queda desactivada por defecto; puede recuperarse al compilar con `WIIU_GEOMETRY_AUTO_AB` definido.
- En ambos casos se conservan la escritura directa y las constantes preparadas.
- Los tiempos del perfilado se separan al cambiar manualmente de formato. Una comparación de rendimiento manual requiere mantener la misma vista; para buscar fallos visuales se puede jugar y mover la cámara libremente.
- Mario permanece despierto en la compilación de pruebas.
- Posiciones y UV siguen en float. Cada campo de color pasa de 16 a 4 bytes;
  la reducción del vértice completo depende de cuántos atributos use el shader.
- La GPU recibe bytes RGBA y los normaliza mediante UNORM. El orden de canales,
  las transparencias, la niebla y la representación en pantalla deben
  comprobarse en la Wii U antes de considerar consolidado el cambio.

El modo compacto se selecciona automáticamente sólo en la compilación con
perfilado. Sin perfilado queda el formato float por defecto. No se han eliminado
las esperas de finalización ni la publicación de datos antes de dibujar.

WUHB de pruebas: `sm64-galaxy-phases-20261114.wuhb`, mismo nombre y título que
las compilaciones anteriores para sustituirlo desde el mismo icono.

## Experimentos descartados

| Ensayo | Motivo |
| --- | --- |
| Puntero local para rellenar cada triángulo. | Sólo 0,025–0,030 ms de diferencia, menos del 0,4 %, sin mejora práctica de FPS. No está en las rutas activas de esta rama. |
| Reutilizar coordenadas proyectadas para descartar caras. | La comparación con fast-math detectó diferencias en el descarte. No se conserva. |

## Próximas mejoras posibles

| Prioridad | Trabajo | Qué medir o comprobar antes de aplicarlo |
| --- | --- | --- |
| 1 | Completar la prueba RGBA8. | Apariencia correcta y cuatro bloques completos en una vista ligera y otra lenta; comparar bytes, tiempo total de preparación y FPS. |
| 2 | Caché del vértice ya preparado para varios triángulos del mismo material. | Contar reutilizaciones reales y fallos de caché antes de implementarla. Invalidar al recargar el vértice o cambiar material; tratar aparte atributos que dependen del triángulo, como el LOD. |
| 3 | Geometría indexada en GX2. | Cuántos atributos duplicados se evitarían y coste de producir los índices. Mantener orden, transparencia y límites de los lotes. Puede ser especialmente útil para modelos densos. |
| 4 | Medir el coste gráfico del castillo por material/capa. | Usar mediciones de GPU; la duración de `GX2DrawDone` es una espera de la CPU, no una medida aislada del trabajo de GPU. Separar coste de escena y del modelo. |
| 5 | Revisar el relleno y la alineación del formato de vértice. | Verificar si reducir bytes de UV compensa: el relleno existente es deliberado y quitarlo puede perjudicar las lecturas de la GPU. |
| 6 | Transformación e iluminación de vértices más eficientes. | Perfilar sus operaciones y contrastar el código generado por el compilador de PowerPC. Validar redondeo, luces, niebla y recorte. |
| 7 | Solapar más trabajo de CPU y GPU con varios búferes protegidos por señales de finalización. | Diseño más delicado: garantizar la vida de recursos y comprobar latencia de entrada. No retirar simplemente `GX2DrawDone`. |
| 8 | Preparar una compilación de uso normal. | Retirar la alternancia y el ruido del perfilado; conservar sólo mejoras verificadas. Repetir pruebas con Galaxy, Mario original y después Render96. |

## Verificación reproducible

En una copia normal del repositorio, crear la referencia desde el commit de
instrumentación inicial y ejecutar los tests en el entorno de desarrollo:

```sh
git show 80107af9:src/pc/gfx/gfx_pc.c > /tmp/gfx_reference.c
python3 tools/tests/check_wiiu_geometry.py --reference /tmp/gfx_reference.c --writer packed --fast-math
python3 tools/tests/check_wiiu_geometry.py --reference /tmp/gfx_reference.c --writer packed --sanitize
python3 tools/tests/check_wiiu_packed_colors.py
python3 tools/tests/check_wiiu_geometry_profile.py
python3 tools/tests/check_wiiu_geometry_ab.py
```

El test de geometría decodifica los colores compactos a sus valores normalizados
antes de compararlos con la referencia: el formato binario intencionadamente
cambia. Los tests no ejecutan la GPU real. Se conservan los datos de rendimiento
y las limitaciones en [el registro de experimentos](wiiu-geometry-profiling.md).
