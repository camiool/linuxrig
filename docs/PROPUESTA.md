# LinuxRig — Propuesta técnica

Rack de efectos para guitarra en tiempo real para Linux, al estilo de las apps tipo "rig" de iOS:
presets con una cadena de plugins, snapshots dentro de cada preset, control MIDI completo y
una interfaz pensada para tocar (no para producir).

---

## 1. Análisis del pedido

| Necesidad | Qué implica técnicamente |
|---|---|
| Guitarra → interfaz de audio → PC | Motor de audio de **baja latencia** (objetivo < 10 ms ida y vuelta) sobre JACK / PipeWire / ALSA |
| Cargar plugins VST3 "o compatibles" | **Host de plugins**: VST3 primero, LV2 (el formato nativo de Linux) después, CLAP opcional |
| Presets con cadena de efectos | Grafo de audio editable + serialización del estado de cada plugin |
| Snapshots | Variaciones de un mismo preset (bypass + valores de parámetros) que cambian **sin recargar plugins y sin cortes** |
| MIDI | Program Change → preset, CC → snapshot / bypass / parámetro, pedal de expresión, MIDI learn, tap tempo |
| UX tipo app de iOS | Vista de pedalera, modo escenario (botones grandes, apto para pantalla táctil), cambio de presets rápido |

Lo que **no** es: un DAW. No hay timeline, ni grabación multipista (salvo un looper más adelante).

---

## 2. Stack recomendado

**C++20 + JUCE 8 + CMake**

Motivos:
- JUCE trae resuelto el hosting de **VST3 y LV2 en Linux** (`AudioPluginFormatManager`, `AudioProcessorGraph`),
  los backends **JACK y ALSA**, entrada MIDI y la ventana para los editores nativos de los plugins.
- Es lo que usan la mayoría de los hosts comerciales; hay mucha documentación y ejemplos (`AudioPluginHost`).
- Licencia: JUCE es AGPLv3 (gratis si el proyecto es open source) o comercial. El SDK de VST3 es MIT desde la versión 3.8.

Alternativas consideradas:
- **Rust**: excelente para el motor, pero el hosting de VST3/LV2 todavía es inmaduro → mucho trabajo de bajo nivel.
- **Construir sobre Carla (libcarla)**: host muy completo (VST2/3, LV2, CLAP, puentes), pero su API es menos cómoda
  para una UX propia y es GPL. Buen plan B si JUCE se queda corto en algún formato.
- **Reutilizar MOD Desktop / Guitarix**: existen, pero ninguno ofrece el flujo preset → snapshot → MIDI que se busca.

**Plugins de Windows**: muchos plugins de guitarra populares no tienen versión Linux. Con **yabridge** (Wine)
se pueden usar VST3/CLAP de Windows desde cualquier host Linux, incluido este, sin cambios en la app.

---

## 3. Arquitectura

```
┌──────────────────────────── UI (hilo de mensajes) ─────────────────────────────┐
│  Vista Pedalera · Modo Escenario · Navegador de presets · Editor MIDI · Tuner  │
└──────────────┬──────────────────────────────────────────────▲──────────────────┘
               │ comandos (cola lock-free)                    │ estado/medidores
┌──────────────▼──────────────────────────────────────────────┴──────────────────┐
│ Controller: PresetManager · SnapshotManager · MidiMapper · PluginScanner       │
└──────────────┬─────────────────────────────────────────────────────────────────┘
               │
┌──────────────▼─────────────── Motor de audio (hilo RT) ────────────────────────┐
│ Entrada → Gain/Gate → [Slot 1] → [Slot 2] → … → [Slot N] → Volumen → Salida    │
│                        (cada slot = plugin + bypass con crossfade)             │
│ Tuner (en paralelo) · Medidores · Transporte/BPM para plugins sincronizados    │
└────────────────────────────────────────────────────────────────────────────────┘
          ▲ JACK / PipeWire-JACK / ALSA          ▲ MIDI (ALSA seq / JACK MIDI)
```

Reglas del hilo de audio: nada de `malloc`, locks, disco ni logs. La UI y el MIDI envían comandos por colas
lock-free; los cambios pesados (cargar un preset) se preparan fuera del hilo RT y se intercambian de forma atómica.

### Módulos

