// Almacenamiento de tokens OAuth. El core solo conoce esta interfaz.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

#include "core/integrations/integration_types.h"

namespace sp::integrations {

struct TokenSet {
    std::string accessToken;
    std::string refreshToken;
    std::string scope;
    std::int64_t expiresAt = 0;  // epoch en segundos

    bool valid() const { return !accessToken.empty(); }
    bool expiresSoon(std::int64_t nowEpoch, std::int64_t skewSeconds = 300) const {
        return expiresAt > 0 && expiresAt - skewSeconds <= nowEpoch;
    }
};

class ITokenStore {
public:
    virtual ~ITokenStore() = default;
    virtual bool load(Platform platform, TokenSet& out) const = 0;
    virtual bool save(Platform platform, const TokenSet& tokens) = 0;
    virtual bool clear(Platform platform) = 0;
};

// Archivo JSON con permisos 0600 en Unix. Escritura atómica (tmp + rename).
class FileTokenStore : public ITokenStore {
public:
    explicit FileTokenStore(std::filesystem::path file) : file_(std::move(file)) {}
    bool load(Platform platform, TokenSet& out) const override;
    bool save(Platform platform, const TokenSet& tokens) override;
    bool clear(Platform platform) override;

    const std::filesystem::path& file() const { return file_; }

private:
    std::filesystem::path file_;
};

std::int64_t nowEpochSeconds();

}  // namespace sp::integrations
