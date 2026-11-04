// Verificación no interactiva de las credenciales de aplicación:
// - Twitch: pide un device code (valida client_id y scopes).
// - Kick: pide un app access token (client_credentials).
// No guarda nada: la UI decide si persiste la configuración tras el éxito.
#pragma once
#include <functional>

#include "core/integrations/config.h"
#include "core/integrations/http.h"
#include "core/integrations/integration_types.h"

namespace sp::integrations {

struct CredentialVerification {
    Result twitch;
    Result kick;

    bool ok() const { return twitch.ok() && kick.ok(); }
};

using CredentialVerificationCallback = std::function<void(CredentialVerification)>;

// Verifica las plataformas que tengan client_id. Las vacías se omiten.
// El callback se invoca una sola vez, siempre en el hilo de interfaz.
void verifyCredentials(const IntegrationConfig& cfg, IHttpClient& http,
                       CredentialVerificationCallback callback);

}  // namespace sp::integrations
