#include "core/integrations/url.h"

#include <cstdio>

namespace sp::integrations {

std::string urlEncode(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (const unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            // %20 es válido tanto en query como en formularios y evita que un
            // servidor interprete '+' como carácter literal.
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0f];
        }
    }
    return out;
}

std::string urlDecode(std::string_view value) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            out += ' ';
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = hex(value[i + 1]), lo = hex(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
            } else {
                out += value[i];
            }
        } else {
            out += value[i];
        }
    }
    return out;
}

std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string out;
    for (const auto& [k, v] : fields) {
        if (!out.empty()) out += '&';
        out += urlEncode(k);
        out += '=';
        out += urlEncode(v);
    }
    return out;
}

}  // namespace sp::integrations
