// Persistencia SQLite con migración de una sola vez desde el antiguo data.txt.
#include "core/store.h"
#include "core/text.h"

#include <sqlite3.h>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <string_view>
#include <unordered_map>

namespace sp {
namespace {

bool exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (error) sqlite3_free(error);
    return rc == SQLITE_OK;
}

int openDb(const std::filesystem::path& file, sqlite3** db) {
#ifdef _WIN32
    return sqlite3_open16(file.c_str(), db);
#else
    return sqlite3_open(file.string().c_str(), db);
#endif
}

void bindText(sqlite3_stmt* st, int col, const std::string& value) {
    sqlite3_bind_text(st, col, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}
std::string columnText(sqlite3_stmt* st, int col) {
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(st, col));
    const int n = sqlite3_column_bytes(st, col);
    return p ? std::string(p, static_cast<std::size_t>(n)) : std::string();
}

bool createSchema(sqlite3* db) {
    return exec(db,
        "PRAGMA foreign_keys=ON;"
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=FULL;"
        "CREATE TABLE IF NOT EXISTS series ("
        " id INTEGER PRIMARY KEY, name TEXT NOT NULL, title_template TEXT NOT NULL,"
        " notif_template TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1);"
        "CREATE TABLE IF NOT EXISTS streams ("
        " id INTEGER PRIMARY KEY, series_id INTEGER NOT NULL REFERENCES series(id) ON DELETE CASCADE,"
        " number INTEGER NOT NULL, status INTEGER NOT NULL CHECK(status BETWEEN 0 AND 2), date TEXT NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_streams_series ON streams(series_id);"
        "CREATE TABLE IF NOT EXISTS tag_values ("
        " stream_id INTEGER NOT NULL REFERENCES streams(id) ON DELETE CASCADE,"
        " name TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY(stream_id,name));"
        "PRAGMA user_version=1;");
}

bool toInt(std::string_view s, int& v) {
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}
std::string unesc(std::string_view s) {
    std::string o; o.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i]; o += (n == 't') ? '\t' : (n == 'n') ? '\n' : n;
        } else o += s[i];
    }
    return o;
}
std::size_t splitTabs(std::string_view line, std::string_view* f, std::size_t maxF) {
    std::size_t n = 0, start = 0;
    while (n < maxF) {
        const auto tab = line.find('\t', start);
        if (tab == std::string_view::npos) { f[n++] = line.substr(start); break; }
        f[n++] = line.substr(start, tab - start); start = tab + 1;
    }
    return n;
}

// Lee el formato TAB anterior en memoria. No modifica ni borra el archivo original.
bool readLegacy(const std::filesystem::path& file, Store& out, int& maxId) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const auto size = in.tellg();
    if (size < 0) return false;
    std::string buf(static_cast<std::size_t>(size), '\0');
    in.seekg(0);
    if (!buf.empty()) in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    if (!in && !buf.empty()) return false;

    out.series.clear(); out.streams.clear(); maxId = 0;
    const std::string_view all(buf);
    std::size_t pos = 0;
    while (pos < all.size()) {
        const auto e = all.find('\n', pos);
        const auto line = all.substr(pos, e == std::string_view::npos ? std::string_view::npos : e - pos);
        pos = (e == std::string_view::npos) ? all.size() : e + 1;
        std::string_view f[7 + 2 * kMaxTags]{};
        const auto nf = splitTabs(line, f, 7 + 2 * kMaxTags);
        if (nf == 0) continue;
        if (f[0] == "S" && nf >= 6) {
            Series s;
            if (!toInt(f[1], s.id)) continue;
            s.name = unesc(f[2]); s.titleTemplate = unesc(f[3]);
            s.notifTemplate = unesc(f[4]); s.active = f[5] == "1";
            maxId = std::max(maxId, s.id); out.series.push_back(std::move(s));
        } else if (f[0] == "T" && nf >= 7) {
            Stream t; int st = 0;
            if (!toInt(f[1], t.id) || !toInt(f[2], t.seriesId) || !toInt(f[3], t.number) || !toInt(f[6], st)) continue;
            t.date = unesc(f[5]); t.status = static_cast<Status>(std::clamp(st, 0, 2));
            if (!f[4].empty()) t.tags.push_back({"topic", unesc(f[4])});
            for (std::size_t k = 7; k + 1 < nf && t.tags.size() < kMaxTags; k += 2) {
                if (!isValidTagName(f[k]) || isReservedTag(f[k])) continue;
                t.setTag(f[k], unesc(f[k + 1]));
            }
            maxId = std::max(maxId, t.id); out.streams.push_back(std::move(t));
        }
    }
    // Evita migrar parcialmente un archivo que contenga líneas, pero no registros reconocibles.
    return !out.series.empty() || !out.streams.empty() || buf.empty();
}

