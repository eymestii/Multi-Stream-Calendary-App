#include "credentials_dialog.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

#include "core/integrations/config.h"
#include "core/integrations/credentials.h"
#include "core/paths.h"
#include "platform/win32/app.h"
#include "platform/win32/integration/win_async.h"
#include "platform/win32/integration/winhttp_client.h"

namespace {

enum {
    IDC_TWITCH_ID = 6001,
    IDC_KICK_ID,
    IDC_KICK_SECRET,
    IDC_KICK_REDIRECT,
    IDC_SHOW_SECRET,
    IDC_STATUS,
    IDC_SAVE,
    IDC_CANCEL
};

struct Init {
    bool required = true;
    bool* accepted = nullptr;
};

struct State {
    HWND twitchId = nullptr;
    HWND kickId = nullptr;
    HWND kickSecret = nullptr;
    HWND kickRedirect = nullptr;
    HWND showSecret = nullptr;
    HWND status = nullptr;
    HWND save = nullptr;
    HWND cancel = nullptr;

    std::unique_ptr<WinHttpClient> http;
    bool busy = false;
    bool statusError = false;
    bool required = true;
    bool* accepted = nullptr;
    std::string twitchSecret;
    std::string twitchRedirect;
};

State* stateOf(HWND hwnd) {
    return reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

std::string trim(std::string value) {
    auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), space));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), space).base(), value.end());
    return value;
}

void setText(HWND h, const std::string& text) { SetWindowTextW(h, W(text).c_str()); }
void setText(HWND h, const std::wstring& text) { SetWindowTextW(h, text.c_str()); }

// EnumChildWindows exige CALLBACK (__stdcall en 32 bits): una lambda sin
// capturas no garantiza esa convención.
BOOL CALLBACK setFontProc(HWND child, LPARAM font) {
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
    return TRUE;
}

void setStatus(State* st, const std::wstring& text, bool error = false) {
    st->statusError = error;
    setText(st->status, text);
    InvalidateRect(st->status, nullptr, TRUE);
}

void setBusy(HWND hwnd, bool busy) {
    State* st = stateOf(hwnd);
    if (!st) return;
    st->busy = busy;
    EnableWindow(st->twitchId, !busy);
    EnableWindow(st->kickId, !busy);
    EnableWindow(st->kickSecret, !busy);
    EnableWindow(st->kickRedirect, !busy);
    EnableWindow(st->showSecret, !busy);
    EnableWindow(st->save, !busy);
    SetWindowTextW(st->save, busy ? L"Verificando..." : L"Verificar y guardar");
}

void toggleSecret(State* st) {
    const bool show = SendMessageW(st->showSecret, BM_GETCHECK, 0, 0) == BST_CHECKED;
    SendMessageW(st->kickSecret, EM_SETPASSWORDCHAR, show ? 0 : static_cast<WPARAM>(0x25CF), 0);
    InvalidateRect(st->kickSecret, nullptr, TRUE);
}

