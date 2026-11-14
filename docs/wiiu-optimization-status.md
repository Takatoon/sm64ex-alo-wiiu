# Estado de las optimizaciones de Wii U

Rama: `feature/wiiu-geometry-optimization`.
Base: `takatoon/feature/wiiu-minimap`, commit `bc4f6118`, verificado en GitHub
antes de crear la rama. `main` no se modifica.

## Cambios aplicados

| Commit | Cambio | Resultado / estado |
| --- | --- | --- |
| `80107af9` | Instrumentación de geometría, tiempos por fase, prueba A/B y prevención del sueño durante la prueba. | Herramientas de diagnóstico; no se contabilizan como una mejora de rendimiento. |
| `7bbcffd2` | Preparar proporción de pantalla, indicadores y luces modificadas una vez por carga de vértices. | Geometría equivalente; perfilado en consola junto con el resto de cambios. Falta una comparación controlada para atribuirle una ganancia individual. |
| `e903d4f6` | Reutilizar la preparación del material entre comandos de vértices y triángulos. | Evita consultas repetidas de shader, texturas y estado. Invalidación conservadora al cambiar otros comandos. Perfilado junto con los demás cambios; sin ganancia individual aislada. |
| `f15f6c7d` | Escribir atributos directamente en el búfer de GX2, evitando la copia intermedia. | Mejora confirmada en una vista inicial: dos bloques de copia a 46,4 FPS frente a dos directos a 59,4 FPS. La preparación baja aproximadamente un 4,4 %. No garantiza 60 FPS en todas las vistas. |
| `16beeed5` | Preparar constantes del material una sola vez; conservar valores dinámicos por vértice y redondeo del LOD. | Ahorro de aproximadamente 0,07–0,10 ms por imagen, un 1–1,3 %. En la vista lenta ambos métodos siguen a 40 FPS. |
| `fd0d2f97` | Formato experimental RGBA8 para colores y niebla, con lectores de atributos independientes para float y compacto. | Primera prueba: ~1 ms / 14 % menos preparación y ~25 % menos datos. En una pareja completa de la vista lenta, ~40 → 55 FPS. El usuario no aprecia cambios de color; faltan comprobaciones visuales más amplias. |

Los resultados proceden de comparaciones en la misma cámara con Galaxy,
1920×1080 y registros activados. Son resultados de esas escenas, no una promesa
para todos los modelos o niveles. El contador puede rondar 59,4 FPS por el coste
de los registros incluso al estar cerca del límite de pantalla.

La corrección de `kill_enable` ya está en la base publicada; se conserva y no
se presenta como una optimización nueva de esta rama.

## Tirones del minimapa en HMC

Confirmados en el log: pausas de ~105 ms, con ~98 ms preparando el GamePad.
El usuario confirma que desaparecen con Mirror TV. Se reproducen tanto con
F como con R y también con Mario original. Las muestras de altura coinciden
con los límites entre plantas del minimapa.

- `f89e8f65`: HMC y BBH eligen la planta por la altura del suelo de colisión,
  evitando cambiarla solo por la altura de un salto. Nadando, volando,
  colgado, en una barra o sin suelo válido se conserva la altura de Mario.
- La reconstrucción reutiliza las coordenadas de muestreo horizontal y
  evita mezclar píxeles totalmente opacos o transparentes. Las pruebas
  conservan exactamente los mismos píxeles, también con texturas HD.
- Los registros `MINIMAP_SELECT` y `MINIMAP_REBUILD` permiten comprobar
  cada cambio de imagen y su coste. Falta verificar la corrección en consola;
  no se promete eliminar el coste de la primera composición de otra planta.

En las pruebas visuales de color el usuario tampoco aprecia diferencias con
agua y fuego. Sigue siendo una comprobación parcial de RGBA8.

## Prueba actual: tamaño del búfer y selector 30/60 FPS

- `05cb8bfe` recupera el selector local **Display > Frame Rate > 30/60 FPS**.
  La elección se guarda como `frame_rate` y se aplica al terminar la actualización
  actual. En 30 se restaura la lista al estado lógico actual y se dibuja una vez;
  en 60 se mantiene el par interpolado/actual. Audio y lógica se actualizan una
  sola vez. El contador de FPS usa el número real de imágenes por actualización.
- Registros `revision=11`, `experiment=batch_capacity`. RGBA8 y el dibujo
  indexado B permanecen activados durante toda la prueba.
- El HUD muestra **B 256**, **B 512** o **B 1024**. Pulsar y soltar **−** cambia
  cíclicamente el límite de triángulos, al comienzo de la siguiente actualización.
  Empieza en 256. Los atajos **− + izquierda/derecha** conservan su función.
- Las mediciones se reinician al cambiar de tamaño o de objetivo de FPS.
  `MODEL_BUFFER_WINDOW` registra `triangles`, `target_fps` y `pool_bytes`,
  además de los tiempos, bytes enviados, índices y vaciados ya existentes.
- Comparar los tres tamaños con la misma cámara/modelo/opciones durante unos
  20 s cada uno y volver a 256 para contrastar. Primero mantener 60 FPS;
  después repetir a 30. No atribuir cargas de modelos o áreas al tamaño del lote.
- La capacidad máxima de almacenamiento es 1024, pero la reserva por lote y
  su vaciado obedecen al límite activo. El respaldo CPU crece de 84 a 336 KiB;
  los índices ocupan 6 KiB. La caché de slots RSP permanece en 68 entradas.
  El pool GPU conserva su capacidad máxima alcanzada hasta salir: `pool_bytes`
  no tiene por qué bajar al volver a un tamaño menor.
- El selector FPS se ofrece en compilaciones HIGH_FPS_PC; las nativas de 30
  permanecen a 30. A 30, los registros de fase solo contienen `current`; una
  ventana de 120 imágenes dura unos cuatro segundos en vez de dos.
- Ambos cambios están comprobados por pruebas automatizadas y compilación;
  su funcionamiento y beneficio en consola quedan pendientes de esta prueba.

Resultados previos: con Galaxy y la misma vista lenta, B mejoró de **39,96 a
59,44 FPS**, con preparación de **6,128 a 5,279 ms**. El usuario no aprecia
nuevos errores gráficos tras jugar con B. Render96 HD registra medianas de
**31,3 FPS** en castillo, **31,1** en HMC y **31,6** en COTMC; VCUTM ronda
**26,1**. Son resultados por escena, sin A/B controlado de Render96.

La caché reutiliza un vértice solo dentro del mismo lote y material, con la
misma versión del vértice y valor de LOD. Se conservan el descarte de caras,
el orden de los triángulos, la interpolación y las transparencias. Los dos
flujos de datos se publican antes de dibujar y se retienen hasta `GX2DrawDone`.

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
| 1 | Medir tamaños 256/512/1024 con B fijo. | Misma cámara/modelo y FPS objetivo; comparar preparación, FPS, vaciados y memoria. Repetir a 30 y 60 por separado. |
| 2 | Validar la caché de vértices preparados en consola. | Ya forma parte de B. Comprobar iluminación, niebla, texturas generadas, cambios de material y distintos modelos. |
| 3 | Consolidar o descartar el dibujo indexado según las mediciones. | La equivalencia y las primeras pruebas en consola pasan. Ampliar la validación visual y repetir las mediciones; Render96 aún no tiene un A comparable. |
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
