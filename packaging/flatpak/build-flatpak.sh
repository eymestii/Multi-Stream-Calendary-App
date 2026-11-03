#!/usr/bin/env bash
# Compila e instala el Flatpak en tu usuario y genera un .flatpak distribuible.
#
# Requisitos:  sudo apt install flatpak flatpak-builder
#              flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
# Uso:         packaging/flatpak/build-flatpak.sh [directorio-de-salida]   (por defecto ./dist)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="${1:-$root/dist}"
app_id="io.github.mestisexo.StreamPlanner"
version="$(sed -n 's/^project(StreamPlanner VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
build="$root/out/build/flatpak"
mkdir -p "$out"

flatpak-builder --user --force-clean --install-deps-from=flathub \
    --repo="$build/repo" --install "$build/app" "$here/$app_id.yml"

flatpak build-bundle "$build/repo" "$out/StreamPlanner-v$version-linux.flatpak" "$app_id"
echo ">> Listo: $out/StreamPlanner-v$version-linux.flatpak"
echo "   Ejecutar:  flatpak run $app_id"
