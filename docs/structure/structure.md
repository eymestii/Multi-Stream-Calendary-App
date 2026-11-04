## 🧌 Estructura del proyecto

El núcleo es portable y no conoce ninguna interfaz. Qt6 y Win32 solo consumen las
cabeceras públicas de `src/core` y aportan el transporte y el OAuth propios de
cada plataforma.

```text
src/
├── core/                       Lógica transversal (sin Qt, sin windows.h)
│   ├── types.h, store.*        Modelo y operaciones en memoria
│   ├── persistence.cpp         SQLite + migración (schema v2)
│   ├── planner.*               Acciones, persistencia y estado por plataforma
│   ├── paths.*                 Rutas de BD, configuración y tokens
│   └── integrations/
│       ├── broadcaster.h       Interfaz común Twitch/Kick
│       ├── http.*              Transporte HTTP abstracto
│       ├── platform_services.h Navegador, loopback y temporizadores
│       ├── config.*            integrations.json, huella y verificación
│       ├── credentials.*       verificación de credenciales de app
│       ├── secure_file.*       escritura atómica con 0600 en Unix
│       ├── token_store.*       tokens.json (0600 en Unix)
│       ├── crypto.*            SHA-256, base64url y PKCE
│       ├── url.*               Codificación de query/formularios
│       ├── api_util.*          Errores HTTP y validaciones
│       ├── twitch_client.*     Device Code Flow + Helix
│       ├── kick_client.*       OAuth 2.1 + PKCE + API pública
│       └── integration_manager.h
├── platform/
│   ├── qt6/                    Interfaz Linux (Flatpak/AppImage)
│   │   └── integration/        QtNetwork, loopback, controlador, credenciales y panel
│   └── win32/                  Interfaz Windows
│       └── integration/        WinHTTP, Winsock, temporizadores, credenciales y diálogo
├── third_party/nlohmann/        json.hpp (solo lo usa el core)
tests/                          core_test + check_decoupling.sh
packaging/                      Flatpak, AppImage, metadatos
config/                         integrations.example.json
docs/                           Guías de integración y estructura
```

Reglas mantenidas por `tests/check_decoupling.sh`:

- `src/core` no incluye Qt, `windows.h`, `commctrl.h` ni `platform/`.
- Win32 y Qt6 no se incluyen entre sí.
- Las interfaces incluyen el core como `"core/..."`, nunca con rutas relativas.
