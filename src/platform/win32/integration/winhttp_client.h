// Transporte HTTP Win32/WinHTTP. Las peticiones se ejecutan en un hilo y la
// respuesta se entrega al hilo de UI mediante PostMessage.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <memory>

#include "core/integrations/http.h"

class WinHttpClient : public sp::integrations::IHttpClient {
public:
    explicit WinHttpClient(HWND notify) : hwnd_(notify) {}
    void send(sp::integrations::HttpRequest request,
              sp::integrations::HttpCallback callback) override;
    void cancelAll() override;

private:
    static void run(sp::integrations::HttpRequest request,
                    sp::integrations::HttpCallback callback,
                    std::shared_ptr<std::atomic<int>> generation, int expected, HWND hwnd);

    HWND hwnd_;
    // Compartido con los hilos en vuelo para que cancelAll() sea seguro aunque el
    // cliente se destruya mientras una petición sigue en curso.
    std::shared_ptr<std::atomic<int>> generation_ = std::make_shared<std::atomic<int>>(0);
};
