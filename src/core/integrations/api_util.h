// Utilidades compartidas por los clientes de Twitch y Kick.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "core/integrations/http.h"
#include "core/integrations/integration_types.h"

namespace sp::integrations {

// Clasifica una respuesta HTTP y extrae el mensaje de error de la API.
Result httpResult(const HttpResponse& response, const char* platform);

// Comprueba límites de título/etiquetas antes de enviar.
Result validatePublish(const Capabilities& caps, const PublishRequest& request,
                       const char* platform);

// Marca de tiempo ISO-8601 UTC (para last_published_at).
std::string nowIso8601();

// Convierte "expires_in" segundos en epoch. 0 si no aplica.
std::int64_t expiryFromNow(std::int64_t expiresInSeconds);

}  // namespace sp::integrations
