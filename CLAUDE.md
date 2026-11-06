# LinuxRig

Rack de efectos para guitarra en tiempo real para Linux (estilo apps "rig" de iOS):
presets con cadena de plugins VST3, snapshots, control MIDI. Ver `docs/PROPUESTA.md` (propuesta completa).

## Contexto del usuario
- Idioma: español.
- SO: CachyOS (Arch). Paquetes con `pacman`; AUR disponible.
- Interfaces de audio: Focusrite Scarlett Solo (principal, instrumento en entrada 2) y M-Audio M-Track 2x2M.
- Audio: PipeWire (+ `pipewire-jack`). Objetivo < 10 ms ida y vuelta, buffer 64–128 @ 48 kHz.
- VST3 del usuario en `~/.vst3`. TONE3000 instalado ahí es **nativo de Linux** (JUCE 9.0.1); yabridge
  (`~/.vst3/yabridge`, aún no instalado) solo para plugins de Windows.
- Otras entradas de audio presentes: micrófono USB C-Media (CM108) y audio interno; con JACK no deben ser el default.
- Plugins propios del usuario: VST3 en su Mac, aún no portados a Linux (pendiente recompilar).
- Hoy en PC de escritorio; a futuro mini‑PC táctil en vivo.

## Decisiones técnicas
- C++20 + JUCE 8 + CMake (+ Ninja). Licencia del proyecto: open source (AGPLv3, por JUCE).
- Hosting VST3 primero; LV2/CLAP después.
- Hilo de audio sin allocs/locks/IO; UI → motor por colas lock-free.
- Escaneo de plugins fuera de proceso.
- Presets en JSON; snapshots = delta de bypass + parámetros sobre el preset.

## Compilar
`cmake --preset release && cmake --build --preset release`; ejecutar con `scripts/run-poc.sh [buffer]`.
JUCE 8.0.15 vía FetchContent (`LINUXRIG_JUCE_TAG`).

## Estado / siguiente paso
Fase 0 (`src/poc/`, ver `docs/FASE0.md`): **cerrada**. TONE3000 a 128 @ 48 kHz, 8 ms informados,
0 xruns, 6,4 % CPU. Sin prueba de 30 min (decisión del usuario); ajustes RT y yabridge sin probar.
Siguiente: Fase 1 (MVP).
