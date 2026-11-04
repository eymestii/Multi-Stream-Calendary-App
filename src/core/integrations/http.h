// Transporte HTTP abstracto. El core no conoce Qt, WinHTTP ni curl.
// Contrato: el callback se ejecuta SIEMPRE en el hilo de interfaz y puede
// ejecutarse de forma síncrona si la petición falla antes de salir a la red.
#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sp::integrations {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    int timeoutMs = 20000;
    bool followRedirects = false;
};

struct HttpResponse {
    int status = 0;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string error;  // vacío si hubo respuesta HTTP

    bool transportOk() const { return error.empty() && status > 0; }
    std::string header(std::string_view name) const;
};

using HttpCallback = std::function<void(HttpResponse)>;

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual void send(HttpRequest request, HttpCallback callback) = 0;
    // Cancela peticiones pendientes; los callbacks pendientes no se invocan.
    virtual void cancelAll() = 0;
};

}  // namespace sp::integrations
