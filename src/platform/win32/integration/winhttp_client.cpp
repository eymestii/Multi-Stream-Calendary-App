#include "winhttp_client.h"

#include <winhttp.h>

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
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

std::string narrow(const wchar_t* s, int len) {
    if (!s || len <= 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, len, out.data(), n, nullptr, nullptr);
    return out;
}

void addHeader(sp::integrations::HttpResponse& response, const std::string& line) {
    const auto colon = line.find(':');
    if (colon == std::string::npos) return;
    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
        value.pop_back();
    response.headers.emplace_back(std::move(name), std::move(value));
}

}  // namespace

void WinHttpClient::send(sp::integrations::HttpRequest request,
                         sp::integrations::HttpCallback callback) {
    if (!callback) return;
    auto generation = generation_;
    const int expected = generation->load();
    const HWND hwnd = hwnd_;
    std::thread([request = std::move(request), callback = std::move(callback), generation, expected,
                 hwnd]() mutable {
        WinHttpClient::run(std::move(request), std::move(callback), std::move(generation), expected,
                           hwnd);
    }).detach();
}

void WinHttpClient::cancelAll() { generation_->fetch_add(1); }

void WinHttpClient::run(sp::integrations::HttpRequest request,
                        sp::integrations::HttpCallback callback,
                        std::shared_ptr<std::atomic<int>> generation, int expected, HWND hwnd) {
    auto response = std::make_shared<sp::integrations::HttpResponse>();
    auto finish = [callback, response, generation, expected, hwnd] {
        if (generation->load() != expected) return;
        auto done = std::make_unique<sp::win::HttpCompletion>();
        done->callback = callback;
        done->response = std::move(*response);
        if (!PostMessageW(hwnd, sp::win::kHttpDone, 0, reinterpret_cast<LPARAM>(done.get()))) {
            return;
        }
        done.release();
    };

    const std::wstring url = wide(request.url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &uc)) {
        response->error = "URL inválida";
        finish();
        return;
    }
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength > 0) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (path.empty()) path = L"/";

    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET session = WinHttpOpen(L"StreamPlanner/1.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        response->error = "No se pudo inicializar WinHTTP";
        finish();
        return;
    }
    const int timeout = std::max(5000, request.timeoutMs);
    WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout);

    HINTERNET connect = WinHttpConnect(session, host.c_str(), uc.nPort, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        response->error = "No se pudo conectar con el servidor";
        finish();
        return;
    }
    const std::wstring verb = wide(request.method);
    HINTERNET req = WinHttpOpenRequest(connect, verb.c_str(), path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        response->error = "No se pudo crear la petición HTTP";
        finish();
        return;
    }

    for (const auto& [name, value] : request.headers) {
        const std::wstring header = wide(name + ": " + value);
        WinHttpAddRequestHeaders(req, header.c_str(), static_cast<DWORD>(-1),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    const void* body = request.body.empty() ? nullptr : request.body.data();
    const DWORD bodyLen = static_cast<DWORD>(request.body.size());
    bool ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 const_cast<void*>(body), bodyLen, bodyLen, 0) != FALSE;
    if (ok) ok = WinHttpReceiveResponse(req, nullptr) != FALSE;

    if (ok) {
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
        response->status = static_cast<int>(status);

        DWORD rawLen = 0;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                            &rawLen, WINHTTP_NO_HEADER_INDEX);
        if (rawLen > 0) {
            std::wstring raw(rawLen / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                    raw.data(), &rawLen, WINHTTP_NO_HEADER_INDEX)) {
                const std::string text = narrow(raw.c_str(), static_cast<int>(raw.size()));
                std::size_t pos = 0;
                bool first = true;
                while (pos < text.size()) {
                    const auto nl = text.find("\r\n", pos);
                    const std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                    if (first) first = false;
                    else if (!line.empty()) addHeader(*response, line);
                    if (nl == std::string::npos) break;
                    pos = nl + 2;
                }
            }
        }

        constexpr std::size_t kMaxBytes = 8 * 1024 * 1024;
        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(req, &available) || available == 0) break;
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(req, chunk.data(), available, &read) || read == 0) break;
            response->body.append(chunk.data(), read);
            if (response->body.size() > kMaxBytes) {
                response->error = "respuesta demasiado grande";
                response->status = 0;
                break;
            }
        }
    } else {
        response->error = "Error de red WinHTTP (" + std::to_string(GetLastError()) + ")";
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    finish();
}
