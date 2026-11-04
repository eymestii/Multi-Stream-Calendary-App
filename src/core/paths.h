// Rutas de datos por plataforma. Portable: sin Qt ni windows.h en la cabecera.
#pragma once
#include <filesystem>

namespace sp {

struct DataPaths {
    std::filesystem::path db;      // base de datos SQLite
    std::filesystem::path legacy;  // data.txt antiguo para migrar (vacío si no aplica)
};

// Windows: %LOCALAPPDATA%\StreamPlanner (legado en %APPDATA%\StreamPlanner\data.txt).
// Linux/Unix: $XDG_DATA_HOME/StreamPlanner o ~/.local/share/StreamPlanner.
//   Dentro de Flatpak, XDG_DATA_HOME apunta solo a ~/.var/app/<id>/data.
// Si existe la variable de entorno STREAMPLANNER_DB, se usa esa ruta (sin migración).
// Crea el directorio de la base de datos si no existe.
DataPaths defaultDataPaths();

// Configuración de integraciones y tokens OAuth.
// Windows: %APPDATA%\StreamPlanner\integrations.json y tokens en %LOCALAPPDATA%.
// Linux/Unix: $XDG_CONFIG_HOME/StreamPlanner/integrations.json y tokens junto a la BD.
// Overrides: STREAMPLANNER_CONFIG y STREAMPLANNER_TOKENS.
// Crea los directorios padre si no existen.
std::filesystem::path defaultConfigPath();
std::filesystem::path defaultTokenPath();

}  // namespace sp
