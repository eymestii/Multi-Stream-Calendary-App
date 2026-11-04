#include "core/integrations/token_store.h"

#include <chrono>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/integrations/secure_file.h"

namespace sp::integrations {
namespace {

using nlohmann::json;

bool readRoot(const std::filesystem::path& file, json& root) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    try {
        in >> root;
        return root.is_object();
    } catch (const std::exception&) {
        return false;
    }
}

bool writeRoot(const std::filesystem::path& file, const json& root) {
    return writePrivateFile(file, root.dump(2) + "\n");
}

TokenSet parseToken(const json& j) {
    TokenSet t;
    if (j.contains("access_token") && j["access_token"].is_string())
        t.accessToken = j["access_token"].get<std::string>();
    if (j.contains("refresh_token") && j["refresh_token"].is_string())
        t.refreshToken = j["refresh_token"].get<std::string>();
    if (j.contains("scope") && j["scope"].is_string()) t.scope = j["scope"].get<std::string>();
    if (j.contains("expires_at") && j["expires_at"].is_number_integer())
        t.expiresAt = j["expires_at"].get<std::int64_t>();
    return t;
}

json toJson(const TokenSet& t) {
    return json{{"access_token", t.accessToken},
                {"refresh_token", t.refreshToken},
                {"scope", t.scope},
                {"expires_at", t.expiresAt}};
}

}  // namespace

std::int64_t nowEpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool FileTokenStore::load(Platform platform, TokenSet& out) const {
    json root;
    if (!readRoot(file_, root)) return false;
    const auto it = root.find(platformId(platform));
    if (it == root.end() || !it->is_object()) return false;
    out = parseToken(*it);
    return out.valid();
}

bool FileTokenStore::save(Platform platform, const TokenSet& tokens) {
    json root;
    readRoot(file_, root);
    if (!root.is_object()) root = json::object();
    root[platformId(platform)] = toJson(tokens);
    return writeRoot(file_, root);
}

bool FileTokenStore::clear(Platform platform) {
    json root;
    if (!readRoot(file_, root)) return true;
    root.erase(platformId(platform));
    return writeRoot(file_, root);
}

}  // namespace sp::integrations
