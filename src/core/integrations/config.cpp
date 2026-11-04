#include "core/integrations/config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/integrations/crypto.h"
#include "core/integrations/secure_file.h"

namespace sp::integrations {
namespace {

bool placeholder(const std::string& v) {
    return v.empty() || v.rfind("GENERIC_", 0) == 0 || v.find('<') != std::string::npos;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::string readString(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

bool readBool(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}

bool loopbackRedirect(const std::string& uri) {
    constexpr const char* kIp = "http://127.0.0.1:";
    constexpr const char* kLocal = "http://localhost:";
    std::size_t prefix = 0;
    if (uri.rfind(kIp, 0) == 0) prefix = std::char_traits<char>::length(kIp);
    else if (uri.rfind(kLocal, 0) == 0) prefix = std::char_traits<char>::length(kLocal);
    else return false;

    const std::size_t slash = uri.find('/', prefix);
    const std::string port = uri.substr(prefix, slash == std::string::npos ? std::string::npos
                                                                           : slash - prefix);
    if (port.empty()) return false;
    if (!std::all_of(port.begin(), port.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return false;
    }
    try {
        const int value = std::stoi(port);
        return value > 0 && value <= 65535;
    } catch (const std::exception&) {
        return false;
    }
}

void appendField(std::string& out, const char* name, const std::string& value) {
    out += name;
    out += '=';
    out += std::to_string(value.size());
    out += ':';
    out += value;
    out += '\n';
}

}  // namespace

bool isLoopbackRedirect(const std::string& uri) { return loopbackRedirect(uri); }

bool ProviderConfig::configured() const { return !placeholder(clientId); }

bool ProviderConfig::hasSecret() const { return !placeholder(clientSecret); }

bool IntegrationConfig::twitchReady() const { return twitch.configured(); }

bool IntegrationConfig::kickReady() const {
    return kick.configured() && kick.hasSecret() && isLoopbackRedirect(kick.redirectUri);
}

bool IntegrationConfig::credentialsReady() const {
    if (!verified) return false;
    const bool tw = twitch.configured();
    const bool kk = kick.configured();
    if (!tw && !kk) return false;
    // Si el usuario empezó a rellenar una plataforma, debe terminarla.
    if (tw && !twitchReady()) return false;
    if (kk && !kickReady()) return false;
    // Un secreto de Kick sin Client ID también es una configuración a medias.
    if (!kk && kick.hasSecret()) return false;
    return true;
}

std::string credentialsFingerprint(const IntegrationConfig& cfg) {
    std::string canonical;
    appendField(canonical, "twitch.client_id", cfg.twitch.clientId);
    appendField(canonical, "twitch.client_secret", cfg.twitch.clientSecret);
    appendField(canonical, "twitch.redirect_uri", cfg.twitch.redirectUri);
    appendField(canonical, "kick.client_id", cfg.kick.clientId);
    appendField(canonical, "kick.client_secret", cfg.kick.clientSecret);
    appendField(canonical, "kick.redirect_uri", cfg.kick.redirectUri);
    return sha256Hex(canonical);
}

void markVerified(IntegrationConfig& cfg) {
    cfg.credentialHash = credentialsFingerprint(cfg);
    cfg.verified = true;
}

IntegrationConfig loadIntegrationConfig(const std::filesystem::path& file) {
    IntegrationConfig cfg;
    cfg.kick.redirectUri = "http://127.0.0.1:7512/callback";

    bool storedVerified = false;
    std::string storedHash;

    std::ifstream in(file, std::ios::binary);
    if (in) {
        try {
            nlohmann::json root;
            in >> root;
            if (root.contains("twitch") && root["twitch"].is_object()) {
                const auto& t = root["twitch"];
                cfg.twitch.clientId = readString(t, "client_id");
                cfg.twitch.clientSecret = readString(t, "client_secret");
                cfg.twitch.redirectUri = readString(t, "redirect_uri");
            }
            if (root.contains("kick") && root["kick"].is_object()) {
                const auto& k = root["kick"];
                cfg.kick.clientId = readString(k, "client_id");
                cfg.kick.clientSecret = readString(k, "client_secret");
                const std::string redirect = readString(k, "redirect_uri");
                if (!redirect.empty()) cfg.kick.redirectUri = redirect;
            }
            storedVerified = readBool(root, "verified");
            storedHash = readString(root, "credential_hash");
        } catch (const std::exception&) {
            // Una configuración inválida deja las integraciones deshabilitadas sin
            // impedir que la aplicación arranque.
            cfg = IntegrationConfig{};
            cfg.kick.redirectUri = "http://127.0.0.1:7512/callback";
            storedVerified = false;
            storedHash.clear();
        }
    }

    if (const std::string v = env("SP_TWITCH_CLIENT_ID"); !v.empty()) cfg.twitch.clientId = v;
    if (const std::string v = env("SP_TWITCH_CLIENT_SECRET"); !v.empty()) cfg.twitch.clientSecret = v;
    if (const std::string v = env("SP_KICK_CLIENT_ID"); !v.empty()) cfg.kick.clientId = v;
    if (const std::string v = env("SP_KICK_CLIENT_SECRET"); !v.empty()) cfg.kick.clientSecret = v;
    if (const std::string v = env("SP_KICK_REDIRECT_URI"); !v.empty()) cfg.kick.redirectUri = v;

    cfg.credentialHash = credentialsFingerprint(cfg);
    cfg.verified = storedVerified && !storedHash.empty() && storedHash == cfg.credentialHash &&
                   (cfg.twitch.configured() || cfg.kick.configured());
    return cfg;
}

bool saveIntegrationConfig(const std::filesystem::path& file, const IntegrationConfig& cfg) {
    const std::string fingerprint = credentialsFingerprint(cfg);
    // Nunca se guarda una marca de verificación que no corresponda a las
    // credenciales que se están escribiendo.
    const bool verified = cfg.verified && !cfg.credentialHash.empty() &&
                          cfg.credentialHash == fingerprint &&
                          (cfg.twitch.configured() || cfg.kick.configured());

    nlohmann::json root;
    root["twitch"] = {{"client_id", cfg.twitch.clientId},
                      {"client_secret", cfg.twitch.clientSecret},
                      {"redirect_uri", cfg.twitch.redirectUri}};
    root["kick"] = {{"client_id", cfg.kick.clientId},
                    {"client_secret", cfg.kick.clientSecret},
                    {"redirect_uri", cfg.kick.redirectUri}};
    root["verified"] = verified;
    root["credential_hash"] = verified ? fingerprint : std::string();
    return writePrivateFile(file, root.dump(2) + "\n");
}

}  // namespace sp::integrations
