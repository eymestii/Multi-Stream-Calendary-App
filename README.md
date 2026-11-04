# Stream Planner

Planificador de series de streams multi-día con tablero kanban (planeado, en vivo,
realizado), plantillas de título/notificación y publicación en las plataformas
oficiales de Twitch y Kick.

## Características

- Series y streams con día, fecha, estado y etiquetas de plantilla.
- Vista previa del título y la notificación con límite de caracteres.
- Kanban por estado y copia rápida al portapapeles.
- **Integración oficial con Twitch**: Device Code Flow, título, categoría y
  etiquetas del canal.
- **Integración oficial con Kick**: OAuth 2.1 + PKCE con redirect loopback,
  título, categoría y etiquetas del canal.
- Búsqueda de categorías con scroll y paginación, como en las apps oficiales.
- Persistencia SQLite con migración automática y almacén de tokens separado.
- Interfaz Qt6 en Linux (Flatpak/AppImage) y Win32 en Windows.

La notificación a la comunidad se genera y se copia localmente: las APIs de
Twitch y Kick no permiten publicarla.

## Compilar

### Linux (Qt6)

Requisitos: CMake ≥ 3.24, Ninja, GCC/Clang, Qt 6.2+ (`Widgets`, `Network`) y
SQLite 3.

```bash
cmake -S . -B out/build/linux -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DSP_BUILD_QT6=ON -DSP_SYSTEM_SQLITE=ON
cmake --build out/build/linux
./out/build/linux/src/platform/qt6/stream-planner
```

### Windows (Win32, MSVC)

```powershell
cmake -S . -B out/build/x64-Release -A x64
cmake --build out/build/x64-Release --config Release
```

El ejecutable queda en `out/build/x64-Release/StreamPlanner.exe`. El instalador
de Inno Setup está en `installer/StreamPlanner.iss`.

## Configurar Twitch y Kick

La primera vez que abras la app verás un diálogo para registrar **tus propias**
credenciales de aplicación de Twitch o Kick. Se verifican contra la plataforma
antes de guardarlas y, sin esa verificación, la interfaz principal no se abre.

También puedes preparar el archivo manualmente antes de arrancar:

```bash
mkdir -p ~/.config/StreamPlanner
cp config/integrations.example.json ~/.config/StreamPlanner/integrations.json
```

En Linux el archivo se guarda con permisos `0600`. Para cambiar las credenciales
con la app abierta usa **Credenciales...** (Qt6) o **Integraciones →
Credenciales...** (Win32).

Guía completa (creación de apps, verificación, scopes, rutas por sistema, Flatpak
y solución de problemas): [`docs/integrations.md`](docs/integrations.md).

## Pruebas

```bash
cmake -S . -B out/build/dev -G Ninja -DSP_BUILD_QT6=OFF -DSP_BUILD_TESTS=ON
cmake --build out/build/dev
./out/build/dev/core_test
bash tests/check_decoupling.sh "$PWD"
```

## Estructura

```text
src/core/                  Lógica portable (sin Qt ni Win32)
  integrations/            OAuth, APIs de Twitch/Kick, tokens y configuración
src/platform/qt6/          Interfaz Linux (Qt6)
  integration/             Transporte QtNetwork, loopback y panel de publicación
src/platform/win32/        Interfaz Windows
  integration/             WinHTTP, Winsock, temporizadores y diálogo de publicación
packaging/                 Flatpak, AppImage y metadatos de escritorio
config/                    Plantilla de credenciales
tests/                     Pruebas del core y comprobación de desacoplamiento
docs/                      Guías de integración y estructura
```

## Empaquetado

- Flatpak: `packaging/flatpak/build-flatpak.sh` (incluye `--share=network`).
- AppImage: `packaging/appimage/build-appimage.sh` (requiere OpenSSL 3 para TLS).
- Windows: `build-release.ps1` y luego compilar el `.iss` con Inno Setup 6.
