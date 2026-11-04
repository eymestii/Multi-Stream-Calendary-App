// Tipos de datos del dominio. Portable: NO incluir windows.h.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sp {

// Límite de Twitch para título del stream y para la notificación de "go live".
// (Verifica el valor vigente en la documentación de Twitch si algún día cambia.)
constexpr std::size_t kTwitchLimit = 140;

// Máximo de etiquetas personalizadas por serie y largo máximo del nombre de una etiqueta.
constexpr std::size_t kMaxTags = 10;
constexpr std::size_t kMaxTagNameLen = 32;

// Columnas del kanban: Planeado / En vivo / Realizado.
enum class Status : std::uint8_t { Planned = 0, Live = 1, Done = 2 };

struct Series {
    int id = 0;
    std::string name;
    std::string titleTemplate;  // p.ej. "[{series}] Día {day} | {topic}"
    std::string notifTemplate;  // p.ej. "Hoy en directo: {series} día {day} - {topic}"
    bool active = true;
};

// Valor de una etiqueta personalizada en un stream concreto.
struct TagValue {
    std::string name;
    std::string value;
};

// Último estado publicado/borrador por plataforma. No contiene tokens:
// las credenciales viven en el almacén de tokens del core de integraciones.
struct StreamTarget {
    std::string platform;  // "twitch" | "kick"
    std::string title;
    std::string categoryId;
    std::string categoryName;
    std::vector<std::string> tags;  // etiquetas de canal (no las de plantilla)
    std::string lastPublishedAt;
    std::string lastError;
};

struct Stream {
    int id = 0;
    int seriesId = 0;
    int number = 0;  // "día" elegido por el usuario; puede repetirse (varios streams el mismo día)
    Status status = Status::Planned;
    std::string date;  // YYYY-MM-DD
    std::vector<TagValue> tags;
    std::vector<StreamTarget> targets;  // estado por plataforma

    const std::string* findTag(std::string_view name) const;  // nullptr si no existe
    void setTag(std::string_view name, std::string value);

    const StreamTarget* findTarget(std::string_view platform) const;
    StreamTarget& target(std::string_view platform);  // crea si no existe
};

}  // namespace sp
