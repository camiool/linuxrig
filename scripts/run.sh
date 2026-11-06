#!/usr/bin/env bash
# Lanza LinuxRig pidiéndole a PipeWire un quantum fijo.
# Uso: scripts/run.sh [buffer] [sample_rate]     (por defecto 128 48000)
set -euo pipefail

BUFFER="${1:-128}"
RATE="${2:-48000}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/release/LinuxRig_artefacts/RelWithDebInfo/LinuxRig"

if [[ ! -x "$BIN" ]]; then
    echo "No está compilado. Ejecuta: cmake --preset release && cmake --build --preset release" >&2
    exit 1
fi

# pipewire-jack respeta PIPEWIRE_QUANTUM: fuerza buffer/rate mientras la app está abierta.
export PIPEWIRE_QUANTUM="$BUFFER/$RATE"
exec "$BIN"
