#include "core/integrations/kick_client.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/integrations/api_util.h"
#include "core/integrations/crypto.h"
#include "core/integrations/http.h"
#include "core/integrations/url.h"
#include "core/text.h"

namespace sp::integrations {
namespace {

using nlohmann::json;

constexpr const char* kKickApi = "https://api.kick.com/public/v1";
constexpr const char* kKickApiV2 = "https://api.kick.com/public/v2";
constexpr const char* kKickId = "https://id.kick.com/oauth";
constexpr const char* kScope = "user:read channel:read channel:write";

struct RedirectParts {
    bool valid = false;
    std::string scheme;
    std::string host;
    std::string path;
    unsigned short port = 0;
};

RedirectParts parseRedirect(const std::string& uri) {
    RedirectParts p;
    const auto schemeEnd = uri.find("://");
    if (schemeEnd == std::string::npos) return p;
    p.scheme = uri.substr(0, schemeEnd);
    const std::string rest = uri.substr(schemeEnd + 3);
    const auto pathPos = rest.find('/');
    std::string authority = pathPos == std::string::npos ? rest : rest.substr(0, pathPos);
    p.path = pathPos == std::string::npos ? "/" : rest.substr(pathPos);

    const auto colon = authority.rfind(':');
    if (colon != std::string::npos) {
        p.host = authority.substr(0, colon);
        try {
            const int port = std::stoi(authority.substr(colon + 1));
            if (port > 0 && port <= 65535) p.port = static_cast<unsigned short>(port);
        } catch (const std::exception&) {
            return p;
        }
    } else {
        p.host = authority;
        p.port = p.scheme == "https" ? 443 : 80;
    }
    // El flujo de escritorio usa un servidor HTTP local: solo http:// en loopback.
    const bool loopbackHost = p.host == "127.0.0.1" || p.host == "localhost";
    p.valid = p.scheme == "http" && loopbackHost && p.port != 0;
    return p;
}

std::string queryParam(const std::string& target, const std::string& key) {
    const auto q = target.find('?');
    if (q == std::string::npos) return {};
    std::size_t start = q + 1;
    while (start <= target.size()) {
        const auto amp = target.find('&', start);
        const std::string pair = target.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        const auto eq = pair.find('=');
        if (eq != std::string::npos && urlDecode(pair.substr(0, eq)) == key)
            return urlDecode(pair.substr(eq + 1));
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return {};
}

std::string scopeFromJson(const json& j) {
    const auto it = j.find("scope");
    if (it == j.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (it->is_array()) {
        std::string out;
        for (const auto& s : *it) {
            if (!s.is_string()) continue;
            if (!out.empty()) out += ' ';
            out += s.get<std::string>();
        }
        return out;
    }
    return {};
}

bool parseTokenJson(const json& input, TokenSet& out) {
    json j = input;
    if (j.contains("data") && j["data"].is_object()) j = j["data"];
    const auto at = j.find("access_token");
    if (at == j.end() || !at->is_string()) return false;
    out.accessToken = at->get<std::string>();
    const auto rt = j.find("refresh_token");
    out.refreshToken = (rt != j.end() && rt->is_string()) ? rt->get<std::string>() : std::string();
    out.scope = scopeFromJson(j);
    std::int64_t expiresIn = 0;
    const auto ex = j.find("expires_in");
    if (ex != j.end() && ex->is_number_integer()) expiresIn = ex->get<std::int64_t>();
    out.expiresAt = expiryFromNow(expiresIn);
    return true;
}

std::string idString(const json& j) {
    if (j.is_string()) return j.get<std::string>();
    if (j.is_number_integer()) return std::to_string(j.get<std::int64_t>());
    return {};
}

json idJson(const std::string& id) {
    if (id.empty()) return json(nullptr);
    const bool numeric = std::all_of(id.begin(), id.end(),
                                     [](unsigned char c) { return std::isdigit(c) != 0; });
    if (numeric) {
        try {
            return json(std::stoll(id));
        } catch (const std::exception&) {
        }
    }
    return json(id);
}

}  // namespace

KickClient::KickClient(ProviderConfig config, IPlatformServices& services, ITokenStore& tokens)
    : config_(std::move(config)), services_(services), store_(tokens) {}

KickClient::~KickClient() {
    if (loopback_) loopback_->stop();
}

Capabilities KickClient::capabilities() const {
    Capabilities c;
    c.setTitle = true;
    c.setCategory = true;
    c.readTags = true;
    c.writeTags = true;
    c.requiresClientSecret = true;
    c.maxTitleLen = 100;
    c.maxTags = 10;
    c.maxTagLen = 25;
    return c;
}

bool KickClient::configured() const {
    const RedirectParts r = parseRedirect(config_.redirectUri);
    return !config_.clientId.empty() && config_.clientId.rfind("GENERIC_", 0) != 0 &&
           !config_.clientSecret.empty() && config_.clientSecret.rfind("GENERIC_", 0) != 0 && r.valid;
}

std::string KickClient::configurationProblem() const {
    if (config_.clientId.empty() || config_.clientId.rfind("GENERIC_", 0) == 0)
        return "Falta kick.client_id en integrations.json (o SP_KICK_CLIENT_ID).";
    if (config_.clientSecret.empty() || config_.clientSecret.rfind("GENERIC_", 0) == 0)
        return "Kick exige client_secret. Añádelo en integrations.json (o SP_KICK_CLIENT_SECRET).";
    if (!parseRedirect(config_.redirectUri).valid)
        return "kick.redirect_uri no es una URL válida (ej: http://127.0.0.1:7512/callback).";
    return {};
}

void KickClient::failConnect(const ConnectCallback& callback, const Result& result) {
    if (loopback_) loopback_->stop();
    state_ = AuthState::Error;
    ConnectResult r;
    r.result = result;
    callback(r);
}

void KickClient::connect(ConnectCallback callback) {
    if (!configured()) {
        ConnectResult r;
        r.result = Result::failure(Error::NotConfigured, configurationProblem());
        callback(r);
        return;
    }
    if (state_ == AuthState::Connected && tokens_.valid()) {
        ConnectResult r;
        r.account = account_;
        callback(r);
        return;
    }
    beginAuthorize(std::move(callback));
}

void KickClient::beginAuthorize(ConnectCallback callback) {
    const RedirectParts redirect = parseRedirect(config_.redirectUri);
    const int gen = ++generation_;
    state_ = AuthState::Connecting;
    codeVerifier_ = randomBase64Url(64);
    pendingState_ = randomBase64Url(32);
    effectiveRedirect_ = config_.redirectUri;

    if (loopback_) loopback_->stop();
    loopback_ = services_.makeLoopback();
    if (!loopback_) {
        failConnect(callback, Result::failure(Error::Network, "Kick: no se pudo crear el servidor local"));
        return;
    }
    const std::string expectedPath = loopbackPath_ = redirect.path;
    if (!loopback_->start(redirect.port,
                          [this, gen, callback, expectedPath](std::string target) mutable {
                              if (gen != generation_) return;
                              if (target.compare(0, expectedPath.size(), expectedPath) != 0) return;
                              handleLoopback(std::move(target), callback, gen);
                          },
                          [this, gen, callback](std::string error) mutable {
                              if (gen != generation_) return;
                              failConnect(callback, Result::failure(Error::Network, error));
                          })) {
        failConnect(callback,
                    Result::failure(Error::Network,
                                    "Kick: no se pudo escuchar en " + redirect.host + ":" +
                                        std::to_string(redirect.port) +
                                        ". Cambia el puerto en integrations.json y en la app de Kick."));
        return;
    }

    std::string url = std::string(kKickId) + "/authorize?response_type=code";
    url += "&client_id=" + urlEncode(config_.clientId);
    // Workaround documentado por Kick: si el redirect usa 127.0.0.1, el parámetro
    // señuelo debe ir ANTES de redirect_uri para que NextJS no lo reescriba.
    if (redirect.host == "127.0.0.1") url += "&redirect=127.0.0.1";
    url += "&redirect_uri=" + urlEncode(effectiveRedirect_);
    url += "&scope=" + urlEncode(kScope);
    url += "&code_challenge=" + urlEncode(sha256Base64Url(codeVerifier_));
    url += "&code_challenge_method=S256";
    url += "&state=" + urlEncode(pendingState_);

    services_.browser().openUrl(url);
    ConnectResult pending;
    pending.pending = true;
    pending.verificationUri = url;
    callback(pending);

    // Corta la espera si el usuario nunca vuelve.
    services_.scheduleAfter(180000, [this, gen, callback]() mutable {
        if (gen != generation_ || state_ != AuthState::Connecting) return;
        failConnect(callback, Result::failure(Error::Cancelled, "Kick: tiempo de autorización agotado"));
    });
}

void KickClient::handleLoopback(std::string target, ConnectCallback callback, int gen) {
    if (gen != generation_) return;
    const std::string error = queryParam(target, "error");
    const std::string state = queryParam(target, "state");
    const std::string code = queryParam(target, "code");
    if (code.empty() && error.empty()) return;  // favicon u otra petición del navegador
    if (!error.empty()) {
        failConnect(callback, Result::failure(Error::Unauthorized, "Kick: " + error));
        return;
    }
    if (state != pendingState_) {
        failConnect(callback, Result::failure(Error::Unauthorized, "Kick: state de OAuth inválido"));
        return;
    }
    if (loopback_) loopback_->stop();
    exchangeCode(code, std::move(callback), gen);
}

void KickClient::exchangeCode(std::string code, ConnectCallback callback, int gen) {
    HttpRequest req;
    req.method = "POST";
    req.url = std::string(kKickId) + "/token";
    req.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    req.body = formEncode({{"grant_type", "authorization_code"},
                           {"client_id", config_.clientId},
                           {"client_secret", config_.clientSecret},
                           {"redirect_uri", effectiveRedirect_},
                           {"code_verifier", codeVerifier_},
                           {"code", std::move(code)}});
    services_.http().send(std::move(req), [this, gen, callback](HttpResponse response) {
        if (gen != generation_) return;
        Result result = httpResult(response, "Kick");
        if (!result.ok()) {
            failConnect(callback, result);
            return;
        }
        try {
            const json j = json::parse(response.body);
            TokenSet t;
            if (!parseTokenJson(j, t)) {
                failConnect(callback, Result::failure(Error::Parse, "Kick: token inválido"));
                return;
            }
            if (t.scope.empty()) t.scope = kScope;
            tokens_ = t;
            store_.save(Platform::Kick, tokens_);
            fetchMe([this, callback](const Result& r) {
                ConnectResult done;
                done.result = r;
                done.account = account_;
                state_ = r.ok() ? AuthState::Connected : AuthState::Error;
                callback(done);
            });
        } catch (const std::exception&) {
            failConnect(callback, Result::failure(Error::Parse, "Kick: JSON de token inválido"));
        }
    });
}

void KickClient::refresh(VoidCallback callback) {
    if (tokens_.refreshToken.empty()) {
        callback(Result::failure(Error::NotConnected, "Kick: no hay refresh token"));
        return;
    }
    refreshWaiters_.push_back(std::move(callback));
    if (refreshing_) return;
    refreshing_ = true;

    HttpRequest req;
    req.method = "POST";
    req.url = std::string(kKickId) + "/token";
    req.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    req.body = formEncode({{"grant_type", "refresh_token"},
                           {"client_id", config_.clientId},
                           {"client_secret", config_.clientSecret},
                           {"refresh_token", tokens_.refreshToken}});
    services_.http().send(std::move(req), [this](HttpResponse response) {
        Result result = httpResult(response, "Kick");
        if (result.ok()) {
            try {
                const json j = json::parse(response.body);
                TokenSet parsed;
                if (parseTokenJson(j, parsed)) {
                    parsed.refreshToken = parsed.refreshToken.empty() ? tokens_.refreshToken : parsed.refreshToken;
                    parsed.scope = parsed.scope.empty() ? tokens_.scope : parsed.scope;
                    tokens_ = parsed;
                    store_.save(Platform::Kick, tokens_);
                } else {
                    result = Result::failure(Error::Parse, "Kick: refresh token inválido");
                }
            } catch (const std::exception&) {
                result = Result::failure(Error::Parse, "Kick: JSON de refresh inválido");
            }
        }
        if (!result.ok() && (result.error == Error::Unauthorized || result.error == Error::InvalidRequest)) {
            tokens_ = TokenSet{};
            store_.clear(Platform::Kick);
            account_ = Account{};
            state_ = AuthState::Disconnected;
        }
        refreshing_ = false;
        std::vector<VoidCallback> waiters;
        waiters.swap(refreshWaiters_);
        for (auto& w : waiters) w(result);
    });
}

void KickClient::fetchMe(VoidCallback callback) {
    HttpRequest req;
    req.url = std::string(kKickApi) + "/users";
    api(std::move(req), [this, callback](const HttpResponse& response, const Result& result) {
        if (!result.ok()) {
            callback(result);
            return;
        }
        try {
            const json j = json::parse(response.body);
            json u;
            const auto data = j.find("data");
            if (data != j.end() && data->is_array() && !data->empty()) u = data->front();
            else if (data != j.end() && data->is_object()) u = *data;
            if (!u.is_object()) {
                callback(Result::failure(Error::Parse, "Kick: usuario no encontrado"));
                return;
            }
            account_ = Account{};
            account_.platform = Platform::Kick;
            if (u.contains("user_id")) account_.userId = idString(u["user_id"]);
            if (u.contains("id")) account_.userId = idString(u["id"]);
            if (u.contains("name") && u["name"].is_string()) account_.login = u["name"].get<std::string>();
            if (u.contains("slug") && u["slug"].is_string()) account_.login = u["slug"].get<std::string>();
            if (u.contains("profile_picture") && u["profile_picture"].is_string())
                account_.avatarUrl = u["profile_picture"].get<std::string>();
            account_.displayName = account_.login.empty() ? account_.userId : account_.login;
            if (account_.userId.empty()) {
                callback(Result::failure(Error::Parse, "Kick: respuesta de usuario inválida"));
                return;
            }
            state_ = AuthState::Connected;
            callback(Result::success());
        } catch (const std::exception&) {
            callback(Result::failure(Error::Parse, "Kick: JSON de usuario inválido"));
        }
    });
}

void KickClient::restore(AccountCallback callback) {
    if (!configured()) {
        callback(Result::failure(Error::NotConfigured, configurationProblem()), Account{});
        return;
    }
    TokenSet stored;
    if (!store_.load(Platform::Kick, stored)) {
        state_ = AuthState::Disconnected;
        callback(Result::failure(Error::NotConnected, "Kick: sin sesión guardada"), Account{});
        return;
    }
    tokens_ = stored;
    state_ = AuthState::Connecting;
    if (tokens_.expiresSoon(nowEpochSeconds())) {
        refresh([this, callback](const Result& result) {
            if (!result.ok()) {
                state_ = AuthState::Disconnected;
                callback(result, Account{});
                return;
            }
            fetchMe([this, callback](const Result& r) {
                if (!r.ok()) state_ = AuthState::Error;
                callback(r, account_);
            });
        });
        return;
    }
    fetchMe([this, callback](const Result& r) {
        if (!r.ok()) state_ = AuthState::Error;
        callback(r, account_);
    });
}

void KickClient::api(HttpRequest request, ApiCallback callback, bool allowRefresh) {
    if (!tokens_.valid()) {
        callback(HttpResponse{}, Result::failure(Error::NotConnected, "Kick: no conectado"));
        return;
    }
    if (tokens_.expiresSoon(nowEpochSeconds())) {
        const int gen = generation_;
        refresh([this, gen, request = std::move(request), callback, allowRefresh](const Result& result) mutable {
            if (gen != generation_) return;
            if (!result.ok()) {
                callback(HttpResponse{}, result);
                return;
            }
            api(std::move(request), std::move(callback), allowRefresh);
        });
        return;
    }

    const HttpRequest retryBase = request;
    request.headers.emplace_back("Authorization", "Bearer " + tokens_.accessToken);
    services_.http().send(
        std::move(request), [this, callback, allowRefresh, retryBase](HttpResponse response) mutable {
            const Result result = httpResult(response, "Kick");
            if (result.error == Error::Unauthorized && allowRefresh && !tokens_.refreshToken.empty()) {
                const int gen = generation_;
                refresh([this, gen, retryBase, callback](const Result& refreshResult) mutable {
                    if (gen != generation_) return;
                    if (!refreshResult.ok()) {
                        callback(HttpResponse{}, refreshResult);
                        return;
                    }
                    api(std::move(retryBase), std::move(callback), false);
                });
                return;
            }
            callback(response, result);
        });
}

void KickClient::disconnect() {
    ++generation_;
    if (loopback_) loopback_->stop();
    tokens_ = TokenSet{};
    store_.clear(Platform::Kick);
    account_ = Account{};
    state_ = AuthState::Disconnected;
}

void KickClient::cancel() {
    ++generation_;
    if (loopback_) loopback_->stop();
    if (state_ == AuthState::Connecting) state_ = AuthState::Disconnected;
}

void KickClient::fetchChannel(ChannelCallback callback) {
    if (!tokens_.valid() || account_.userId.empty()) {
        callback(Result::failure(Error::NotConnected, "Kick: no conectado"), ChannelSnapshot{});
        return;
    }
    HttpRequest req;
    req.url = std::string(kKickApi) + "/channels?broadcaster_user_id=" + urlEncode(account_.userId);
    api(std::move(req), [callback](const HttpResponse& response, const Result& result) {
        if (!result.ok()) {
            callback(result, ChannelSnapshot{});
            return;
        }
        try {
            const json j = json::parse(response.body);
            const auto data = j.find("data");
            if (data == j.end() || !data->is_array() || data->empty()) {
                callback(Result::failure(Error::Parse, "Kick: canal no encontrado"), ChannelSnapshot{});
                return;
            }
            const auto& c = data->front();
            ChannelSnapshot snap;
            if (c.contains("stream_title") && c["stream_title"].is_string())
                snap.title = c["stream_title"].get<std::string>();
            if (c.contains("category") && c["category"].is_object()) {
                const auto& cat = c["category"];
                if (cat.contains("id")) snap.categoryId = idString(cat["id"]);
                if (cat.contains("name") && cat["name"].is_string())
                    snap.categoryName = cat["name"].get<std::string>();
            }
            // Kick expone las etiquetas dentro del objeto "stream".
            if (c.contains("stream") && c["stream"].is_object()) {
                const auto& st = c["stream"];
                if (st.contains("custom_tags") && st["custom_tags"].is_array()) {
                    snap.tagsKnown = true;
                    for (const auto& t : st["custom_tags"])
                        if (t.is_string()) snap.tags.push_back(t.get<std::string>());
                }
            }
            if (!snap.tagsKnown && c.contains("custom_tags") && c["custom_tags"].is_array()) {
                snap.tagsKnown = true;
                for (const auto& t : c["custom_tags"])
                    if (t.is_string()) snap.tags.push_back(t.get<std::string>());
            }
            callback(Result::success(), snap);
        } catch (const std::exception&) {
            callback(Result::failure(Error::Parse, "Kick: JSON de canal inválido"), ChannelSnapshot{});
        }
    });
}

void KickClient::searchCategories(std::string query, std::string cursor,
                                  CategoriesCallback callback) {
    auto parse = [callback](const HttpResponse& response, const Result& result) {
        if (!result.ok()) {
            callback(result, CategoryPage{});
            return;
        }
        try {
            const json j = json::parse(response.body);
            CategoryPage page;
            if (j.contains("data") && j["data"].is_array()) {
                for (const auto& c : j["data"]) {
                    if (!c.is_object()) continue;
                    Category cat;
                    if (c.contains("id")) cat.id = idString(c["id"]);
                    if (c.contains("name") && c["name"].is_string())
                        cat.name = c["name"].get<std::string>();
                    if (c.contains("thumbnail") && c["thumbnail"].is_string())
                        cat.boxArtUrl = c["thumbnail"].get<std::string>();
                    if (!cat.id.empty()) page.items.push_back(std::move(cat));
                }
            }
            if (j.contains("pagination") && j["pagination"].is_object()) {
                const auto& p = j["pagination"];
                if (p.contains("next_cursor") && p["next_cursor"].is_string())
                    page.cursor = p["next_cursor"].get<std::string>();
                else if (p.contains("cursor") && p["cursor"].is_string())
                    page.cursor = p["cursor"].get<std::string>();
            } else if (j.contains("next_cursor") && j["next_cursor"].is_string()) {
                page.cursor = j["next_cursor"].get<std::string>();
            }
            callback(Result::success(), page);
        } catch (const std::exception&) {
            callback(Result::failure(Error::Parse, "Kick: JSON de categorías inválido"), CategoryPage{});
        }
    };

    HttpRequest req;
    req.url = std::string(kKickApiV2) + "/categories?limit=20";
    // La API v2 exige nombres de 3 a 100 caracteres; con menos se listan las
    // categorías destacadas para no provocar un 400.
    if (utf8Length(query) >= 3) req.url += "&name=" + urlEncode(query);
    if (!cursor.empty()) req.url += "&cursor=" + urlEncode(cursor);

    api(std::move(req), parse);
}

void KickClient::publish(const PublishRequest& request, PublishCallback callback) {
    if (!tokens_.valid() || account_.userId.empty()) {
        callback(Result::failure(Error::NotConnected, "Kick: no conectado"));
        return;
    }
    Result valid = validatePublish(capabilities(), request, "Kick");
    if (!valid.ok()) {
        callback(valid);
        return;
    }

    json body = json::object();
    if (request.applyTitle) body["stream_title"] = request.title;
    if (request.applyCategory) body["category_id"] = idJson(request.categoryId);
    if (request.applyTags) {
        json tags = json::array();
        for (const auto& t : request.tags) tags.push_back(t);
        body["custom_tags"] = std::move(tags);
    }

    HttpRequest req;
    req.method = "PATCH";
    req.url = std::string(kKickApi) + "/channels";
    req.body = body.dump();
    req.headers.emplace_back("Content-Type", "application/json");
    api(std::move(req), [callback](const HttpResponse&, const Result& result) {
        if (!result.ok()) {
            callback(result);
            return;
        }
        callback(Result::success());
    });
}

}  // namespace sp::integrations
