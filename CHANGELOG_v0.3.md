# CRAY LDR VOX LEVELER — v0.3

## Implementado
- Se amplió la región de clasificación acústica de eventos vocales para reducir falsos negativos en registros altos y voces brillantes.
- Se mantiene una condición de dominancia LF/HF para evitar que las fricativas de alta frecuencia se conviertan automáticamente en eventos vocales.
- Se añadió test sintético de voz brillante en registro alto a 44.1 y 48 kHz.
- Se actualizó la versión CMake a 0.3.0 y la documentación.

## Validación
- Núcleo DSP compilado en Release con GCC 13.
- 14/14 pruebas y 10 846 comprobaciones; 0 fallos.
- No se ha compilado ni validado el VST3 JUCE en Windows en esta entrega.

## Limitación
La clasificación continúa siendo una heurística basada en energía y balance espectral. No es pitch tracking, detección fonética ni segmentación lingüística de sílabas/palabras. La validación con grabaciones vocales reales sigue pendiente.
