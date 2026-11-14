# Estado de las optimizaciones de Wii U

Rama: `feature/wiiu-geometry-optimization`.
Base: `takatoon/feature/wiiu-minimap`, commit `bc4f6118`, verificado en GitHub
antes de crear la rama. `main` no se modifica.

## Conclusiones de las pruebas — 14 de noviembre de 2026

| Trabajo | Resultado | Decisión |
| --- | --- | --- |
| Colores RGBA8 | Menos preparación y datos enviados; aprobado por el usuario tras las comprobaciones visuales. | Conservar; activado por defecto en `b121de77`. |
| Dibujo indexado y caché de vértices preparados | Galaxy mejora de unos 40 a 59–60 FPS en una misma vista. El usuario no aprecia nuevos problemas tras jugar con Galaxy y Render96 HD. | Conservar B. En la compilación de pruebas está fijo; su activación fuera del perfilado y la limpieza de diagnósticos quedan para la consolidación. |
| Búfer de 256/512/1024 triángulos | Muchos menos vaciados por capacidad, sin ganancia sustancial de FPS demostrada en BOB con Render96 HD. | Ensayo evaluado: no elevar el límite por defecto ni implementar adaptación automática basándose solo en los vaciados. Mantener 256 como referencia. |
| Selector 30/60 FPS | Cambia correctamente la ruta de dibujo y el contador; Galaxy alcanza 30 en escenas ligeras. Render96 HD ronda 24 en la prueba de BOB a objetivo 30. | Conservar el selector; revisar la sincronización de presentación bajo carga antes de prometer 30 estables. |

La ampliación del búfer no es la misma mejora que la caché introducida con B:
la primera aumenta los triángulos que caben en un lote; la segunda evita
preparar y enviar repetidamente vértices compartidos. El resultado del ensayo
de tamaños no invalida la mejora del dibujo indexado.

## Cambios aplicados

| Commit | Cambio | Resultado / estado |
| --- | --- | --- |
| `80107af9` | Instrumentación de geometría, tiempos por fase, prueba A/B y prevención del sueño durante la prueba. | Herramientas de diagnóstico; no se contabilizan como una mejora de rendimiento. |
| `7bbcffd2` | Preparar proporción de pantalla, indicadores y luces modificadas una vez por carga de vértices. | Geometría equivalente; perfilado en consola junto con el resto de cambios. Falta una comparación controlada para atribuirle una ganancia individual. |
| `e903d4f6` | Reutilizar la preparación del material entre comandos de vértices y triángulos. | Evita consultas repetidas de shader, texturas y estado. Invalidación conservadora al cambiar otros comandos. Perfilado junto con los demás cambios; sin ganancia individual aislada. |
| `f15f6c7d` | Escribir atributos directamente en el búfer de GX2, evitando la copia intermedia. | Mejora confirmada en una vista inicial: dos bloques de copia a 46,4 FPS frente a dos directos a 59,4 FPS. La preparación baja aproximadamente un 4,4 %. No garantiza 60 FPS en todas las vistas. |
| `16beeed5` | Preparar constantes del material una sola vez; conservar valores dinámicos por vértice y redondeo del LOD. | Ahorro de aproximadamente 0,07–0,10 ms por imagen, un 1–1,3 %. En la vista lenta ambos métodos siguen a 40 FPS. |
| `fd0d2f97` | Formato RGBA8 para colores y niebla, con lectores de atributos independientes para float y compacto. | Primera prueba: ~1 ms / 14 % menos preparación y ~25 % menos datos. En una pareja completa de la vista lenta, ~40 → 55 FPS. Aprobado por el usuario posteriormente. |
| `af189c6d` | Selector manual F/R e indicador durante las pruebas de color. | Herramienta de comparación histórica; la prueba actual usa RGBA8 fijo y el botón cambia el tamaño del lote. |
| `f89e8f65` | Seleccionar la planta del minimapa por el suelo bajo Mario en HMC y BBH. | Corrige la selección motivada únicamente por la altura del salto. Comprobación específica en consola pendiente. |
| `029d2de4` | Reducir cálculos de escalado y mezcla al reconstruir el panel del GamePad. | Salida idéntica en pruebas normales/HD; sin ganancia aislada medida en consola. |
| `b121de77` | Activar RGBA8 por defecto tras la aprobación del usuario. | Conservado. Posiciones y coordenadas de textura siguen en float. |
| `6294da88` | Dibujo indexado y caché de vértices preparados, con comparación A/B. | En la misma vista de Galaxy, ~40 → 59–60 FPS y ~14 % menos preparación. Validación posterior jugando con Render96 HD, sin nuevos problemas visuales comunicados. |
| `05cb8bfe` | Recuperar el selector 30/60 FPS de la rama local. | Cambio en caliente, una o dos imágenes por actualización y contador correcto. Revisar presentación a 30 bajo carga. |
| `561deae4` | Ensayar límites de 256/512/1024 triángulos mediante «−». | Sin aumento claro de FPS en BOB. Se conserva como herramienta de diagnóstico, no como mejora de rendimiento aprobada. |

