// Servicios Win32: navegador, loopback OAuth (Winsock) y temporizadores.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "core/integrations/platform_services.h"
#include "winhttp_client.h"

class WinLoopbackServer : public sp::integrations::ILoopbackServer {
public:
    explicit WinLoopbackServer(HWND notify) : hwnd_(notify) {}
    ~WinLoopbackServer() override;

    bool start(unsigned short port, std::function<void(std::string)> onCallback,
               std::function<void(std::string)> onError) override;
    void stop() override;
    unsigned short port() const override { return port_; }

private:
    void threadMain();

    HWND hwnd_;
    SOCKET listen_ = INVALID_SOCKET;
    unsigned short port_ = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::function<void(std::string)> onCallback_;
    std::function<void(std::string)> onError_;
};

class WinPlatformServices : public sp::integrations::IPlatformServices {
public:
    explicit WinPlatformServices(HWND notify);
    ~WinPlatformServices() override;

    sp::integrations::IHttpClient& http() override { return http_; }
    sp::integrations::IBrowserLauncher& browser() override { return browser_; }
    std::unique_ptr<sp::integrations::ILoopbackServer> makeLoopback() override;
    void scheduleAfter(int milliseconds, std::function<void()> callback) override;

private:
    class Browser : public sp::integrations::IBrowserLauncher {
    public:
        void openUrl(const std::string& url) override;
    };

    HWND hwnd_;
    WinHttpClient http_;
    Browser browser_;
};
