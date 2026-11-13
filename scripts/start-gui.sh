#!/bin/bash
set -euo pipefail

# Prefer an installed SAP GUI for Java 8.10. SAP_GUI_BIN can point to an
# installation elsewhere; the existing SAPGUI link remains a fallback.
gui_bin=${SAP_GUI_BIN:-}
if [[ -z "$gui_bin" ]]; then
    shopt -s nullglob
    gui_candidates=("$HOME"/SAPClients/SAPGUI8.10*/bin/guistart)
    if (( ${#gui_candidates[@]} )); then
        gui_bin=$(printf '%s\n' "${gui_candidates[@]}" | sort -V | tail -n 1)
    else
        gui_bin="$HOME/SAPClients/SAPGUI/bin/guistart"
    fi
fi

if [[ ! -x "$gui_bin" ]]; then
    printf 'SAP GUI launcher not found: %s\n' "$gui_bin" >&2
    exit 1
fi

exec "$gui_bin" '/H/localhost/S/3200&user=DEVELOPER&pass=ABAPtr2025#SP00&lang=EN'
