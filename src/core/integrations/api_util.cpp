#include "core/integrations/api_util.h"

#include <chrono>
#include <cstdio>
#include <ctime>

#include <nlohmann/json.hpp>

#include "core/text.h"

namespace sp::integrations {
namespace {

std::string extractMessage(const std::string& body) {
    if (body.empty()) return {};
    try {
        const auto j = nlohmann::json::parse(body);
        if (!j.is_object()) return {};
        for (const char* key : {"message", "error_description", "error", "detail"}) {
            const auto it = j.find(key);
            if (it != j.end() && it->is_string()) return it->get<std::string>();
            if (it != j.end() && it->is_object()) {
                const auto m = it->find("message");
                if (m != it->end() && m->is_string()) return m->get<std::string>();
            }
        }
    } catch (const std::exception&) {
    }
    return {};
}

int retryAfter(const HttpResponse& r) {
    const std::string v = r.header("Retry-After");
    if (v.empty()) return 0;
    try {
        return std::stoi(v);
    } catch (const std::exception&) {
        return 0;
    }
}

}  // namespace

Result httpResult(const HttpResponse& response, const char* platform) {
    if (!response.error.empty()) {
        return Result::failure(Error::Network, response.error);
    }
    if (response.status >= 200 && response.status < 300) return Result::success();

    Result r;
    r.httpStatus = response.status;
    r.message = extractMessage(response.body);
    if (r.message.empty()) r.message = "HTTP " + std::to_string(response.status);
    if (platform && *platform) r.message = std::string(platform) + ": " + r.message;

    if (response.status == 401) r.error = Error::Unauthorized;
    else if (response.status == 403) r.error = Error::Forbidden;
    else if (response.status == 429) {
        r.error = Error::RateLimited;
        r.retryAfterSeconds = retryAfter(response);
    } else if (response.status >= 500) r.error = Error::Server;
    else r.error = Error::InvalidRequest;
    return r;
}

Result validatePublish(const Capabilities& caps, const PublishRequest& request, const char* platform) {
    const std::string prefix = platform ? std::string(platform) + ": " : std::string();
    if (request.applyTitle) {
        const std::size_t len = utf8Length(request.title);
        if (request.title.empty()) return Result::failure(Error::InvalidRequest, prefix + "el título está vacío");
        if (caps.maxTitleLen && len > caps.maxTitleLen) {
            return Result::failure(Error::InvalidRequest,
                                   prefix + "el título supera el límite de " +
                                       std::to_string(caps.maxTitleLen) + " caracteres");
        }
    }
    if (request.applyCategory && request.categoryId.empty()) {
        return Result::failure(Error::InvalidRequest, prefix + "selecciona una categoría");
    }
    if (request.applyTags && caps.writeTags) {
        if (caps.maxTags && request.tags.size() > caps.maxTags) {
            return Result::failure(Error::InvalidRequest,
                                   prefix + "no se pueden usar más de " + std::to_string(caps.maxTags) +
                                       " etiquetas");
        }
        for (const std::string& tag : request.tags) {
            if (tag.empty()) return Result::failure(Error::InvalidRequest, prefix + "hay una etiqueta vacía");
            if (caps.maxTagLen && utf8Length(tag) > caps.maxTagLen) {
                return Result::failure(Error::InvalidRequest,
                                       prefix + "la etiqueta \"" + tag + "\" supera " +
                                           std::to_string(caps.maxTagLen) + " caracteres");
            }
        }
    } else if (request.applyTags && !caps.writeTags) {
        return Result::failure(Error::Unsupported, prefix + "esta plataforma no permite editar etiquetas");
    }
    return Result::success();
}

std::string nowIso8601() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::int64_t expiryFromNow(std::int64_t expiresInSeconds) {
    using namespace std::chrono;
    const auto now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    return expiresInSeconds > 0 ? now + expiresInSeconds : 0;
}

}  // namespace sp::integrations