void submit(HWND hwnd) {
    State* st = stateOf(hwnd);
    if (!st || st->busy) return;

    const std::string twitchId = trim(U8(GetText(st->twitchId)));
    const std::string kickId = trim(U8(GetText(st->kickId)));
    const std::string kickSecret = trim(U8(GetText(st->kickSecret)));
    const std::string kickRedirect = trim(U8(GetText(st->kickRedirect)));

    if (twitchId.empty() && kickId.empty()) {
        setStatus(st, L"Introduce al menos el Client ID de Twitch o el de Kick.", true);
        return;
    }
    if (!kickId.empty() && kickSecret.empty()) {
        setStatus(st, L"Kick necesita su Client Secret para verificar la aplicación.", true);
        return;
    }
    if (kickId.empty() && !kickSecret.empty()) {
        setStatus(st, L"Has escrito un Client Secret de Kick sin su Client ID.", true);
        return;
    }
    if (!kickId.empty() && kickRedirect.empty()) {
        setStatus(st, L"Indica la Redirect URI de Kick.", true);
        return;
    }
    if (!kickId.empty() && !sp::integrations::isLoopbackRedirect(kickRedirect)) {
        setStatus(st,
                  L"La Redirect URI de Kick debe apuntar a 127.0.0.1 o localhost con un puerto, "
                  L"por ejemplo http://127.0.0.1:7512/callback.",
                  true);
        return;
    }

    sp::integrations::IntegrationConfig cfg;
    cfg.twitch.clientId = twitchId;
    cfg.twitch.clientSecret = st->twitchSecret;
    cfg.twitch.redirectUri = st->twitchRedirect;
    cfg.kick.clientId = kickId;
    cfg.kick.clientSecret = kickSecret;
    cfg.kick.redirectUri = kickRedirect;

    setBusy(hwnd, true);
    setStatus(st, L"Verificando credenciales con Twitch y Kick...");

    sp::integrations::verifyCredentials(
        cfg, *st->http,
        [hwnd, cfg](sp::integrations::CredentialVerification verification) mutable {
            if (!IsWindow(hwnd)) return;
            State* s = stateOf(hwnd);
            if (!s) return;
            setBusy(hwnd, false);
            if (!verification.ok()) {
                std::wstring message;
                auto append = [&message](const sp::integrations::Result& result) {
                    if (result.ok() || result.message.empty()) return;
                    if (!message.empty()) message += L"\n";
                    message += W(result.message);
                };
                append(verification.twitch);
                append(verification.kick);
                if (message.empty()) message = L"No se pudieron verificar las credenciales.";
                setStatus(s, message, true);
                return;
            }

            sp::integrations::markVerified(cfg);
            if (!cfg.credentialsReady()) {
                setStatus(s,
                          L"La configuración no es válida. Revisa el Client ID y la Redirect URI "
                          L"de Kick.",
                          true);
                return;
            }
            if (!sp::integrations::saveIntegrationConfig(sp::defaultConfigPath(), cfg)) {
                setStatus(s,
                          L"Las credenciales son válidas, pero no se pudo guardar el archivo. "
                          L"Revisa los permisos de la carpeta de configuración.",
                          true);
                return;
            }
            if (s->accepted) *s->accepted = true;
            DestroyWindow(hwnd);
        });
}

