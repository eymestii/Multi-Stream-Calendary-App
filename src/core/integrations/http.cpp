#include "core/integrations/http.h"

#include <algorithm>
#include <cctype>

namespace sp::integrations {

std::string HttpResponse::header(std::string_view name) const {
    for (const auto& [k, v] : headers) {
        if (k.size() != name.size()) continue;
        bool same = true;
        for (std::size_t i = 0; i < k.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(k[i])) !=
                std::tolower(static_cast<unsigned char>(name[i]))) {
                same = false;
                break;
            }
        }
        if (same) return v;
    }
    return {};
}

}  // namespace sp::integrations
