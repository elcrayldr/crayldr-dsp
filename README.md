# CRAY LDR VOX LEVELER v0.4 (NATURAL safety refinement)

Automatizador vocal de volumen (VST3, Windows x64). Analiza la toma completa en **una pasada de reproducción del DAW**, genera una **curva de ganancia** y la aplica sincronizada con la posición del host. No es un compresor: la ganancia es una meseta por evento vocal que solo se mueve en los valles entre eventos, con suavizado simétrico (con anticipación). No exporta ni importa audio.

## Compilar con Visual Studio 2022 (Windows x64)

Requisitos: Visual Studio 2022 con la carga "Desarrollo para el escritorio con C++", CMake >= 3.22 y Git. Necesita internet la primera vez (descarga JUCE 8.0.6 con FetchContent).

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Resultado: `build\CrayLdrVoxLeveler_artefacts\Release\VST3\CRAY LDR VOX LEVELER.vst3`
Copiar esa carpeta a `C:\Program Files\Common Files\VST3\` y reescanear plugins en FL Studio 26 / Cubase 13.

Alternativa en el IDE: *Archivo > Abrir > Carpeta...* sobre este directorio y compilar el destino `CrayLdrVoxLeveler_VST3`.
Si cambias la versión de JUCE: `-DCRAYLDR_JUCE_TAG=<tag>` (el código del editor usa la API de JUCE 8).
Solo núcleo + pruebas (sin JUCE): `cmake -S . -B build -DCRAYLDR_BUILD_PLUGIN=OFF`.
En GitHub, `.github/workflows/build.yml` compila el VST3 en `windows-latest`, ejecuta las pruebas, sube el `.vst3` como artefacto y lanza pluginval nivel 5.

## Uso
1. Inserta el plugin en la pista de voz **antes** de otros efectos que cambien el nivel (compresores, etc.) o asegúrate de que no cambien entre análisis y mezcla.
2. Pulsa **ANALIZAR** y reproduce la toma completa. Al parar el transporte (con >= 1 s capturado) se finaliza solo; también puedes pulsar **FINALIZAR**.
3. La curva se aplica en cuanto está lista (**APLICAR CURVA** activo). Mientras analiza, el audio pasa sin tocar.
4. Cambiar modo, cantidad, rango o techo de pico reconstruye la curva al instante desde el análisis guardado, sin volver a reproducir.
5. Se admiten seeks, loops y re-análisis parcial (los tramos se sobrescriben por posición). **BORRAR** descarta análisis y curva.
6. Análisis y curva viajan dentro del estado del plugin (proyecto del DAW).

Modos: **NATURAL** (jerarquía frase 0.85 / grupo 0.55 / evento 0.30, transiciones ~55 ms, pendiente <= 300 dB/s) y **EXTREME** (1.0 / 0.90 / 0.80, ~25 ms, <= 1200 dB/s). Rango = tope de subida y bajada. El techo de pico impide que una subida lleve un pico conocido por encima de él.

## Cambios v0.4 respecto a v0.3
- NATURAL limita las subidas a +4 dB y las bajadas a -5 dB como máximo, aunque el control de rango esté más alto. EXTREME conserva hasta +/-8 dB.
- Motivo: reducir el riesgo de levantar respiraciones, ruido de sala y bleed al compensar sílabas suaves.
- La toma real adjunta se ha reprocesado; en esta toma el máximo NATURAL baja de +4,83 a +4,00 dB. El resto del algoritmo no se ha modificado.

## Cambios v0.3 respecto a v0.2
- Clasificador acústico menos restrictivo para voces brillantes y registros altos: LF mínimo 0.20 (antes 0.30), HF máximo 0.76 (antes 0.60) y comprobación relativa LF/HF. Se mantiene una protección frente a fricativas dominadas por alta frecuencia.
- Se añadió una prueba sintética de voz brillante en registro alto (420–460 Hz), a 44.1 y 48 kHz.
- No se presenta esta mejora como reconocimiento de tono, sílabas o fonemas. El análisis sigue siendo acústico y heurístico.
- Versión CMake actualizada a 0.4.0.

## Pruebas ejecutadas en esta entrega
Núcleo DSP compilado en Release con GCC 13 en este entorno: **14/14 pruebas**, 10 846 comprobaciones, 0 fallos. Incluye la prueba nueva de registro alto. Esto no equivale a validar el VST3 con Visual Studio, pluginval, FL Studio 26 o Cubase 13.

## Arquitectura
`Source/Core` (sin JUCE, probado aquí): `Analyzer` (RMS, pico, balance LF/HF por hop de 5 ms, sin reservas de memoria), `AnalysisGrid` (indexada por posición, preasignada 30 min), `CurveBuilder` (puerta adaptativa, eventos por prominencia de valle, grupos y frases, objetivos jerárquicos, Hann + límite de pendiente Lipschitz + guarda de pico), `CurveSnapshot` (inmutable; ganancia = función pura de la posición absoluta), `Serialization` (binario versionado con CRC, 8 bytes/hop), `VoxLevelerCore` (hilos y transporte).
Hilo de audio: sin locks, sin asignaciones. Publicación de curvas por puntero atómico y protocolo de quiescencia (`busy_`/`seq_`); un snapshot retirado solo se libera cuando se demuestra que el audio no lo lee. Latencia 0.

## Pruebas (`Tests/CoreTests.cpp`, sin `assert`)
13 pruebas / 10 840 comprobaciones: mapeo de hops a 44.1/48/88.2/96 kHz; análisis idéntico para bloques de 1 a 4096; seek, loop y hops parciales; segmentación (9 eventos, 3 frases; fricativa y respiración no son eventos); nivelación (rango de niveles 16.0 dB -> NATURAL 8.4 dB, EXTREME 2.4 dB); sílaba suave tras una fuerte no se atenúa (fallo de v0.1); sin movimiento de ganancia dentro de un evento; fricativas, respiraciones y ruido de sala nunca se suben; pendiente y rango; techo de pico; **aplicación única** (entrada 1.0 => salida = curva, no curva²; mismo resultado bit a bit con cualquier partición de bloques, seek, loop, estéreo; paso exacto si el transporte está parado o la posición es desconocida); **cero asignaciones** en `processBlock`; serialización con rechazo de 400/400 corrupciones y truncados; cambio de sample rate; monitor de análisis obsoleto; hilo de audio frente a hilo de control. `NegativeControl` demuestra que el arnés falla de verdad en Release.
Verificadas aquí con g++ 13: Release (-DNDEBUG), ASan+UBSan y ThreadSanitizer, todo limpio.

## Límites conocidos (honestidad)
- "Eventos / grupos / frases" son unidades **acústicas** (núcleos de energía, huecos cortos y largos). No hay reconocimiento fonético ni lingüístico de sílabas/palabras. La clasificación vocal/no vocal usa balance espectral LF/HF, no autocorrelación de tono.
- El techo de pico usa el pico por hop de 5 ms; entre muestras (inter-sample) no se mide.
- Si cambias el audio de la pista (edición, FX previo) tras analizar, la curva queda desalineada; el monitor lo avisa, pero hay que re-analizar.
- Análisis limitado a los primeros 30 min de la línea de tiempo del proyecto.
- Requiere que el host informe de la posición de línea de tiempo (FL Studio y Cubase lo hacen); si no, el plugin deja pasar el audio sin tocar.
- La parte JUCE (`PluginProcessor`/`PluginEditor`) y el empaquetado VST3 **no se han compilado ni probado en esta entrega**; el núcleo DSP sí.
- La clasificación de voz sigue dependiendo de LF/HF. La mejora de v0.3 reduce falsos negativos en voces brillantes, pero puede necesitar ajuste con voces reales procesadas, falsete, distorsión y dobles.

## Validación manual pendiente (FL Studio 26 y Cubase 13)
1. Escanea, abre y guarda/recarga el proyecto: la curva debe seguir ahí sin re-analizar.
2. Analiza reproduciendo desde el inicio; analiza con loop activo; salta con seek durante el análisis.
3. Reproduce con la curva: pasa por seek, loop y parada/arranque sin saltos audibles.
4. Renderiza offline y compara con la reproducción en tiempo real (deben ser idénticos).
5. Cambia el sample rate del proyecto y repite 3.
6. Estéreo y mono; automatiza APLICAR CURVA.

## GitHub / distribución
Este paquete de código fuente no incluye grabaciones vocales privadas, renders procesados ni curvas de análisis generadas a partir de una toma personal. La validación con voces reales debe mantenerse como material de prueba privado, salvo autorización expresa para publicarlo. Antes de publicar el repositorio, el propietario debe elegir una licencia; no se presupone ninguna. Consulta `GITHUB_RELEASE_CHECKLIST.md`.
