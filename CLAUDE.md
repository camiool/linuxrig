# LinuxRig

Rack de efectos para guitarra en tiempo real para Linux (estilo apps "rig" de iOS):
presets con cadena de plugins VST3, snapshots, control MIDI. Ver `docs/PROPUESTA.md` (propuesta completa).

## Contexto del usuario
- Idioma: español.
- SO: CachyOS (Arch). Paquetes con `pacman`; AUR disponible.
- Interfaces de audio: Focusrite Scarlett Solo (principal, instrumento en entrada 2) y M-Audio M-Track 2x2M.
- Audio: PipeWire (+ `pipewire-jack`). Objetivo < 10 ms ida y vuelta, buffer 64–128 @ 48 kHz.
- VST3 del usuario en `~/.vst3`. TONE3000 es la versión Windows → vía yabridge (`~/.vst3/yabridge`).
- Plugins propios del usuario: VST3 en su Mac, aún no portados a Linux (pendiente recompilar).
- Hoy en PC de escritorio; a futuro mini‑PC táctil en vivo.

## Decisiones técnicas
- C++20 + JUCE 8 + CMake (+ Ninja). Licencia del proyecto: open source (AGPLv3, por JUCE).
- Hosting VST3 primero; LV2/CLAP después.
- Hilo de audio sin allocs/locks/IO; UI → motor por colas lock-free.
- Escaneo de plugins fuera de proceso.
- Presets en JSON; snapshots = delta de bypass + parámetros sobre el preset.

## Estado / siguiente paso
Fase 0 (prueba de concepto): entrada de la interfaz → 1 VST3 → salida vía JACK/PipeWire,
mostrando latencia y xruns. Validar primero con un VST3 nativo de Linux y luego con TONE3000 vía yabridge.