Las ganancias aisladas indicadas con Galaxy proceden de comparaciones en la
misma cámara, 1920×1080 y registros activados. Las pruebas posteriores jugando
con Render96 HD y la comparación de tamaños de BOB incluyen movimiento y no
deben tratarse como comparaciones de cámara idéntica. Son resultados de esas
escenas, no una promesa para todos los modelos o niveles. El contador puede
rondar 59,4 FPS por el coste de los registros cerca del límite de pantalla.

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

## Última prueba evaluada: tamaño del búfer y selector 30/60 FPS

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
- Prueba BOB con Render96 HD: medianas a objetivo 60 de **29,0 / 29,3 / 29,0
  FPS** para 256/512/1024; a objetivo 30, **24,0 / 24,0 / 24,1 FPS**.
  Los vaciados por capacidad bajan de unos 51 a 6, pero no hay ganancia
  sustancial demostrada de FPS. Se jugó en movimiento, sin cámara idéntica.
- La ruta de 30 sí dibuja solo la imagen actual. Bajo esta carga registra
  ~7 ms extra de espera de presentación; revisar sincronización al superar
  el presupuesto de 33,3 ms. Galaxy sí mantiene ~30 en escenas más ligeras.
- El análisis no modifica el renderizador ni genera otra compilación.

Conclusión del ensayo: no se ha demostrado que 512 o 1024 sea más rápido que
256 en estas condiciones. No es una prueba de que nunca puedan ayudar en otra
escena, pero no justifica aumentar el valor por defecto. La compilación de
diagnóstico conserva el selector y sigue arrancando con límite 256; antes de
una compilación normal conviene retirar también la capacidad de almacenamiento
extra reservada solo para probar 1024.

El tramo de preparación de geometría ronda 28 ms por imagen con Render96 HD
en BOB, incluidos unos 10 ms de transformación e iluminación. Esto señala un
límite importante en el trabajo de CPU. La espera al terminar el render no
mide el tiempo total ni el porcentaje de utilización de la GPU: su margen
real sigue sin estar cuantificado.

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

## Experimentos descartados o no adoptados

| Ensayo | Motivo |
| --- | --- |
| Puntero local para rellenar cada triángulo. | Sólo 0,025–0,030 ms de diferencia, menos del 0,4 %, sin mejora práctica de FPS. No está en las rutas activas de esta rama. |
| Reutilizar coordenadas proyectadas para descartar caras. | La comparación con fast-math detectó diferencias en el descarte. No se conserva. |

El ensayo de ampliar los lotes ya está evaluado sin beneficio sustancial de
FPS demostrado. Su selector permanece en la compilación de diagnóstico; no
se ha retirado código ni cambiado el WUHB al cerrar este análisis.

## Próximas mejoras posibles

