// Codificación para query strings y formularios application/x-www-form-urlencoded.
#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sp::integrations {

std::string urlEncode(std::string_view value);
std::string urlDecode(std::string_view value);

// Construye "a=1&b=2" codificando ambos lados.
std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields);

}  // namespace sp::integrations
