#include "win_platform_services.h"

#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

#include "win_async.h"

namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

}  // namespace

WinLoopbackServer::~WinLoopbackServer() { stop(); }

bool WinLoopbackServer::start(unsigned short port, std::function<void(std::string)> onCallback,
                              std::function<void(std::string)> onError) {
    stop();
    onCallback_ = std::move(onCallback);
    onError_ = std::move(onError);

    listen_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_ == INVALID_SOCKET) {
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        ::listen(listen_, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listen_);
        listen_ = INVALID_SOCKET;
        // El fallo inmediato se comunica devolviendo false; onError se reserva
        // para errores posteriores al arranque.
        return false;
    }
    int len = sizeof(addr);
    if (getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len) == 0)
        port_ = ntohs(addr.sin_port);
    running_ = true;
    thread_ = std::thread([this] { threadMain(); });
    return true;
}

void WinLoopbackServer::stop() {
    // Primero se pide al hilo que salga y se espera: el bucle usa select() con
    // timeout, así que nunca queda bloqueado indefinidamente. Solo después se
    // cierra el socket, evitando cerrarlo mientras otro hilo lo usa.
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (listen_ != INVALID_SOCKET) {
        closesocket(listen_);
        listen_ = INVALID_SOCKET;
    }
    port_ = 0;
}

void WinLoopbackServer::threadMain() {
    while (running_) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listen_, &readable);
        timeval wait{};
        wait.tv_sec = 0;
        wait.tv_usec = 200 * 1000;  // 200 ms: permite cancelar con rapidez
        const int ready = ::select(0, &readable, nullptr, nullptr, &wait);
        if (!running_) break;
        if (ready <= 0) continue;  // timeout o error transitorio

        sockaddr_in peer{};
        int len = sizeof(peer);
        SOCKET client = ::accept(listen_, reinterpret_cast<sockaddr*>(&peer), &len);
        if (client == INVALID_SOCKET) continue;

        // Si el navegador conecta pero no envía la petición, recv no debe
        // bloquear el cierre del servidor.
        DWORD recvTimeout = 5000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&recvTimeout), sizeof(recvTimeout));

        char buffer[8192] = {};
        const int n = ::recv(client, buffer, sizeof(buffer) - 1, 0);
        std::string target;
        if (n > 0) {
            buffer[n] = '\0';
            const std::string request(buffer, static_cast<std::size_t>(n));
            const auto lineEnd = request.find("\r\n");
            const std::string line = request.substr(0, lineEnd);
            const auto firstSpace = line.find(' ');
            if (firstSpace != std::string::npos) {
                const auto secondSpace = line.find(' ', firstSpace + 1);
                target = line.substr(firstSpace + 1, secondSpace == std::string::npos
                                                          ? std::string::npos
                                                          : secondSpace - firstSpace - 1);
            }
        }

        const char* html =
            "<!doctype html><html><head><meta charset=\"utf-8\"><title>Stream Planner</title></head>"
            "<body style=\"font-family:sans-serif;text-align:center;padding:60px;background:#1e1e1e;"
            "color:#eee\"><h2>Autorizacion recibida</h2><p>Ya puedes cerrar esta pestana y volver "
            "a Stream Planner.</p></body></html>";
        const std::string body(html);
        const std::string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                                   "Content-Length: " + std::to_string(body.size()) +
                                   "\r\nConnection: close\r\n\r\n";
        ::send(client, header.c_str(), static_cast<int>(header.size()), 0);
        ::send(client, body.c_str(), static_cast<int>(body.size()), 0);
        ::shutdown(client, SD_BOTH);
        closesocket(client);

        if (!target.empty() && onCallback_) {
            auto msg = std::make_unique<sp::win::LoopbackMessage>();
            msg->callback = onCallback_;
            msg->target = std::move(target);
            if (PostMessageW(hwnd_, sp::win::kLoopback, 0, reinterpret_cast<LPARAM>(msg.get())))
                msg.release();
        }
    }
}

void WinPlatformServices::Browser::openUrl(const std::string& url) {
    const std::wstring w = wide(url);
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

WinPlatformServices::WinPlatformServices(HWND notify) : hwnd_(notify), http_(notify) {
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
}

WinPlatformServices::~WinPlatformServices() { WSACleanup(); }

std::unique_ptr<sp::integrations::ILoopbackServer> WinPlatformServices::makeLoopback() {
    return std::make_unique<WinLoopbackServer>(hwnd_);
}

void WinPlatformServices::scheduleAfter(int milliseconds, std::function<void()> callback) {
    const HWND hwnd = hwnd_;
    std::thread([hwnd, milliseconds, callback = std::move(callback)] {
        Sleep(static_cast<DWORD>(std::max(0, milliseconds)));
        auto fn = std::make_unique<std::function<void()>>(callback);
        if (PostMessageW(hwnd, sp::win::kSchedule, 0, reinterpret_cast<LPARAM>(fn.get())))
            fn.release();
    }).detach();
}
