// Contrato de un proveedor de streaming (Twitch / Kick).
// Toda la lógica de OAuth, endpoints y validación vive aquí; las UIs solo pintan.
#pragma once
#include <string>
#include <string_view>

#include "core/integrations/integration_types.h"

namespace sp::integrations {

class IBroadcaster {
public:
    virtual ~IBroadcaster() = default;

    virtual Platform platform() const = 0;
    virtual Capabilities capabilities() const = 0;
    virtual AuthState authState() const = 0;
    virtual bool configured() const = 0;
    virtual std::string configurationProblem() const = 0;
    virtual const Account& account() const = 0;

    // Carga tokens guardados y los valida/refresca. No abre el navegador.
    virtual void restore(AccountCallback callback) = 0;

    // Inicia el flujo OAuth completo (abre el navegador si hace falta).
    virtual void connect(ConnectCallback callback) = 0;
    virtual void disconnect() = 0;

    virtual void fetchChannel(ChannelCallback callback) = 0;
    virtual void searchCategories(std::string query, std::string cursor,
                                  CategoriesCallback callback) = 0;
    virtual void publish(const PublishRequest& request, PublishCallback callback) = 0;

    // Cancela el flujo OAuth/loopback en curso. Las peticiones HTTP pendientes las
    // cancela el IHttpClient de la plataforma.
    virtual void cancel() = 0;
};

}  // namespace sp::integrations
