// Servicios que dependen de la plataforma pero que el core necesita para OAuth:
// abrir el navegador y escuchar el redirect loopback. Se implementan en src/platform.
#pragma once
#include <functional>
#include <memory>
#include <string>

namespace sp::integrations {

class IBrowserLauncher {
public:
    virtual ~IBrowserLauncher() = default;
    virtual void openUrl(const std::string& url) = 0;
};

// Servidor HTTP local de un solo uso (solo 127.0.0.1) para el redirect de OAuth.
class ILoopbackServer {
public:
    virtual ~ILoopbackServer() = default;
    // Arranca en el puerto indicado (0 = efímero). `onCallback` recibe la ruta + query.
    // Si arranca, devuelve true y deja el puerto real en port().
    virtual bool start(unsigned short port, std::function<void(std::string)> onCallback,
                       std::function<void(std::string)> onError) = 0;
    virtual void stop() = 0;
    virtual unsigned short port() const = 0;
};

class IHttpClient;

class IPlatformServices {
public:
    virtual ~IPlatformServices() = default;
    virtual IHttpClient& http() = 0;
    virtual IBrowserLauncher& browser() = 0;
    virtual std::unique_ptr<ILoopbackServer> makeLoopback() = 0;
    // Ejecuta `callback` en el hilo de UI tras `milliseconds`. Se usa para el
    // polling del Device Code Flow y los reintentos con backoff.
    virtual void scheduleAfter(int milliseconds, std::function<void()> callback) = 0;
};

}  // namespace sp::integrations
