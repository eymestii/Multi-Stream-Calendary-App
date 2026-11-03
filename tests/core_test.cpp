// Pruebas del core: sin Qt ni Windows. Se ejecutan con `ctest`.
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "core/planner.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "sp_core_test";
    fs::remove_all(dir);
    sp::DataPaths paths{dir / "t.db", {}};

    // parseDay / todayIso
    CHECK(sp::parseDay("12") == 12);
    CHECK(sp::parseDay("") == 0);
    CHECK(sp::parseDay("0") == 0);
    CHECK(sp::parseDay("12a") == 0);
    CHECK(sp::parseDay("123456") == 0);
    CHECK(sp::parseDay("-3") == 0);
    CHECK(sp::todayIso().size() == 10);

    {
        sp::Planner p(paths);
        CHECK(p.open());
        CHECK(p.firstSeriesId() == 0);

        auto c = p.createSeries("Mi serie");
        CHECK(c.ok() && c.id > 0);
        const int sid = c.id;

        // Guardar con plantilla válida
        sp::Draft d{"Mi serie", "[{series}] Día {day} | {topic}", "Hoy: {topic} {tema_2}"};
        sp::Stream ref = sp::makeSample(1, "2026-11-02", {"topic", "tema_2"}, {});
        CHECK(sp::makePreview(d, ref).title == "[Mi serie] Día 1 | <topic>");
        CHECK(p.saveSeries(sid, d, ref).ok());
        CHECK(p.findSeries(sid)->notifTemplate == d.notifTemplate);

        // Límite de Twitch (140) por caracteres, no por bytes: 140 'ñ' caben
        sp::Draft ok140{"x", std::string(140, 'a'), "n"};
        CHECK(p.saveSeries(sid, ok140, ref).ok());
        std::string ene; for (int i = 0; i < 140; ++i) ene += "\xC3\xB1";
        sp::Draft n140{"x", ene, "n"};
        CHECK(p.saveSeries(sid, n140, ref).ok());
        sp::Draft over{"x", std::string(141, 'a'), "n"};
        CHECK(p.saveSeries(sid, over, ref).err == sp::Err::TitleTooLong);
        sp::Draft overN{"x", "t", std::string(141, 'a')};
        CHECK(p.saveSeries(sid, overN, ref).err == sp::Err::NotifTooLong);

        // Demasiadas etiquetas
        std::string many;
        for (int i = 0; i < 11; ++i) many += "{t" + std::to_string(i) + "}";
        CHECK(p.saveSeries(sid, {"x", many, "n"}, ref).err == sp::Err::TooManyTags);
        CHECK(sp::makePreview({"x", many, "n"}, ref).tags.size() == sp::kMaxTags);

        // Restaurar plantillas por defecto y añadir streams
        CHECK(p.saveSeries(sid, d, ref).ok());
        CHECK(p.addStream(sid, 0, "", {}).err == sp::Err::InvalidDay);
        CHECK(p.addStream(999, 1, "", {}).err == sp::Err::NoSeries);
        auto a = p.addStream(sid, 3, "2026-11-03", {{"topic", "Intro"}});
        auto b = p.addStream(sid, 1, "2026-11-01", {{"topic", "Setup"}});
        auto e = p.addStream(sid, 3, "2026-11-04", {{"topic", "Otro"}});
        CHECK(a.ok() && b.ok() && e.ok());
        CHECK(p.latestDay(sid) == 3);

        // Orden por día y luego por creación
        auto v = p.streamsOf(sid);
        CHECK(v.size() == 3 && v[0]->id == b.id && v[1]->id == a.id && v[2]->id == e.id);

        // Stream demasiado largo para la plantilla guardada
        auto big = p.addStream(sid, 4, "", {{"topic", std::string(140, 'z')}});
        CHECK(big.err == sp::Err::TitleTooLong && big.titleLen > sp::kTwitchLimit);

        // Mover entre columnas y límites
        CHECK(p.moveStream(a.id, -1).ok());  // ya en Planeado: no hace nada, no es error
        CHECK(p.findStream(a.id)->status == sp::Status::Planned);
        CHECK(p.moveStream(a.id, +1).ok() && p.findStream(a.id)->status == sp::Status::Live);
        CHECK(p.moveStream(a.id, +1).ok() && p.findStream(a.id)->status == sp::Status::Done);
        CHECK(p.moveStream(a.id, +1).ok() && p.findStream(a.id)->status == sp::Status::Done);
        CHECK(p.count(sid, sp::Status::Done) == 1 && p.count(sid, sp::Status::Planned) == 2);
        CHECK(p.moveStream(12345, 1).err == sp::Err::NoStream);

        CHECK(p.toggleActive(sid).ok() && !p.findSeries(sid)->active);
        CHECK(p.deleteStream(e.id).ok() && p.findStream(e.id) == nullptr);
    }

    // Persistencia: reabrir y comprobar que todo sigue ahí (incluidas etiquetas Unicode)
    {
        sp::Planner p(paths);
        CHECK(p.open());
        CHECK(p.series().size() == 1);
        const int sid = p.firstSeriesId();
        CHECK(!p.findSeries(sid)->active);
        auto v = p.streamsOf(sid);
        CHECK(v.size() == 2);
        CHECK(*v[0]->findTag("topic") == "Setup");
        CHECK(p.createSeries("Serie ñandú ✓").ok());
        CHECK(p.deleteSeries(sid).ok());  // borra también sus streams (ON DELETE CASCADE)
        CHECK(p.series().size() == 1 && p.streamsOf(sid).empty());
    }
    {
        sp::Planner p(paths);
        CHECK(p.open());
        CHECK(p.series().size() == 1 && p.series()[0].name == "Serie ñandú ✓");
    }

    // Rutas por defecto
#ifndef _WIN32
    setenv("XDG_DATA_HOME", (dir / "xdg").c_str(), 1);
    unsetenv("STREAMPLANNER_DB");
    CHECK(sp::defaultDataPaths().db == dir / "xdg" / "StreamPlanner" / "stream_planner.db");
    setenv("STREAMPLANNER_DB", (dir / "custom" / "x.db").c_str(), 1);
    CHECK(sp::defaultDataPaths().db == dir / "custom" / "x.db");
#endif

    fs::remove_all(dir);
    std::printf(failures ? "%d FALLOS\n" : "OK: todas las pruebas del core pasan\n", failures);
    return failures ? 1 : 0;
}
