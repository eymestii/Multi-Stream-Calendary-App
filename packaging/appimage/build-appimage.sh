#!/usr/bin/env bash
# Construye StreamPlanner-v<versión>-linux-<arch>.AppImage con Qt6.
#
# Requisitos (Debian/Ubuntu):
#   sudo apt install build-essential cmake ninja-build qt6-base-dev libsqlite3-dev \
#        libgl1-mesa-dev qt6-wayland libssl-dev patchelf desktop-file-utils libfuse2 curl
#
# TLS: Twitch/Kick usan HTTPS. Qt6Network carga el backend OpenSSL en tiempo de
# ejecución; linuxdeploy-plugin-qt empaqueta libssl/libcrypto si están instaladas
# al compilar. Sin OpenSSL el AppImage no podrá iniciar sesión ni publicar.
#
# IMPORTANTE (compatibilidad): un AppImage hereda la versión de glibc del sistema donde
# se compila. Para que corra en distros antiguas, compílalo en la más antigua que quieras
# soportar (p. ej. Ubuntu 22.04 -> Qt 6.2). El workflow de GitHub Actions ya lo hace así.
#
# Uso: packaging/appimage/build-appimage.sh [directorio-de-salida]   (por defecto: ./dist)
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="${1:-$root/dist}"
build="$root/out/build/appimage"
appdir="$build/AppDir"
tools="$here/tools"
arch="$(uname -m)"
app_id="io.github.mestisexo.StreamPlanner"
version="$(sed -n 's/^project(StreamPlanner VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"

# QMake de Qt6 (lo necesita linuxdeploy-plugin-qt para localizar plugins)
QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake-qt6 || true)}"
[ -n "$QMAKE" ] || { echo "No se encontró qmake6. Instala qt6-base-dev (o exporta QMAKE=/ruta/a/qmake6)."; exit 1; }
export QMAKE

mkdir -p "$tools" "$out"
fetch() {  # fetch <nombre> <url>
    [ -x "$tools/$1" ] && return 0
    echo ">> Descargando $1"
    curl -fsSL -o "$tools/$1" "$2" && chmod +x "$tools/$1"
}
fetch "linuxdeploy-$arch.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$arch.AppImage"
fetch "linuxdeploy-plugin-qt-$arch.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$arch.AppImage"

echo ">> Compilando (Release, SQLite del sistema)"
rm -rf "$appdir"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DSP_BUILD_QT6=ON -DSP_SYSTEM_SQLITE=ON
cmake --build "$build"
DESTDIR="$appdir" cmake --install "$build"
# appimagetool aún busca el nombre antiguo (.appdata.xml) para validar los metadatos.
cp "$appdir/usr/share/metainfo/$app_id.metainfo.xml" "$appdir/usr/share/metainfo/$app_id.appdata.xml"

echo ">> Empaquetando AppImage"
# Sin FUSE (CI, contenedores) los AppImage de herramientas se ejecutan extrayéndose.
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$tools:$PATH"              # linuxdeploy busca sus plugins en PATH
export LDAI_OUTPUT="$out/StreamPlanner-v$version-linux-$arch.AppImage"
# Actualización incremental opcional, p. ej.: export LDAI_UPDATE_INFORMATION="gh-releases-zsync|mestisexo|Stream-Planner|latest|StreamPlanner-*-linux-*.AppImage.zsync"
export VERSION="$version"
# Wayland nativo (si el sistema de compilación lo tiene) además de xcb.
[ -e "$(dirname "$("$QMAKE" -query QT_INSTALL_PLUGINS)/platforms/libqwayland-generic.so")/libqwayland-generic.so" ] \
    && export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so"
# Para que respete el tema GTK en GNOME descomenta (aumenta el tamaño):
# export DEPLOY_PLATFORM_THEMES=1

"$tools/linuxdeploy-$arch.AppImage" \
    --appdir "$appdir" \
    --plugin qt \
    --desktop-file "$appdir/usr/share/applications/$app_id.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/$app_id.png" \
    --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/$app_id.svg" \
    --output appimage

echo ">> Listo: $LDAI_OUTPUT"
ls -lh "$LDAI_OUTPUT"