| Prioridad | Trabajo | Qué medir o comprobar antes de aplicarlo |
| --- | --- | --- |
| 1 | Revisar la presentación a 30 FPS bajo carga. | Investigar los ~7 ms de espera añadida en BOB con Render96 HD, sin confundir imágenes presentadas con actualizaciones de la lógica ni quitar sincronización a ciegas. |
| 2 | Desglosar el coste restante de listas y triángulos. | Separar descarte, recorrido de órdenes, matrices y preparación de atributos antes de elegir otra optimización. Los envíos solo ocupan ~1,5 ms de los ~28 ms de preparación. |
| 3 | Comparar una compilación nativa de 30 con el selector a 30. | Misma cámara, modelo y opciones. Medir el ahorro de preparación de interpolación; no hay resultados todavía. |
| 4 | Medir trabajo y margen reales de la GPU. | Usar medidas de GPU; `GX2DrawDone` es una espera de la CPU. No concluir que la GPU va sobrada a partir de esa espera. |
| 5 | Optimizar transformación e iluminación de vértices en CPU. | Ese tramo ronda 10 ms en la prueba de Render96. Contrastar el código generado y conservar redondeo, luces, niebla y recorte. |
| 6 | Consolidar las mejoras verificadas para una compilación normal. | Activar B fuera del perfilado, retirar los controles experimentales y la reserva ampliada, conservar el selector FPS y repetir comprobaciones de regresión. No es una instrucción de compilar ahora. |
| 7 | Evaluar una ruta de modelos residentes y transformados en GPU. | Investigación de mayor alcance: matrices/huesos, materiales, compatibilidad de modelos y coste trasladado a GPU. Sin estimación medida de FPS. |
| 8 | Evaluar solapamiento de CPU/GPU y formatos de vértice más compactos. | Proteger recursos en vuelo, comprobar latencia y medir alineación/ancho de banda. No sumar ganancias teóricas ni retirar simplemente las esperas. |

## Verificación reproducible

En una copia normal del repositorio, crear la referencia desde el commit de
instrumentación inicial y ejecutar los tests en el entorno de desarrollo:

```sh
git show 80107af9:src/pc/gfx/gfx_pc.c > /tmp/gfx_reference.c
python3 tools/tests/check_wiiu_geometry.py --reference /tmp/gfx_reference.c --writer packed --fast-math
python3 tools/tests/check_wiiu_geometry.py --reference /tmp/gfx_reference.c --writer packed --sanitize
python3 tools/tests/check_wiiu_geometry.py --reference /tmp/gfx_reference.c --writer indexed --batch-limit 256 --fast-math
python3 tools/tests/check_wiiu_geometry.py --reference /tmp/gfx_reference.c --writer indexed --batch-limit 512 --fast-math
python3 tools/tests/check_wiiu_geometry.py --reference /tmp/gfx_reference.c --writer indexed --batch-limit 1024 --fast-math
python3 tools/tests/check_wiiu_geometry.py --reference /tmp/gfx_reference.c --writer indexed --batch-limit 1024 --sanitize
python3 tools/tests/check_wiiu_packed_colors.py
python3 tools/tests/check_wiiu_geometry_profile.py
python3 tools/tests/check_wiiu_geometry_ab.py
python3 tools/tests/check_wiiu_frame_rate.py
python3 tools/tests/check_wiiu_color_controls.py
python3 tools/tests/check_wiiu_drc_blit.py
python3 tools/tests/check_wiiu_gamepad_ui.py
python3 tools/tests/test_wiiu_gfx_resources.py
python3 tools/tests/test_wiiu_gfx_shutdown.py
```

El test de geometría decodifica los colores compactos a sus valores normalizados
antes de compararlos con la referencia: el formato binario intencionadamente
cambia. Los tests no ejecutan la GPU real. Se conservan los datos de rendimiento
y las limitaciones en [el registro de experimentos](wiiu-geometry-profiling.md).
Este cierre solo actualiza la documentación y su historial; no vuelve a ejecutar
las pruebas ni genera una nueva compilación.
