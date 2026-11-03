#!/usr/bin/env bash
# Verifica que src/core NO depende de ninguna interfaz (Qt, Win32) y que las UIs
# no se incluyen entre sí. Uso: tests/check_decoupling.sh [raíz-del-repo]
set -u
root="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
bad=0
fail() { echo "ACOPLAMIENTO: $1"; bad=1; }

# 1) El core no puede incluir Qt, windows.h ni nada de platform/
if grep -rnE '#\s*include\s*[<"](Q[A-Za-z]+|windows\.h|commctrl\.h|platform/)' "$root/src/core"; then
    fail "src/core incluye cabeceras de interfaz"
fi
# 2) El core no usa tipos/macros de Qt ni de Win32 (salvo el #ifdef _WIN32 de paths/persistence)
if grep -rnE '\b(QString|QObject|Q_OBJECT|HWND|LPARAM|WPARAM)\b' "$root/src/core"; then
    fail "src/core usa tipos de interfaz"
fi
# 3) Win32 y Qt no se conocen entre sí
if grep -rnE '#\s*include\s*"platform/qt6' "$root/src/platform/win32"; then fail "win32 -> qt6"; fi
if grep -rnE '#\s*include\s*"platform/win32' "$root/src/platform/qt6"; then fail "qt6 -> win32"; fi
# 4) Qt solo debe usar el core a través de cabeceras "core/..."
if grep -rnE '#\s*include\s*"(\.\./)+core' "$root/src/platform"; then fail "include relativo hacia core"; fi

[ $bad -eq 0 ] && echo "OK: core desacoplado de las interfaces"
exit $bad
