// Configuración de integraciones (client_id / client_secret / redirect_uri).
// Las credenciales reales viven FUERA del repositorio; aquí solo se cargan y
// se guardan con permisos privados (0600 en Unix).
#pragma once
#include <filesystem>
#include <string>

#include "core/integrations/integration_types.h"

namespace sp::integrations {

struct ProviderConfig {
    std::string clientId;
    std::string clientSecret;
    std::string redirectUri;

    // Un placeholder GENERIC_... se considera "no configurado".
    bool configured() const;
    bool hasSecret() const;
};

struct IntegrationConfig {
    ProviderConfig twitch;
    ProviderConfig kick;

    // Solo es true si las credenciales actuales son exactamente las que se
    // verificaron (credentialHash coincide). Editar cualquier campo lo invalida.
    bool verified = false;
    std::string credentialHash;

    bool twitchReady() const;
    bool kickReady() const;
    // Hay al menos una plataforma completa y todas las que tienen datos están
    // verificadas. Sin esto la UI principal no debe abrirse.
    bool credentialsReady() const;
};

// Huella estable de los campos sensibles (no de los flags de verificación).
std::string credentialsFingerprint(const IntegrationConfig& cfg);

// true si `uri` es una URL loopback con puerto (127.0.0.1 o localhost), que es
// lo único que puede escuchar el servidor OAuth local.
bool isLoopbackRedirect(const std::string& uri);

// Marca la configuración como verificada y sella la huella actual.
void markVerified(IntegrationConfig& cfg);

// Lee el JSON indicado (si no existe, devuelve una configuración vacía) y aplica
// los overrides de entorno SP_TWITCH_CLIENT_ID, SP_TWITCH_CLIENT_SECRET,
// SP_KICK_CLIENT_ID, SP_KICK_CLIENT_SECRET y SP_KICK_REDIRECT_URI.
// El flag `verified` solo se conserva si la huella guardada coincide.
IntegrationConfig loadIntegrationConfig(const std::filesystem::path& file);

// Escribe la configuración con permisos privados. Solo persiste `verified` si
// la huella coincide con las credenciales que se van a guardar.
bool saveIntegrationConfig(const std::filesystem::path& file, const IntegrationConfig& cfg);

}  // namespace sp::integrations
