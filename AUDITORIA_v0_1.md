# Auditoría técnica de DSP Core v0.1 (resumen)

1. Test engañoso: todo dentro de `assert()`. Con `-DNDEBUG` (Release) `prepare()` y `buildGainCurve()` no se ejecutan y aun así imprime PASS. Solo comprueba `isfinite`. (Corregido: arnés propio + control negativo.)
2. El motor equivale a un compresor bidireccional: `(ref - nivel) * sens` con attack/release. Medido: corte de -6 dB ~20 ms después del inicio de una sílaba fuerte y -8 dB sostenido > 100 ms sobre el material suave posterior. (Reemplazado por mesetas por evento con suavizado simétrico no causal; regresión cubierta por test.)
3. Suavizado causal: no usa la ventaja del análisis offline (anticipación).
4. Captura frágil: un seek/loop/discontinuidad detiene la captura sin avisar (verificado). Sin transporte. (Reemplazado por rejilla temporal indexada por posición, hops parciales descartados.)
5. No apto para tiempo real: `buildGainCurve` asigna y reemplaza `gainCurve_` mientras el audio puede leerlo (data race); `applyGain` hace búsqueda binaria y `pow` por muestra y canal. (Reemplazado por snapshot inmutable + quiescencia + ganancia lineal precalculada.)
6. Sin detección real: RMS de banda completa; respiraciones, sibilantes y ruido se suben; puerta fija a -55 dBFS sin histéresis; NATURAL/EXTREME solo cambiaban límites.
7. Faltaban: estado persistente, render offline, independencia del sample rate (curva en muestras), detección de análisis obsoleto, último frame parcial descartado, suma mono con cancelación de fase.