1. **AudioEngine** — dispositivo, buffer/sample rate, grafo, medición de latencia y xruns.
2. **PluginScanner** — escaneo **fuera de proceso** (un plugin roto no tumba la app), caché de la lista.
3. **Chain / Slot** — inserción, borrado, reordenamiento, bypass con micro‑crossfade (~10 ms) para evitar clicks.
4. **PresetManager** — guardar/cargar, bancos y setlists. Precarga del preset siguiente para cambios casi instantáneos.
5. **SnapshotManager** — aplica bypass + parámetros con rampas suaves; opción de "spillover" (las colas de delay/reverb no se cortan).
6. **MidiMapper** — PC, CC, notas; MIDI learn; curvas/rangos para expresión; tap tempo; MIDI out/feedback a controladores.
7. **Utilidades integradas** — afinador, noise gate, cargador de IR (convolución), metrónomo; looper en una fase posterior.

### Modelo de datos (JSON legible)

```json
{
  "name": "Lead Crunch",
  "bpm": 120,
  "chain": [
    { "id": "s1", "plugin": { "format": "VST3", "uid": "…", "name": "Amp X" },
      "state": "<base64 del estado del plugin>", "bypass": false }
  ],
  "snapshots": [
    { "name": "Rítmica", "slots": { "s1": { "bypass": false, "params": { "gain": 0.45 } } } },
    { "name": "Solo",    "slots": { "s1": { "bypass": false, "params": { "gain": 0.80 } } } }
  ],
  "midi": [
    { "type": "CC", "channel": 1, "number": 11, "target": "s1.param.gain", "min": 0.2, "max": 0.9 }
  ]
}
```

Snapshot = **delta** sobre el estado base del preset (solo los parámetros "capturados"),
igual que en las pedaleras de hardware: cambia rápido y no recarga nada.

---

## 4. Hoja de ruta

| Fase | Entregable | Criterio de "listo" |
|---|---|---|
| **0. Prueba de concepto** | Entrada → 1 VST3 → salida vía JACK/PipeWire | Tocar con < 10 ms de latencia y sin xruns en 30 min |
| **1. MVP** | Cadena de N slots, escáner, editores nativos, guardar/cargar presets | Armar un rig completo y recuperarlo tras reiniciar |
| **2. Snapshots + MIDI** | Snapshots, PC/CC, MIDI learn, pedal de expresión, tap tempo | Cambiar preset/snapshot desde una pedalera MIDI sin cortes |
| **3. UX de escenario** | Modo escenario, afinador, setlists, BPM global, gate/IR integrados | Usable en vivo solo con pies (y/o pantalla táctil) |
| **4. Extras** | LV2/CLAP completos, rutas paralelas (A/B, doble amp), looper, control remoto desde el teléfono (OSC/WebSocket) | — |

---

## 5. Configuración del sistema (necesaria para buena latencia)

- **PipeWire** con `pipewire-jack` (o JACK2 directo), quantum 64–128 a 48 kHz.
- Usuario en grupo `audio`/`realtime` con `rtprio 95` y `memlock unlimited`.
- Kernel con `PREEMPT_RT` (ya en mainline) o al menos `threadirqs`; governor de CPU en `performance`.
- La app mostrará latencia real y contador de xruns para diagnosticar.

---

## 6. Riesgos

- **Plugins que fallan** (crash, editores que no abren en Linux/Wayland): escaneo fuera de proceso y lista negra.
  Hosting fuera de proceso por plugin queda como opción futura (cuesta latencia).
- **Plugins vía yabridge**: más carga de CPU y algún problema de GUI; depende del plugin.
- **Cambio de preset sin hueco**: cargar plugins pesados (amps neuronales) tarda; se mitiga con precarga y caché.
- **Wayland**: los editores de plugins usan X11; correr bajo XWayland.

---

## 7. Decisiones tomadas

- **Interfaces de audio**: Focusrite Scarlett Solo y M-Audio M-Track 2x2M (ambas USB class-compliant, funcionan con ALSA/PipeWire sin drivers extra).
- **Plugins**: TONE3000 y plugins propios. Pendiente: confirmar formato (VST3/LV2/CLAP) y si son nativos Linux o requieren yabridge.
- **Plataforma**: PC de escritorio ahora; mini‑PC táctil para tocar en vivo a futuro (la UI de escenario se diseña pensando en eso).
- **Licencia**: open source (JUCE bajo AGPLv3).
- **Distribución**: CachyOS (base Arch). Dependencias vía `pacman`; kernel de baja latencia disponible en sus repos.
- **Entorno de desarrollo**: local, en el PC con CachyOS (ver motivo abajo).

El desarrollo se hace en local porque un contenedor en la nube no tiene interfaz de audio, ni JACK/PipeWire en
tiempo real, ni los plugins instalados, ni pantalla para la GUI: se podría compilar, pero no probar lo que importa
(latencia, xruns, carga de plugins, sonido).
