#include "core/integrations/credentials.h"

#include <memory>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/integrations/api_util.h"
#include "core/integrations/url.h"

namespace sp::integrations {
namespace {

using nlohmann::json;

constexpr const char* kTwitchDeviceUrl = "https://id.twitch.tv/oauth2/device";
constexpr const char* kKickTokenUrl = "https://id.kick.com/oauth/token";
constexpr const char* kTwitchScope = "channel:manage:broadcast";

bool hasString(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() && !it->get<std::string>().empty();
}

Result parseSuccess(const HttpResponse& response, const char* platform, const char* requiredKey) {
    if (!response.error.empty()) {
        return Result::failure(Error::Network, std::string(platform) + ": " + response.error);
    }
    Result result = httpResult(response, platform);
    if (!result.ok()) return result;
    try {
        json body = json::parse(response.body);
        if (body.contains("data") && body["data"].is_object()) body = body["data"];
        if (hasString(body, requiredKey)) return Result::success();
    } catch (const std::exception&) {
    }
    return Result::failure(Error::Parse,
                           std::string(platform) + ": la respuesta de verificación no es válida");
}

struct Join {
    CredentialVerification result;
    bool twitchNeeded = false;
    bool kickNeeded = false;
    bool twitchDone = false;
    bool kickDone = false;
    bool finished = false;
    CredentialVerificationCallback callback;

    void maybeFinish() {
        if (finished) return;
        if (twitchNeeded && !twitchDone) return;
        if (kickNeeded && !kickDone) return;
        finished = true;
        auto done = std::move(callback);
        if (done) done(result);
    }
};

void verifyTwitch(const IntegrationConfig& cfg, IHttpClient& http,
                  const std::shared_ptr<Join>& join) {
    HttpRequest request;
    request.method = "POST";
    request.url = kTwitchDeviceUrl;
    request.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    request.body = formEncode({{"client_id", cfg.twitch.clientId}, {"scopes", kTwitchScope}});
    http.send(std::move(request), [join](HttpResponse response) {
        join->result.twitch = parseSuccess(response, "Twitch", "device_code");
        join->twitchDone = true;
        join->maybeFinish();
    });
}

void verifyKick(const IntegrationConfig& cfg, IHttpClient& http, const std::shared_ptr<Join>& join) {
    HttpRequest request;
    request.method = "POST";
    request.url = kKickTokenUrl;
    request.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    request.body = formEncode({{"grant_type", "client_credentials"},
                               {"client_id", cfg.kick.clientId},
                               {"client_secret", cfg.kick.clientSecret}});
    http.send(std::move(request), [join](HttpResponse response) {
        join->result.kick = parseSuccess(response, "Kick", "access_token");
        join->kickDone = true;
        join->maybeFinish();
    });
}

}  // namespace

void verifyCredentials(const IntegrationConfig& cfg, IHttpClient& http,
                       CredentialVerificationCallback callback) {
    auto join = std::make_shared<Join>();
    join->callback = std::move(callback);

    join->twitchNeeded = cfg.twitch.configured();
    join->kickNeeded = cfg.kick.configured();

    if (!join->twitchNeeded && !join->kickNeeded) {
        const Result missing = Result::failure(
            Error::NotConfigured,
            "Introduce las credenciales de al menos una plataforma (Twitch o Kick).");
        join->result.twitch = missing;
        join->result.kick = missing;
        join->finished = true;
        auto done = std::move(join->callback);
        if (done) done(join->result);
        return;
    }

    if (!join->twitchNeeded) {
        join->result.twitch = Result::success();
        join->twitchDone = true;
    }
    if (!join->kickNeeded) {
        join->result.kick = Result::success();
        join->kickDone = true;
    }

    // Kick necesita el secreto para el intercambio client_credentials.
    if (join->kickNeeded && !cfg.kick.hasSecret()) {
        join->result.kick = Result::failure(Error::NotConfigured,
                                            "Kick: falta el Client Secret de la aplicación.");
        join->kickDone = true;
    }

    if (join->twitchNeeded) verifyTwitch(cfg, http, join);
    if (join->kickNeeded && join->result.kick.ok()) verifyKick(cfg, http, join);

    join->maybeFinish();
}

}  // namespace sp::integrations
