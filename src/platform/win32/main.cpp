// Stream Planner - punto de entrada y ventana principal (Win32).
// Lógica de datos: core/   |   Interfaz: ui/
#include "platform/win32/app.h"

#include <utility>

#include "core/integrations/config.h"
#include "core/paths.h"
#include "platform/win32/integration/credentials_dialog.h"
#include "platform/win32/integration/win_async.h"

// Activa los controles con estilo moderno (Common Controls v6)
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

App g;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Respuestas asíncronas de WinHTTP/OAuth/temporizadores: siempre en el hilo de UI.
    if (sp::win::processMessage(msg, wp, lp)) return 0;

    switch (msg) {
        case WM_CREATE: {
            g.hwnd = hwnd;
            createControls();
            HMENU menu = CreateMenu();
            HMENU integrations = CreatePopupMenu();
            AppendMenuW(integrations, MF_STRING, IDC_CREDENTIALS, L"Credenciales...");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(integrations), L"Integraciones");
            SetMenu(hwnd, menu);
            if (!g.store.load(g.dataPath, g.legacyDataPath)) {
                MessageBoxW(hwnd, L"No se pudo abrir la base de datos. Tus datos no se han eliminado. La aplicación se cerrará para evitar sobrescribirlos.", L"Stream Planner", MB_OK | MB_ICONERROR);
                return -1;
            }
            // Integraciones oficiales: configuración local + tokens + transporte WinHTTP.
            g.platformServices = std::make_unique<WinPlatformServices>(hwnd);
            g.tokenStore = std::make_unique<sp::integrations::FileTokenStore>(sp::defaultTokenPath());
            g.integrations = std::make_unique<sp::integrations::IntegrationManager>(
                sp::integrations::loadIntegrationConfig(sp::defaultConfigPath()), *g.platformServices,
                *g.tokenStore);
            g.curSeries = g.store.series.empty() ? 0 : g.store.series.front().id;
            refreshSeriesList();
            loadFields();
            return 0;
        }
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) layout(LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = (MINMAXINFO*)lp;
            mmi->ptMinTrackSize.x = 960;
            mmi->ptMinTrackSize.y = 740;
            return 0;
        }
        case WM_COMMAND:
            onCommand(LOWORD(wp), HIWORD(wp));
            return 0;
        case WM_CTLCOLORSTATIC: {
            HWND h = (HWND)lp;
            if ((h == g.stTitleCount && g.titleOver) || (h == g.stNotifCount && g.notifOver) ||
                (h == g.stTags && g.tagsOver)) {
                HDC dc = (HDC)wp;
                SetTextColor(dc, RGB(200, 0, 0));
                SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
                return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
            }
            break;
        }
        case WM_DESTROY:
            // Cancela peticiones en vuelo; los gestores se liberan al salir del
            // bucle de mensajes para no invalidar callbacks ya encolados.
            if (g.platformServices) g.platformServices->http().cancelAll();
            if (HMENU menu = GetMenu(hwnd)) {
                SetMenu(hwnd, nullptr);
                DestroyMenu(menu);
            }
            if (g.font) DeleteObject(g.font);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void reloadIntegrations() {
    if (!g.platformServices || !g.tokenStore) return;
    // Evita que respuestas del gestor anterior sigan llegando. Las que ya
    // estuvieran encoladas siguen siendo válidas porque el gestor se conserva.
    g.platformServices->http().cancelAll();
    if (g.integrations) {
        // Invalida temporizadores y callbacks pendientes del gestor anterior.
        g.integrations->twitch().cancel();
        g.integrations->kick().cancel();
        g.retiredIntegrations.push_back(std::move(g.integrations));
    }
    g.integrations = std::make_unique<sp::integrations::IntegrationManager>(
        sp::integrations::loadIntegrationConfig(sp::defaultConfigPath()), *g.platformServices,
        *g.tokenStore);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    // Base de datos en %LOCALAPPDATA%; origen antiguo en %APPDATA% para migración.
    wchar_t localBuf[MAX_PATH], roamingBuf[MAX_PATH];
    DWORD localN = GetEnvironmentVariableW(L"LOCALAPPDATA", localBuf, MAX_PATH);
    DWORD roamingN = GetEnvironmentVariableW(L"APPDATA", roamingBuf, MAX_PATH);
    std::filesystem::path localDir = (localN > 0 && localN < MAX_PATH) ? std::filesystem::path(localBuf) : std::filesystem::path(L".");
    std::filesystem::path roamingDir = (roamingN > 0 && roamingN < MAX_PATH) ? std::filesystem::path(roamingBuf) : std::filesystem::path(L".");
    localDir /= L"StreamPlanner";
    roamingDir /= L"StreamPlanner";
    std::error_code ec;
    std::filesystem::create_directories(localDir, ec);
    g.dataPath = localDir / L"stream_planner.db";
    g.legacyDataPath = roamingDir / L"data.txt";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"StreamPlannerWnd";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Stream Planner", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1180, 800, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    // Puerta de credenciales: sin credenciales verificadas no se muestra la UI.
    if (!sp::integrations::loadIntegrationConfig(sp::defaultConfigPath()).credentialsReady()) {
        if (!openCredentialsDialog(hwnd, true)) {
            DestroyWindow(hwnd);
            return 0;
        }
        reloadIntegrations();
    }

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    // Tras el arranque, devuelve al sistema las páginas de inicialización que ya no se usan.
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {  // permite Tab entre controles
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
