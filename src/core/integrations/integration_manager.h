// Registro de proveedores y punto de entrada del core de integraciones.
#pragma once
#include "core/integrations/broadcaster.h"
#include "core/integrations/config.h"
#include "core/integrations/kick_client.h"
#include "core/integrations/platform_services.h"
#include "core/integrations/token_store.h"
#include "core/integrations/twitch_client.h"

namespace sp::integrations {

class IntegrationManager {
public:
    IntegrationManager(IntegrationConfig config, IPlatformServices& services, ITokenStore& tokens)
        : twitch_(config.twitch, services, tokens), kick_(config.kick, services, tokens) {}

    IBroadcaster& provider(Platform p) {
        return p == Platform::Twitch ? static_cast<IBroadcaster&>(twitch_) : kick_;
    }
    const IBroadcaster& provider(Platform p) const {
        return p == Platform::Twitch ? static_cast<const IBroadcaster&>(twitch_) : kick_;
    }

    TwitchClient& twitch() { return twitch_; }
    KickClient& kick() { return kick_; }

private:
    TwitchClient twitch_;
    KickClient kick_;
};

}  // namespace sp::integrations
