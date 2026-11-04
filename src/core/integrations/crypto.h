// Utilidades criptográficas mínimas para OAuth: SHA-256, base64url y aleatoriedad.
// Portable y sin dependencias externas (necesario para PKCE y state).
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace sp::integrations {

std::string sha256(std::string_view data);  // 32 bytes binarios
std::string sha256Hex(std::string_view data);  // 64 caracteres hex (huellas)
std::string base64Url(const unsigned char* data, std::size_t len);
std::string sha256Base64Url(std::string_view data);  // code_challenge S256
std::string randomBase64Url(std::size_t bytes);      // code_verifier / state

}  // namespace sp::integrations
