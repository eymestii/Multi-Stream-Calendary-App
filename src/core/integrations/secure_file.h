// Escritura atómica de archivos que contienen secretos.
// En Unix el archivo nace con permisos 0600 (solo el propietario puede leerlo).
// En Windows se apoya en los permisos de la carpeta de usuario (%APPDATA%).
#pragma once
#include <filesystem>
#include <string>

namespace sp::integrations {

// Crea los directorios padre si faltan y escribe `data` en un temporal privado
// que después se renombra sobre `file`. Devuelve false si algo falla.
bool writePrivateFile(const std::filesystem::path& file, const std::string& data);

}  // namespace sp::integrations
