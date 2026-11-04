// Pruebas del core: sin Qt ni Windows. Se ejecutan con `ctest`.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "core/integrations/api_util.h"
#include "core/integrations/config.h"
#include "core/integrations/credentials.h"
#include "core/integrations/crypto.h"
#include "core/integrations/http.h"
#include "core/integrations/token_store.h"
#include "core/integrations/url.h"
#include "core/planner.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static std::string hex(const std::string& bin) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (const unsigned char c : bin) {
        out += d[c >> 4];
        out += d[c & 15];
    }
    return out;
}

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

    // Criptografía: vector conocido de SHA-256 y vector RFC 7636 para PKCE.
    CHECK(hex(sp::integrations::sha256("abc")) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sp::integrations::sha256Base64Url("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") ==
          "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
    CHECK(sp::integrations::randomBase64Url(32).size() > 30);

    // Codificación de URL: %20 también en query; nunca '+' ambiguo.
    CHECK(sp::integrations::urlEncode("Just Chatting") == "Just%20Chatting");
    CHECK(sp::integrations::urlEncode("a+b&c") == "a%2Bb%26c");
    CHECK(sp::integrations::urlDecode("Just%20Chatting") == "Just Chatting");
    CHECK(sp::integrations::urlDecode("a+b") == "a b");
    CHECK(sp::integrations::formEncode({{"scope", "user:read channel:read"}}) ==
          "scope=user%3Aread%20channel%3Aread");

    // Configuración con placeholders: nunca debe considerarse configurada.
    const sp::integrations::IntegrationConfig emptyCfg =
        sp::integrations::loadIntegrationConfig(dir / "no-existe.json");
    CHECK(!emptyCfg.twitch.configured() && !emptyCfg.kick.configured());
    CHECK(emptyCfg.kick.redirectUri == "http://127.0.0.1:7512/callback");

    // Almacén de tokens por plataforma.
    {
        sp::integrations::FileTokenStore tokens(dir / "tokens.json");
        sp::integrations::TokenSet t;
        t.accessToken = "access";
        t.refreshToken = "refresh";
        t.scope = "channel:read";
        t.expiresAt = sp::integrations::nowEpochSeconds() + 3600;
        CHECK(tokens.save(sp::integrations::Platform::Twitch, t));
        sp::integrations::TokenSet kick;
        kick.accessToken = "kick-access";
        kick.expiresAt = sp::integrations::nowEpochSeconds() + 3600;
        CHECK(tokens.save(sp::integrations::Platform::Kick, kick));
        sp::integrations::TokenSet loaded;
        CHECK(tokens.load(sp::integrations::Platform::Twitch, loaded));
        CHECK(loaded.accessToken == "access" && loaded.refreshToken == "refresh");
        // Guardar Kick no debe borrar Twitch.
        CHECK(tokens.load(sp::integrations::Platform::Kick, loaded));
        CHECK(loaded.accessToken == "kick-access");
#ifndef _WIN32
        struct stat st {};
        CHECK(::stat((dir / "tokens.json").c_str(), &st) == 0);
        CHECK((st.st_mode & 0777) == 0600);  // los tokens solo los lee el usuario
#endif
        CHECK(tokens.clear(sp::integrations::Platform::Twitch));
        CHECK(!tokens.load(sp::integrations::Platform::Twitch, loaded));
        CHECK(tokens.load(sp::integrations::Platform::Kick, loaded));
    }

    // Configuración de credenciales: guardado privado, huella y puerta de acceso.
    {
        sp::integrations::IntegrationConfig cfg;
        cfg.twitch.clientId = "twitch-client";
        cfg.kick.clientId = "kick-client";
        cfg.kick.clientSecret = "kick-secret";
        cfg.kick.redirectUri = "http://127.0.0.1:7512/callback";
        CHECK(!cfg.credentialsReady());  // todavía no se ha verificado
        CHECK(sp::integrations::credentialsFingerprint(cfg).size() == 64);
        const std::string before = sp::integrations::credentialsFingerprint(cfg);
        sp::integrations::markVerified(cfg);
        CHECK(cfg.verified && cfg.credentialsReady());
        CHECK(sp::integrations::credentialsFingerprint(cfg) == before);

        const fs::path configFile = dir / "integrations.json";
        CHECK(sp::integrations::saveIntegrationConfig(configFile, cfg));
#ifndef _WIN32
        struct stat st {};
        CHECK(::stat(configFile.c_str(), &st) == 0);
        CHECK((st.st_mode & 0777) == 0600);  // las credenciales solo las lee el usuario
#endif
        const auto loaded = sp::integrations::loadIntegrationConfig(configFile);
        CHECK(loaded.verified && loaded.credentialsReady());
        CHECK(loaded.twitch.clientId == "twitch-client");
        CHECK(loaded.kick.clientSecret == "kick-secret");

        // Editar una credencial invalida la marca de verificación.
        auto edited = loaded;
        edited.kick.clientSecret = "otro-secreto";
        edited.verified = true;  // aunque la UI intente conservarla
        edited.credentialHash = loaded.credentialHash;
        CHECK(sp::integrations::saveIntegrationConfig(configFile, edited));
        const auto reloaded = sp::integrations::loadIntegrationConfig(configFile);
        CHECK(!reloaded.verified && !reloaded.credentialsReady());

        // Solo Twitch también es una configuración válida (no exige Kick).
        sp::integrations::IntegrationConfig twitchOnly;
        twitchOnly.twitch.clientId = "twitch-client";
        sp::integrations::markVerified(twitchOnly);
        CHECK(twitchOnly.credentialsReady());
        // Kick sin secreto nunca está listo.
        sp::integrations::IntegrationConfig kickNoSecret;
        kickNoSecret.kick.clientId = "kick-client";
        kickNoSecret.kick.redirectUri = "http://127.0.0.1:7512/callback";
        sp::integrations::markVerified(kickNoSecret);
        CHECK(!kickNoSecret.credentialsReady());
        // Un secreto de Kick suelto (sin Client ID) tampoco.
        sp::integrations::IntegrationConfig orphanSecret;
        orphanSecret.kick.clientSecret = "kick-secret";
        sp::integrations::markVerified(orphanSecret);
        CHECK(!orphanSecret.credentialsReady());

        // La Redirect URI de Kick debe ser loopback con puerto.
        CHECK(sp::integrations::isLoopbackRedirect("http://127.0.0.1:7512/callback"));
        CHECK(sp::integrations::isLoopbackRedirect("http://localhost:8080/cb"));
        CHECK(!sp::integrations::isLoopbackRedirect("https://example.com/callback"));
        CHECK(!sp::integrations::isLoopbackRedirect(""));
        CHECK(!sp::integrations::isLoopbackRedirect("http://127.0.0.1/callback"));
    }

    // Verificación de credenciales con un transporte falso (sin red).
    {
        struct FakeHttp : sp::integrations::IHttpClient {
            std::vector<sp::integrations::HttpRequest> requests;
            std::function<sp::integrations::HttpResponse(const sp::integrations::HttpRequest&)> handler;
            void send(sp::integrations::HttpRequest request,
                      sp::integrations::HttpCallback callback) override {
                requests.push_back(request);
                callback(handler(request));
            }
            void cancelAll() override {}
        };

        sp::integrations::IntegrationConfig cfg;
        cfg.twitch.clientId = "twitch-client";
        cfg.kick.clientId = "kick-client";
        cfg.kick.clientSecret = "kick-secret";
        cfg.kick.redirectUri = "http://127.0.0.1:7512/callback";

        FakeHttp http;
        http.handler = [](const sp::integrations::HttpRequest& request) {
            sp::integrations::HttpResponse response;
            response.status = 200;
            if (request.url.find("twitch.tv") != std::string::npos)
                response.body = R"({"device_code":"device","user_code":"user"})";
            else
                response.body = R"({"access_token":"app-token","expires_in":3600})";
            return response;
        };
        bool called = false;
        sp::integrations::CredentialVerification result;
        sp::integrations::verifyCredentials(
            cfg, http, [&](sp::integrations::CredentialVerification value) {
                called = true;
                result = value;
            });
        CHECK(called && result.ok());
        CHECK(http.requests.size() == 2);
        CHECK(http.requests[0].body.find("client_id=twitch-client") != std::string::npos);
        CHECK(http.requests[1].body.find("grant_type=client_credentials") != std::string::npos);

        // Un client_id inválido devuelve error y no se guarda nada.
        FakeHttp bad;
        bad.handler = [](const sp::integrations::HttpRequest&) {
            sp::integrations::HttpResponse response;
            response.status = 400;
            response.body = R"({"message":"invalid client"})";
            return response;
        };
        called = false;
        sp::integrations::verifyCredentials(
            cfg, bad, [&](sp::integrations::CredentialVerification value) {
                called = true;
                result = value;
            });
        CHECK(called && !result.ok());
        CHECK(result.twitch.message.find("invalid client") != std::string::npos);

        // Kick sin secreto falla sin salir a la red.
        sp::integrations::IntegrationConfig noSecret;
        noSecret.kick.clientId = "kick-client";
        noSecret.kick.redirectUri = "http://127.0.0.1:7512/callback";
        FakeHttp unreachable;
        unreachable.handler = [](const sp::integrations::HttpRequest&) {
            return sp::integrations::HttpResponse{};
        };
        called = false;
        sp::integrations::verifyCredentials(
            noSecret, unreachable, [&](sp::integrations::CredentialVerification value) {
                called = true;
                result = value;
            });
        CHECK(called && !result.ok());
        CHECK(unreachable.requests.empty());

        // Sin ninguna credencial tampoco se puede verificar.
        called = false;
        sp::integrations::verifyCredentials(
            sp::integrations::IntegrationConfig{}, unreachable,
            [&](sp::integrations::CredentialVerification value) {
                called = true;
                result = value;
            });
        CHECK(called && !result.ok());
    }

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

        // Estado de publicación por plataforma.
        sp::StreamTarget tg;
        tg.platform = "twitch";
        tg.title = "Título oficial";
        tg.categoryId = "509658";
        tg.categoryName = "Just Chatting";
        tg.tags = {"Español", "Charla"};
        tg.lastPublishedAt = "2026-11-03T10:00:00Z";
        CHECK(p.saveTarget(a.id, tg).ok());
        CHECK(p.target(a.id, "twitch") != nullptr);
        CHECK(p.target(a.id, "twitch")->tags.size() == 2);
        CHECK(p.saveTarget(9999, tg).err == sp::Err::NoStream);

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
        const sp::StreamTarget* tg = nullptr;
        for (const sp::Stream* s : v)
            if (const sp::StreamTarget* t = p.target(s->id, "twitch")) tg = t;
        CHECK(tg != nullptr && tg->categoryId == "509658" && tg->tags.size() == 2);
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
