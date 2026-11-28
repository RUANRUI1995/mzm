#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# A Windows Python does not read MSYS paths (/d/..., /home/...): give it a Windows one.
if command -v cygpath >/dev/null 2>&1; then
    ROOT_DIR="$(cygpath -m "$ROOT_DIR")"
fi

# Buscar ejecutable de Python 3
if command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN=(python3)
elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN=(python)
elif command -v py >/dev/null 2>&1; then
    # The devkitPro MSYS2 shell has no Python of its own; the Windows py launcher is on its PATH.
    PYTHON_BIN=(py -3)
else
    echo "Error: No se encontró Python 3 instalado en el sistema." >&2
    exit 1
fi

exec "${PYTHON_BIN[@]}" "$ROOT_DIR/tools/build_3ds.py" "$@"