void createControls(HWND hwnd, State* st) {
    const DWORD ED = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
    const DWORD BT = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
    auto mk = [hwnd](const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w,
                     int h) {
        return CreateWindowExW(0, cls, text, style, x, y, w, h, hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               GetModuleHandleW(nullptr), nullptr);
    };
    auto label = [&](const wchar_t* text, int x, int y, int w, int h) {
        return mk(L"STATIC", text, WS_CHILD | WS_VISIBLE, 0, x, y, w, h);
    };

    const wchar_t* intro = st->required
                               ? L"Para abrir Stream Planner debes registrar y verificar tus propias "
                                 L"credenciales de aplicación de Twitch o Kick. Deja vacía la "
                                 L"plataforma que no uses."
                               : L"Puedes cambiar tus credenciales de aplicación. Se verificarán "
                                 L"antes de guardarlas. Deja vacía la plataforma que no uses.";
    auto* introLabel = label(intro, 16, 12, 600, 40);
    SendMessageW(introLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);

    label(L"Twitch Client ID", 16, 62, 300, 20);
    st->twitchId = mk(L"EDIT", L"", ED, IDC_TWITCH_ID, 16, 82, 590, 26);

    label(L"Kick Client ID", 16, 118, 300, 20);
    st->kickId = mk(L"EDIT", L"", ED, IDC_KICK_ID, 16, 138, 590, 26);

    label(L"Kick Client Secret", 16, 174, 300, 20);
    st->kickSecret = mk(L"EDIT", L"", ED | ES_PASSWORD, IDC_KICK_SECRET, 16, 194, 480, 26);
    st->showSecret = mk(L"BUTTON", L"Mostrar", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                        IDC_SHOW_SECRET, 506, 194, 100, 24);

    label(L"Kick Redirect URI", 16, 230, 300, 20);
    st->kickRedirect = mk(L"EDIT", L"", ED, IDC_KICK_REDIRECT, 16, 250, 590, 26);

    auto* help = label(L"La Redirect URI debe coincidir exactamente con la configurada en tu app "
                       L"de Kick. Las credenciales se guardan solo en este equipo.",
                       16, 286, 600, 34);
    SendMessageW(help, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);

    st->status = label(L"", 16, 330, 600, 52);
    SendMessageW(st->status, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);

    st->cancel = mk(L"BUTTON", L"Cancelar", BT, IDC_CANCEL, 380, 396, 100, 30);
    st->save = mk(L"BUTTON", L"Verificar y guardar", BT | BS_DEFPUSHBUTTON, IDC_SAVE, 490, 396, 130, 30);

    EnumChildWindows(hwnd, setFontProc, reinterpret_cast<LPARAM>(g.font));

    SetFocus(st->twitchId);
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (sp::win::processMessage(msg, wp, lp)) return 0;

    switch (msg) {
        case WM_CREATE: {
            auto* init = reinterpret_cast<Init*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            auto* st = new State;
            st->required = init ? init->required : true;
            st->accepted = init ? init->accepted : nullptr;
            st->http = std::make_unique<WinHttpClient>(hwnd);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

            const sp::integrations::IntegrationConfig cfg =
                sp::integrations::loadIntegrationConfig(sp::defaultConfigPath());
            st->twitchSecret = cfg.twitch.clientSecret;
            st->twitchRedirect = cfg.twitch.redirectUri;
            createControls(hwnd, st);
            setText(st->twitchId, cfg.twitch.clientId);
            setText(st->kickId, cfg.kick.clientId);
            setText(st->kickSecret, cfg.kick.clientSecret);
            setText(st->kickRedirect, cfg.kick.redirectUri);
            SendMessageW(st->kickSecret, EM_SETPASSWORDCHAR, static_cast<WPARAM>(0x25CF), 0);
            return 0;
        }
        case WM_COMMAND: {
            State* st = stateOf(hwnd);
            if (!st) break;
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (id == IDC_SAVE && code == BN_CLICKED) {
                submit(hwnd);
                return 0;
            }
            if (id == IDC_CANCEL && code == BN_CLICKED) {
                SendMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (id == IDC_SHOW_SECRET && code == BN_CLICKED) {
                toggleSecret(st);
                return 0;
            }
            break;
        }
        case WM_CLOSE: {
            State* st = stateOf(hwnd);
            if (st && st->http) st->http->cancelAll();
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            State* st = stateOf(hwnd);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (st) {
                if (st->http) st->http->cancelAll();
                delete st;
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            State* st = stateOf(hwnd);
            if (st && reinterpret_cast<HWND>(lp) == st->status) {
                HDC dc = reinterpret_cast<HDC>(wp);
                SetTextColor(dc, st->statusError ? RGB(180, 0, 32) : RGB(27, 94, 32));
                SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

bool openCredentialsDialog(HWND owner, bool required) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        wc.lpszClassName = L"StreamPlannerCredentials";
        RegisterClassExW(&wc);
        registered = true;
    }

    bool accepted = false;
    Init init;
    init.required = required;
    init.accepted = &accepted;

    RECT rc{};
    if (owner) GetWindowRect(owner, &rc);
    const int w = 640, h = 480;
    const int x = owner ? rc.left + ((rc.right - rc.left) - w) / 2 : CW_USEDEFAULT;
    const int y = owner ? rc.top + ((rc.bottom - rc.top) - h) / 2 : CW_USEDEFAULT;

    if (owner) EnableWindow(owner, FALSE);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                                L"StreamPlannerCredentials", L"Credenciales de Twitch y Kick",
                                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, y, w, h, owner,
                                nullptr, GetModuleHandleW(nullptr), &init);
    if (!hwnd) {
        if (owner) EnableWindow(owner, TRUE);
        return false;
    }

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }

    if (required && !accepted) {
        MessageBoxW(owner, L"Sin credenciales verificadas no se puede abrir Stream Planner.",
                    L"Stream Planner", MB_OK | MB_ICONWARNING);
    }
    return accepted;
}
