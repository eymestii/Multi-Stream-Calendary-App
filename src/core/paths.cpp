#include "core/paths.h"

#include <cstdlib>
#include <system_error>

namespace sp {
namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
fs::path envPath(const wchar_t* name) {
    const wchar_t* v = _wgetenv(name);
    return (v && *v) ? fs::path(v) : fs::path(L".");
}
#else
fs::path xdgHome(const char* var, const char* fallbackRelative) {
    const char* x = std::getenv(var);
    if (x && *x && x[0] == '/') return fs::path(x);  // la spec XDG exige ruta absoluta
    const char* h = std::getenv("HOME");
    if (h && *h) return fs::path(h) / fallbackRelative;
    return fs::path(".");
}
fs::path dataHome() { return xdgHome("XDG_DATA_HOME", ".local/share"); }
fs::path configHome() { return xdgHome("XDG_CONFIG_HOME", ".config"); }
#endif

fs::path envOverride(const char* name, fs::path fallback) {
    if (const char* o = std::getenv(name); o && *o) {
        fs::path p(o);
        std::error_code ec;
        if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
        return p;
    }
    std::error_code ec;
    fs::create_directories(fallback.parent_path(), ec);
    return fallback;
}

}  // namespace

DataPaths defaultDataPaths() {
    DataPaths p;
    // STREAMPLANNER_DB: ruta explícita de la base de datos (pruebas, modo portable).
    if (const char* o = std::getenv("STREAMPLANNER_DB"); o && *o) {
        p.db = fs::path(o);
        std::error_code ec;
        if (p.db.has_parent_path()) fs::create_directories(p.db.parent_path(), ec);
        return p;
    }
#ifdef _WIN32
    p.db = envPath(L"LOCALAPPDATA") / L"StreamPlanner" / L"stream_planner.db";
    p.legacy = envPath(L"APPDATA") / L"StreamPlanner" / L"data.txt";
#else
    p.db = dataHome() / "StreamPlanner" / "stream_planner.db";
#endif
    std::error_code ec;
    fs::create_directories(p.db.parent_path(), ec);
    return p;
}

std::filesystem::path defaultConfigPath() {
#ifdef _WIN32
    return envOverride("STREAMPLANNER_CONFIG",
                       envPath(L"APPDATA") / L"StreamPlanner" / L"integrations.json");
#else
    return envOverride("STREAMPLANNER_CONFIG", configHome() / "StreamPlanner" / "integrations.json");
#endif
}

std::filesystem::path defaultTokenPath() {
#ifdef _WIN32
    return envOverride("STREAMPLANNER_TOKENS",
                       envPath(L"LOCALAPPDATA") / L"StreamPlanner" / L"tokens.json");
#else
    return envOverride("STREAMPLANNER_TOKENS", dataHome() / "StreamPlanner" / "tokens.json");
#endif
}

}  // namespace sp
