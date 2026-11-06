# Fase 1 — MVP

Objetivo: cadena de N plugins, escáner, editores nativos y presets.
Criterio de "listo": **armar un rig completo y recuperarlo tras reiniciar**.

```bash
cmake --preset release && cmake --build --preset release
ctest --preset release          # tests del motor y de los presets
scripts/run.sh [buffer]         # 128 por defecto
```

Tests con AddressSanitizer + UBSan: `cmake --preset debug && cmake --build --preset debug && ctest --preset debug`.

## Código

| Carpeta | Qué hay | Depende de UI |
|---|---|---|
| `src/engine/` | `Slot` (plugin + bypass con fundido de 10 ms), `ChainProcessor` (cadena en serie, cambio en caliente), `AudioEngine` (dispositivo → cadena) | No |
| `src/core/` | `Preset` (JSON), `PluginCatalog` (escaneo fuera de proceso + caché), `Rig` (la cadena vista desde la UI) | No |
| `src/app/` | `MainComponent` (ventana), `Pedalboard` (fila de pedales) | Sí |
| `tests/` | Tests del motor y de los presets (sin interfaz de audio ni plugins) | No |
| `src/poc/` | Prueba de concepto de la Fase 0, se conserva como referencia | Sí |

Reglas del motor:
- La cadena activa es una lista inmutable publicada con un puntero atómico. Cada edición arma una lista
  nueva fuera del hilo de audio y la intercambia; la vieja se libera cuando el callback en curso terminó.
- Los slots se comparten entre listas: reordenar no recrea plugins.
- Un slot en bypass no procesa; al cambiar de estado hace un fundido de 10 ms entre seco y procesado.
- Bus interno estéreo: la guitarra (mono) entra en los dos canales; un plugin de salida mono se copia a ambos.

## Archivos del usuario (`~/.config/LinuxRig/`)

| Archivo | Contenido |
|---|---|
| `session.json` | La cadena actual con el estado de cada plugin. Se guarda 2 s después de cada cambio, cada 60 s y al cerrar (también con SIGTERM/Ctrl-C). Se carga al abrir. |
| `presets/*.json` | Presets guardados con "Guardar" / "Guardar como...". |
| `plugins.xml` | Caché del escáner y lista negra. "Reescanear plugins" la rehace desde cero. |
| `app.settings` | Dispositivo de audio, entrada, ganancias, último preset. |

Si un preset apunta a un plugin que no está instalado, el slot queda marcado "(falta)", deja pasar la señal
y conserva sus datos: al volver a guardar no se pierde.

## Verificación (2026‑11‑06)

- Tests: 10/10 en release; sin errores de ASan/UBSan en 3 corridas (UBSan avisa de un `memcmp` nulo dentro de
  `juce::MemoryBlock` al comparar bloques vacíos en los tests; es de JUCE, no afecta a la app).
- Escáner fuera de proceso: TONE3000 encontrado y cacheado.
- Sesión con 2 × TONE3000 (el 2.º en bypass): restaurada al abrir, guardada al cerrar con el estado real de
  cada plugin (5048 bytes) y el bypass; vuelta a abrir igual.
- Falta que el usuario lo pruebe tocando: armar un rig con modelos cargados, reiniciar y comprobar que suena igual.

## Pendiente para fases siguientes

- Cargar un preset crea los plugins en el hilo de mensajes: con plugins pesados la UI se congela un momento
  (precarga en segundo plano: Fase 2/3, junto con los snapshots).
- Reordenar con arrastrar y soltar; por ahora botones `<` `>`.
