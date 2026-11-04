// Mensajes internos para entregar callbacks asíncronos en el hilo de UI de Win32.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <string>

#include "core/integrations/http.h"

namespace sp::win {

constexpr UINT kHttpDone = WM_APP + 1;
constexpr UINT kSchedule = WM_APP + 2;
constexpr UINT kLoopback = WM_APP + 3;

struct HttpCompletion {
    std::function<void(sp::integrations::HttpResponse)> callback;
    sp::integrations::HttpResponse response;
};

struct LoopbackMessage {
    std::function<void(std::string)> callback;
    std::string target;
};

// Invoca y libera el mensaje correspondiente. Devuelve true si lo gestionó.
bool processMessage(UINT message, WPARAM wparam, LPARAM lparam);

}  // namespace sp::win
