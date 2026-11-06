# Fase 0 — Prueba de concepto

Objetivo: **entrada de la interfaz → 1 VST3 → salida** vía JACK (PipeWire), mostrando latencia y xruns.
Criterio de "listo": tocar con **< 10 ms ida y vuelta** y **0 xruns en 30 minutos**.

Código: `src/poc/` (app `LinuxRigPoC`).

- `AudioEngine`: callback de audio sin allocs ni locks. El plugin activo se publica con un puntero atómico
  y solo se destruye cuando el callback en curso terminó.
- `MainComponent`: selección de dispositivo, carga de un `.vst3`, editor nativo del plugin, ganancias,
  medidores y panel de diagnóstico.

## 1. Dependencias

```bash
sudo pacman -S --needed cmake ninja
```

El resto (gcc, pipewire-jack, cabeceras de X11/freetype/alsa) ya está instalado. JUCE (8.0.15) lo descarga
CMake en el primer configure (`-DLINUXRIG_JUCE_TAG=…` para cambiar de versión).

## 2. Compilar

```bash
cmake --preset release
cmake --build --preset release
```

## 3. Configurar el sistema para baja latencia

Estado encontrado el 2026‑11‑05: usuario fuera del grupo `realtime` (memlock 8 MB), governor `powersave`,
quantum de PipeWire 1024.

```bash
sudo gpasswd -a $USER realtime     # rtprio 98 + memlock ilimitado (paquete realtime-privileges); cerrar sesión después
sudo cpupower frequency-set -g performance   # paquete cpupower; solo mientras se prueba
```

Opcional, si aparecen xruns: añadir `threadirqs` a la línea del kernel.

En `pavucontrol` (o `wpctl`), poner la **Scarlett Solo** en el perfil **Pro Audio**: expone los canales
crudos y evita el remuestreo/mezcla del perfil normal.

## 4. Ejecutar

```bash
scripts/run-poc.sh 128 48000    # o 64 48000
```

El script fija el quantum de PipeWire (`PIPEWIRE_QUANTUM`) mientras la app está abierta.

1. En el primer arranque elige JACK y la Scarlett sola (entrada "Scarlett Solo USB Input 2 Inst/Line",
   salida "Headphones / Line 1-2"). Con JACK sobre PipeWire cada nodo es un "dispositivo"; sin esto JUCE
   tomaría el primero (el micrófono USB C-Media). **Audio…** permite cambiarlo; se guarda en
   `~/.config/LinuxRig/poc.settings` (borrarlo vuelve a la elección automática).
2. El plugin por defecto es `~/.vst3/TONE3000.vst3` (es **nativo de Linux**, no hace falta yabridge).
   **Cargar VST3…** permite elegir otro; **Editor** abre su interfaz.

Primer arranque verificado (2026‑11‑05): quantum 128 @ 48 kHz, ida y vuelta informada 8,00 ms
(4 + 4 ms), 0 xruns, CPU 0,6 % con TONE3000. Falta la prueba tocando 30 min.

## 5. Prueba de aceptación

1. Con 128 muestras: tocar 30 min. Pulsar **Reiniciar contadores** al empezar.
2. Anotar: latencia ida y vuelta mostrada, xruns, sobrecargas, CPU.
3. Repetir con 64 muestras.
4. Repetir con un plugin de Windows vía yabridge (`sudo pacman -S yabridge yabridgectl`) para medir el coste extra.

| Buffer | Plugin | Ida y vuelta | Xruns (30 min) | Sobrecargas | CPU | Notas |
|---|---|---|---|---|---|---|
| 128 | TONE3000 (nativo, con modelo) | 8,00 ms (informada) | 0 (sesión corta) | 0 | 6,4 % | Tocando; suena bien |
| 64  | TONE3000 (nativo) | | | | | Arrancó sin errores en PipeWire; sin medir |
| 128 | (Windows vía yabridge) | | | | | No probado |

**Cerrada el 2026‑11‑05.** El usuario dio por buena la Fase 0 sin la prueba de 30 min. Quedan como
verificación pendiente, a retomar si aparecen problemas: la prueba larga, 64 muestras medidas, los
ajustes RT del sistema y un plugin de Windows vía yabridge.

**Ojo con la latencia mostrada**: es la que informa el driver (más la del plugin). La real incluye los
conversores AD/DA de la interfaz (~1–2 ms extra). Para medirla de verdad: cable de la salida a la entrada
y `jack_iodelay` (paquete `jack-example-tools`) con `pw-jack`.
