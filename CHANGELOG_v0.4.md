# v0.4 — Conservative NATURAL gain envelope

- Added an asymmetric safety ceiling for NATURAL: positive correction is capped at +4.0 dB while negative correction remains bounded by the configured range (maximum 5 dB in NATURAL).
- EXTREME retains its configured range up to 8 dB.
- Rationale: positive gain is the operation most likely to expose breaths, headphone bleed and room tone. The cap reduces that risk without forcing the whole algorithm to use a lower symmetric range.
- No change to event detection, phrase grouping, timing, smoothing, host transport, or plugin wrapper.
- Reprocessed the supplied dry 48 kHz stereo vocal and regenerated both gain curves.
