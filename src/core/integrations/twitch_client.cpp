#include "core/integrations/twitch_client.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/integrations/api_util.h"
#include "core/integrations/http.h"
#include "core/integrations/url.h"

namespace sp::integrations {
namespace {

using nlohmann::json;

constexpr const char* kTwitchApi = "https://api.twitch.tv/helix";
constexpr const char* kTwitchId = "https://id.twitch.tv/oauth2";
constexpr const char* kScope = "channel:manage:broadcast";

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

bool parseTokenJson(const json& j, TokenSet& out) {
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

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool containsError(const std::string& body, const char* code) {
    return lower(body).find(code) != std::string::npos;
}

}  // namespace

TwitchClient::TwitchClient(ProviderConfig config, IPlatformServices& services, ITokenStore& tokens)
    : config_(std::move(config)), services_(services), store_(tokens) {}

Capabilities TwitchClient::capabilities() const {
    Capabilities c;
    c.setTitle = true;
    c.setCategory = true;
    c.readTags = true;
    c.writeTags = true;
    c.requiresClientSecret = false;
    c.maxTitleLen = 140;
    c.maxTags = 10;
    c.maxTagLen = 25;
    return c;
}

std::string TwitchClient::configurationProblem() const {
    if (!config_.configured())
        return "Falta twitch.client_id en integrations.json (o SP_TWITCH_CLIENT_ID).";
    return {};
}

void TwitchClient::failConnect(const ConnectCallback& callback, const Result& result) {
    state_ = AuthState::Error;
    ConnectResult r;
    r.result = result;
    callback(r);
}

void TwitchClient::connect(ConnectCallback callback) {
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

    const int gen = ++generation_;
    state_ = AuthState::Connecting;
    deviceCode_.clear();
    verificationUri_.clear();
    pollInterval_ = 5;
    deviceExpiresAt_ = 0;

    HttpRequest req;
    req.method = "POST";
    req.url = std::string(kTwitchId) + "/device";
    req.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    req.body = formEncode({{"client_id", config_.clientId}, {"scopes", kScope}});
    services_.http().send(std::move(req), [this, gen, callback](HttpResponse response) {
        if (gen != generation_) return;
        Result result = httpResult(response, "Twitch");
        if (!result.ok()) {
            failConnect(callback, result);
            return;
        }
        try {
            const json j = json::parse(response.body);
            const auto dc = j.find("device_code");
            const auto uc = j.find("user_code");
            const auto vu = j.find("verification_uri");
            if (dc == j.end() || uc == j.end() || vu == j.end() || !dc->is_string() ||
                !uc->is_string() || !vu->is_string()) {
                failConnect(callback, Result::failure(Error::Parse, "Twitch: respuesta de dispositivo inválida"));
                return;
            }
            deviceCode_ = dc->get<std::string>();
            deviceScope_ = kScope;
            const std::string userCode = uc->get<std::string>();
            verificationUri_ = vu->get<std::string>();
            if (j.contains("interval") && j["interval"].is_number_integer())
                pollInterval_ = std::max(2, j["interval"].get<int>());
            if (j.contains("expires_in") && j["expires_in"].is_number_integer())
                deviceExpiresAt_ = expiryFromNow(j["expires_in"].get<std::int64_t>());
            services_.browser().openUrl(verificationUri_);

            ConnectResult pending;
            pending.pending = true;
            pending.userCode = userCode;
            pending.verificationUri = verificationUri_;
            callback(pending);
            services_.scheduleAfter(pollInterval_ * 1000, [this, gen, callback] {
                if (gen != generation_) return;
                pollDevice(callback);
            });
        } catch (const std::exception&) {
            failConnect(callback, Result::failure(Error::Parse, "Twitch: JSON de dispositivo inválido"));
        }
    });
}

void TwitchClient::pollDevice(ConnectCallback callback) {
    if (generation_ == 0) return;
    if (deviceExpiresAt_ > 0 && nowEpochSeconds() >= deviceExpiresAt_) {
        failConnect(callback, Result::failure(Error::Unauthorized, "Twitch: el código de activación caducó"));
        return;
    }

    const int gen = generation_;
    HttpRequest req;
    req.method = "POST";
    req.url = std::string(kTwitchId) + "/token";
    req.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    req.body = formEncode({{"client_id", config_.clientId},
                           {"scopes", deviceScope_},
                           {"device_code", deviceCode_},
                           {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}});
    services_.http().send(std::move(req), [this, gen, callback](HttpResponse response) {
        if (gen != generation_) return;

        if (response.status >= 200 && response.status < 300) {
            try {
                const json j = json::parse(response.body);
                TokenSet t;
                if (!parseTokenJson(j, t)) {
                    failConnect(callback, Result::failure(Error::Parse, "Twitch: token inválido"));
                    return;
                }
                if (t.scope.empty()) t.scope = kScope;
                tokens_ = t;
                store_.save(Platform::Twitch, tokens_);
                fetchMe([this, callback](const Result& result) {
                    ConnectResult done;
                    done.result = result;
                    done.account = account_;
                    if (result.ok()) state_ = AuthState::Connected;
                    else state_ = AuthState::Error;
                    callback(done);
                });
                return;
            } catch (const std::exception&) {
                failConnect(callback, Result::failure(Error::Parse, "Twitch: JSON de token inválido"));
                return;
            }
        }

        if (containsError(response.body, "authorization_pending")) {
            services_.scheduleAfter(pollInterval_ * 1000, [this, gen, callback] {
                if (gen != generation_) return;
                pollDevice(callback);
            });
            return;
        }
        if (containsError(response.body, "slow_down")) {
            pollInterval_ += 5;
            services_.scheduleAfter(pollInterval_ * 1000, [this, gen, callback] {
                if (gen != generation_) return;
                pollDevice(callback);
            });
            return;
        }
        Result result = httpResult(response, "Twitch");
        if (result.message.empty()) result.message = "Twitch: autorización cancelada o caducada";
        failConnect(callback, result);
    });
}

void TwitchClient::refresh(VoidCallback callback) {
    if (tokens_.refreshToken.empty()) {
        callback(Result::failure(Error::NotConnected, "Twitch: no hay refresh token"));
        return;
    }

    refreshWaiters_.push_back(std::move(callback));
    if (refreshing_) return;
    refreshing_ = true;

    HttpRequest req;
    req.method = "POST";
    req.url = std::string(kTwitchId) + "/token";
    req.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    std::vector<std::pair<std::string, std::string>> fields = {
        {"grant_type", "refresh_token"},
        {"refresh_token", tokens_.refreshToken},
        {"client_id", config_.clientId}};
    if (!config_.clientSecret.empty()) fields.emplace_back("client_secret", config_.clientSecret);
    req.body = formEncode(fields);

    services_.http().send(std::move(req), [this](HttpResponse response) {
        Result result = httpResult(response, "Twitch");
        if (result.ok()) {
            try {
                const json j = json::parse(response.body);
                TokenSet t = tokens_;
                TokenSet parsed;
                if (parseTokenJson(j, parsed)) {
                    t.accessToken = parsed.accessToken;
                    if (!parsed.refreshToken.empty()) t.refreshToken = parsed.refreshToken;
                    if (!parsed.scope.empty()) t.scope = parsed.scope;
                    t.expiresAt = parsed.expiresAt;
                    tokens_ = t;
                    store_.save(Platform::Twitch, tokens_);
                } else {
                    result = Result::failure(Error::Parse, "Twitch: refresh token inválido");
                }
            } catch (const std::exception&) {
                result = Result::failure(Error::Parse, "Twitch: JSON de refresh inválido");
            }
        }
        if (!result.ok() && (result.error == Error::Unauthorized || result.error == Error::InvalidRequest)) {
            tokens_ = TokenSet{};
            store_.clear(Platform::Twitch);
            account_ = Account{};
            state_ = AuthState::Disconnected;
        }
        refreshing_ = false;
        std::vector<VoidCallback> waiters;
        waiters.swap(refreshWaiters_);
        for (auto& w : waiters) w(result);
    });
}

void TwitchClient::fetchMe(VoidCallback callback) {
    HttpRequest req;
    req.url = std::string(kTwitchApi) + "/users";
    api(std::move(req), [this, callback](const HttpResponse& response, const Result& result) {
        if (!result.ok()) {
            callback(result);
            return;
        }
        try {
            const json j = json::parse(response.body);
            const auto data = j.find("data");
            if (data == j.end() || !data->is_array() || data->empty()) {
                callback(Result::failure(Error::Parse, "Twitch: usuario no encontrado"));
                return;
            }
            const auto& u = data->front();
            account_ = Account{};
            account_.platform = Platform::Twitch;
            if (u.contains("id") && u["id"].is_string()) account_.userId = u["id"].get<std::string>();
            if (u.contains("login") && u["login"].is_string()) account_.login = u["login"].get<std::string>();
            if (u.contains("display_name") && u["display_name"].is_string())
                account_.displayName = u["display_name"].get<std::string>();
            if (u.contains("profile_image_url") && u["profile_image_url"].is_string())
                account_.avatarUrl = u["profile_image_url"].get<std::string>();
            if (account_.userId.empty()) {
                callback(Result::failure(Error::Parse, "Twitch: respuesta de usuario inválida"));
                return;
            }
            state_ = AuthState::Connected;
            callback(Result::success());
        } catch (const std::exception&) {
            callback(Result::failure(Error::Parse, "Twitch: JSON de usuario inválido"));
        }
    });
}

void TwitchClient::restore(AccountCallback callback) {
    if (!configured()) {
        callback(Result::failure(Error::NotConfigured, configurationProblem()), Account{});
        return;
    }
    TokenSet stored;
    if (!store_.load(Platform::Twitch, stored)) {
        state_ = AuthState::Disconnected;
        callback(Result::failure(Error::NotConnected, "Twitch: sin sesión guardada"), Account{});
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

void TwitchClient::api(HttpRequest request, ApiCallback callback, bool allowRefresh) {
    if (!tokens_.valid()) {
        callback(HttpResponse{}, Result::failure(Error::NotConnected, "Twitch: no conectado"));
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
    request.headers.emplace_back("Client-Id", config_.clientId);
    request.headers.emplace_back("Authorization", "Bearer " + tokens_.accessToken);
    services_.http().send(
        std::move(request), [this, callback, allowRefresh, retryBase](HttpResponse response) mutable {
            const Result result = httpResult(response, "Twitch");
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

void TwitchClient::disconnect() {
    ++generation_;
    tokens_ = TokenSet{};
    store_.clear(Platform::Twitch);
    account_ = Account{};
    state_ = AuthState::Disconnected;
}

void TwitchClient::cancel() {
    ++generation_;
    if (state_ == AuthState::Connecting) state_ = AuthState::Disconnected;
    deviceCode_.clear();
}

void TwitchClient::fetchChannel(ChannelCallback callback) {
    if (!tokens_.valid() || account_.userId.empty()) {
        callback(Result::failure(Error::NotConnected, "Twitch: no conectado"), ChannelSnapshot{});
        return;
    }
    HttpRequest req;
    req.url = std::string(kTwitchApi) + "/channels?broadcaster_id=" + urlEncode(account_.userId);
    api(std::move(req), [callback](const HttpResponse& response, const Result& result) {
        if (!result.ok()) {
            callback(result, ChannelSnapshot{});
            return;
        }
        try {
            const json j = json::parse(response.body);
            const auto data = j.find("data");
            if (data == j.end() || !data->is_array() || data->empty()) {
                callback(Result::failure(Error::Parse, "Twitch: canal no encontrado"), ChannelSnapshot{});
                return;
            }
            const auto& c = data->front();
            ChannelSnapshot snap;
            if (c.contains("title") && c["title"].is_string()) snap.title = c["title"].get<std::string>();
            if (c.contains("game_id") && c["game_id"].is_string())
                snap.categoryId = c["game_id"].get<std::string>();
            if (c.contains("game_name") && c["game_name"].is_string())
                snap.categoryName = c["game_name"].get<std::string>();
            if (c.contains("tags") && c["tags"].is_array()) {
                snap.tagsKnown = true;
                for (const auto& t : c["tags"])
                    if (t.is_string()) snap.tags.push_back(t.get<std::string>());
            }
            callback(Result::success(), snap);
        } catch (const std::exception&) {
            callback(Result::failure(Error::Parse, "Twitch: JSON de canal inválido"), ChannelSnapshot{});
        }
    });
}

void TwitchClient::searchCategories(std::string query, std::string cursor,
                                    CategoriesCallback callback) {
    HttpRequest req;
    req.url = std::string(kTwitchApi) + (query.empty() ? "/games/top?" : "/search/categories?");
    req.url += "first=20";
    if (!query.empty()) req.url += "&query=" + urlEncode(query);
    if (!cursor.empty()) req.url += "&after=" + urlEncode(cursor);
    api(std::move(req), [callback](const HttpResponse& response, const Result& result) {
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
                    if (c.contains("id") && c["id"].is_string()) cat.id = c["id"].get<std::string>();
                    if (c.contains("name") && c["name"].is_string()) cat.name = c["name"].get<std::string>();
                    if (c.contains("box_art_url") && c["box_art_url"].is_string())
                        cat.boxArtUrl = c["box_art_url"].get<std::string>();
                    if (!cat.id.empty()) page.items.push_back(std::move(cat));
                }
            }
            if (j.contains("pagination") && j["pagination"].is_object()) {
                const auto& p = j["pagination"];
                if (p.contains("cursor") && p["cursor"].is_string())
                    page.cursor = p["cursor"].get<std::string>();
            }
            callback(Result::success(), page);
        } catch (const std::exception&) {
            callback(Result::failure(Error::Parse, "Twitch: JSON de categorías inválido"), CategoryPage{});
        }
    });
}

void TwitchClient::publish(const PublishRequest& request, PublishCallback callback) {
    if (!tokens_.valid() || account_.userId.empty()) {
        callback(Result::failure(Error::NotConnected, "Twitch: no conectado"));
        return;
    }
    Result valid = validatePublish(capabilities(), request, "Twitch");
    if (!valid.ok()) {
        callback(valid);
        return;
    }

    json body = json::object();
    if (request.applyTitle) body["title"] = request.title;
    if (request.applyCategory) body["game_id"] = request.categoryId;
    if (request.applyTags) body["tags"] = request.tags;

    HttpRequest req;
    req.method = "PATCH";
    // broadcaster_id va en la query; el cuerpo solo lleva los campos a cambiar.
    req.url = std::string(kTwitchApi) + "/channels?broadcaster_id=" + urlEncode(account_.userId);
    req.body = body.dump();
    req.headers.emplace_back("Content-Type", "application/json");
    api(std::move(req), [callback](const HttpResponse& response, const Result& result) {
        if (!result.ok()) {
            callback(result);
            return;
        }
        if (response.status < 200 || response.status >= 300) {
            callback(httpResult(response, "Twitch"));
            return;
        }
        callback(Result::success());
    });
}

}  // namespace sp::integrations