bool insertAll(sqlite3* db, const Store& store) {
    if (!exec(db, "BEGIN IMMEDIATE; DELETE FROM tag_values; DELETE FROM streams; DELETE FROM series;")) return false;
    sqlite3_stmt *ss = nullptr, *st = nullptr, *tag = nullptr;
    bool ok = sqlite3_prepare_v2(db, "INSERT INTO series(id,name,title_template,notif_template,active) VALUES(?,?,?,?,?)", -1, &ss, nullptr) == SQLITE_OK
        && sqlite3_prepare_v2(db, "INSERT INTO streams(id,series_id,number,status,date) VALUES(?,?,?,?,?)", -1, &st, nullptr) == SQLITE_OK
        && sqlite3_prepare_v2(db, "INSERT INTO tag_values(stream_id,name,value) VALUES(?,?,?)", -1, &tag, nullptr) == SQLITE_OK;
    for (const auto& s : store.series) {
        if (!ok) break;
        sqlite3_bind_int(ss, 1, s.id); bindText(ss, 2, s.name); bindText(ss, 3, s.titleTemplate);
        bindText(ss, 4, s.notifTemplate); sqlite3_bind_int(ss, 5, s.active ? 1 : 0);
        ok = sqlite3_step(ss) == SQLITE_DONE; sqlite3_reset(ss); sqlite3_clear_bindings(ss);
    }
    for (const auto& t : store.streams) {
        if (!ok) break;
        sqlite3_bind_int(st, 1, t.id); sqlite3_bind_int(st, 2, t.seriesId); sqlite3_bind_int(st, 3, t.number);
        sqlite3_bind_int(st, 4, static_cast<int>(t.status)); bindText(st, 5, t.date);
        ok = sqlite3_step(st) == SQLITE_DONE; sqlite3_reset(st); sqlite3_clear_bindings(st);
        for (const auto& tv : t.tags) {
            if (!ok) break;
            sqlite3_bind_int(tag, 1, t.id); bindText(tag, 2, tv.name); bindText(tag, 3, tv.value);
            ok = sqlite3_step(tag) == SQLITE_DONE; sqlite3_reset(tag); sqlite3_clear_bindings(tag);
        }
    }
    if (ss) sqlite3_finalize(ss);
    if (st) sqlite3_finalize(st);
    if (tag) sqlite3_finalize(tag);
    if (ok) ok = exec(db, "COMMIT;");
    else exec(db, "ROLLBACK;");
    return ok;
}
} // namespace

bool Store::save(const std::filesystem::path& file) const {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    sqlite3* db = nullptr;
    if (openDb(file, &db) != SQLITE_OK || !db) { if (db) sqlite3_close(db); return false; }
    const bool ok = createSchema(db) && insertAll(db, *this);
    sqlite3_close(db);
    return ok;
}

bool Store::load(const std::filesystem::path& file, const std::filesystem::path& legacyFile) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const bool databaseExisted = std::filesystem::exists(file, ec);
    sqlite3* db = nullptr;
    if (openDb(file, &db) != SQLITE_OK || !db) { if (db) sqlite3_close(db); return false; }
    if (!createSchema(db)) { sqlite3_close(db); return false; }

    if (!databaseExisted && !legacyFile.empty() && std::filesystem::exists(legacyFile, ec)) {
        int maxId = 0;
        if (!readLegacy(legacyFile, *this, maxId)) { sqlite3_close(db); series.clear(); streams.clear(); return false; }
        nextId_ = maxId + 1;
        const bool migrated = insertAll(db, *this);
        sqlite3_close(db);
        if (!migrated) { series.clear(); streams.clear(); return false; }
        // Conserva una copia byte a byte del origen; no se elimina el data.txt original.
        auto backup = legacyFile; backup += ".migrated.bak";
        if (!std::filesystem::exists(backup, ec)) {
            ec.clear(); std::filesystem::copy_file(legacyFile, backup, std::filesystem::copy_options::none, ec);
        }
        return true;
    }

    series.clear(); streams.clear(); int maxId = 0;
    sqlite3_stmt* q = nullptr;
    bool ok = sqlite3_prepare_v2(db, "SELECT id,name,title_template,notif_template,active FROM series ORDER BY id", -1, &q, nullptr) == SQLITE_OK;
    while (ok && sqlite3_step(q) == SQLITE_ROW) {
        Series s; s.id = sqlite3_column_int(q, 0); s.name = columnText(q, 1);
        s.titleTemplate = columnText(q, 2); s.notifTemplate = columnText(q, 3); s.active = sqlite3_column_int(q, 4) != 0;
        maxId = std::max(maxId, s.id); series.push_back(std::move(s));
    }
    if (q) sqlite3_finalize(q);
    q = nullptr;
    if (ok) ok = sqlite3_prepare_v2(db, "SELECT id,series_id,number,status,date FROM streams ORDER BY id", -1, &q, nullptr) == SQLITE_OK;
    while (ok && sqlite3_step(q) == SQLITE_ROW) {
        Stream t; t.id = sqlite3_column_int(q, 0); t.seriesId = sqlite3_column_int(q, 1);
        t.number = sqlite3_column_int(q, 2); t.status = static_cast<Status>(std::clamp(sqlite3_column_int(q, 3), 0, 2));
        t.date = columnText(q, 4); maxId = std::max(maxId, t.id); streams.push_back(std::move(t));
    }
    if (q) sqlite3_finalize(q);
    q = nullptr;
    if (ok) ok = sqlite3_prepare_v2(db, "SELECT stream_id,name,value FROM tag_values ORDER BY stream_id,name", -1, &q, nullptr) == SQLITE_OK;
    // Índice id -> posición: evita O(streams x etiquetas) al asignar cada etiqueta.
    std::unordered_map<int, std::size_t> pos;
    pos.reserve(streams.size());
    for (std::size_t i = 0; i < streams.size(); ++i) pos.emplace(streams[i].id, i);
    while (ok && sqlite3_step(q) == SQLITE_ROW) {
        const auto it = pos.find(sqlite3_column_int(q, 0));
        if (it != pos.end()) streams[it->second].setTag(columnText(q, 1), columnText(q, 2));
    }
    if (q) sqlite3_finalize(q);
    sqlite3_close(db);
    if (!ok) { series.clear(); streams.clear(); return false; }
    nextId_ = maxId + 1;
    return true;
}

} // namespace sp
