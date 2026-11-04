// Tipos del dominio de integraciones (Twitch / Kick).
// Portable: NO incluye Qt, windows.h ni WinHTTP.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sp::integrations {

enum class Platform : std::uint8_t { Twitch = 0, Kick = 1 };

inline const char* platformId(Platform p) { return p == Platform::Twitch ? "twitch" : "kick"; }
inline const char* platformName(Platform p) { return p == Platform::Twitch ? "Twitch" : "Kick"; }

inline bool platformFromId(std::string_view id, Platform& out) {
    if (id == "twitch") { out = Platform::Twitch; return true; }
    if (id == "kick") { out = Platform::Kick; return true; }
    return false;
}

// Lo que cada plataforma permite hacer de forma oficial. La UI se adapta a esto.
struct Capabilities {
    bool setTitle = false;
    bool setCategory = false;
    bool readTags = false;
    bool writeTags = false;
    bool requiresClientSecret = false;
    std::size_t maxTitleLen = 0;
    std::size_t maxTags = 0;
    std::size_t maxTagLen = 0;
};

enum class AuthState : std::uint8_t { Disconnected, Connecting, Connected, Error };

inline const char* authStateName(AuthState s) {
    switch (s) {
        case AuthState::Disconnected: return "disconnected";
        case AuthState::Connecting: return "connecting";
        case AuthState::Connected: return "connected";
        case AuthState::Error: return "error";
    }
    return "disconnected";
}

struct Account {
    Platform platform = Platform::Twitch;
    std::string userId;
    std::string login;
    std::string displayName;
    std::string avatarUrl;
};

struct Category {
    std::string id;
    std::string name;
    std::string boxArtUrl;
};

struct ChannelSnapshot {
    std::string title;
    std::string categoryId;
    std::string categoryName;
    std::vector<std::string> tags;
    // true si la plataforma devolvió el campo de etiquetas (aunque esté vacío).
    // Permite no borrar etiquetas cuando la API no las incluye (p. ej. offline).
    bool tagsKnown = false;
};

struct PublishRequest {
    std::string title;
    std::string categoryId;
    std::string categoryName;
    std::vector<std::string> tags;
    bool applyTitle = true;
    bool applyCategory = true;
    bool applyTags = true;
};

enum class Error : std::uint8_t {
    None,
    NotConfigured,
    NotConnected,
    Cancelled,
    Network,
    Unauthorized,
    Forbidden,
    RateLimited,
    InvalidRequest,
    Server,
    Parse,
    Unsupported
};

struct Result {
    Error error = Error::None;
    int httpStatus = 0;
    int retryAfterSeconds = 0;
    std::string message;

    bool ok() const { return error == Error::None; }
    static Result success() { return {}; }
    static Result failure(Error e, std::string msg = {}, int status = 0) {
        Result r;
        r.error = e;
        r.message = std::move(msg);
        r.httpStatus = status;
        return r;
    }
};

struct CategoryPage {
    std::vector<Category> items;
    std::string cursor;
};

struct ConnectResult {
    Result result;
    Account account;
    // Flujo en curso: la UI puede mostrar el código y la URL mientras se espera.
    // El mismo callback se invocará otra vez al terminar (pending=false).
    bool pending = false;
    std::string userCode;
    std::string verificationUri;
};

// Los callbacks SIEMPRE se invocan en el hilo de interfaz (ver IHttpClient).
using ConnectCallback = std::function<void(const ConnectResult&)>;
using VoidCallback = std::function<void(const Result&)>;
using AccountCallback = std::function<void(const Result&, const Account&)>;
using ChannelCallback = std::function<void(const Result&, const ChannelSnapshot&)>;
using CategoriesCallback = std::function<void(const Result&, const CategoryPage&)>;
using PublishCallback = std::function<void(const Result&)>;

}  // namespace sp::integrations
