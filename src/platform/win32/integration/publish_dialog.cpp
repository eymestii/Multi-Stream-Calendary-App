#include "publish_dialog.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

#include "core/integrations/api_util.h"
#include "platform/win32/app.h"

namespace {

enum {
    IDC_PLATFORM = 5001,
    IDC_STATUS,
    IDC_CONNECT,
    IDC_REFRESH,
    IDC_NOTIF,
    IDC_TITLE,
    IDC_SEARCH,
    IDC_FIND,
    IDC_MORE,
    IDC_CATEGORIES,
    IDC_CAT_LABEL,
    IDC_TAG_EDIT,
    IDC_TAG_ADD,
    IDC_TAGS,
    IDC_TAG_DEL,
    IDC_PUBLISH,
    IDC_RESULT,
    IDC_CLOSE
};

struct State {
    HWND platform = nullptr;
    HWND status = nullptr;
    HWND connect = nullptr;
    HWND refresh = nullptr;
    HWND notif = nullptr;
    HWND title = nullptr;
    HWND search = nullptr;
    HWND find = nullptr;
    HWND more = nullptr;
    HWND categories = nullptr;
    HWND catLabel = nullptr;
    HWND tagEdit = nullptr;
    HWND tagAdd = nullptr;
    HWND tags = nullptr;
    HWND tagDel = nullptr;
    HWND publish = nullptr;
    HWND result = nullptr;

    int platformIndex = 0;  // 0 Twitch, 1 Kick
    int streamId = 0;
    bool connected = false;
    bool loading = false;
    bool statusError = false;
    bool channelLoaded = false;
    std::vector<std::string> tags;
    std::vector<std::string> catIds;
    std::vector<std::string> catNames;
    std::string catId;
    std::string catName;
    std::string cursor;
    std::string previewTitle;
};

State* stateOf(HWND hwnd) { return reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }

sp::integrations::IBroadcaster& providerFor(int platform) {
    return g.integrations->provider(platform == 1 ? sp::integrations::Platform::Kick
                                                  : sp::integrations::Platform::Twitch);
}

const char* keyFor(int platform) { return platform == 1 ? "kick" : "twitch"; }

std::wstring wstr(const std::string& s) { return W(s); }

void setText(HWND h, const std::string& s) { SetWindowTextW(h, wstr(s).c_str()); }
void setText(HWND h, const std::wstring& s) { SetWindowTextW(h, s.c_str()); }

std::string getText(HWND h) { return U8(GetText(h)); }

void setStatus(State* st, const std::string& text, bool error = false) {
    st->statusError = error;
    setText(st->status, text);
    InvalidateRect(st->status, nullptr, TRUE);
}

void refreshTags(State* st) {
    SendMessageW(st->tags, LB_RESETCONTENT, 0, 0);
    for (const auto& t : st->tags)
        SendMessageW(st->tags, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wstr(t).c_str()));
}

void updateCatLabel(State* st, const std::string& id, const std::string& name) {
    if (id.empty()) setText(st->catLabel, L"Categoría: (sin seleccionar)");
    else setText(st->catLabel, L"Categoría: " + wstr(name) + L"  [" + wstr(id) + L"]");
}

void saveDraft(HWND hwnd, bool published, const std::string& error) {
    State* st = stateOf(hwnd);
    if (!st || st->streamId <= 0) return;
    sp::Stream* stream = g.store.findStream(st->streamId);
    if (!stream) return;
    sp::StreamTarget& target = stream->target(keyFor(st->platformIndex));
    target.platform = keyFor(st->platformIndex);
    target.title = getText(st->title);
    if (!st->catId.empty()) {
        target.categoryId = st->catId;
        target.categoryName = st->catName;
    }
    target.tags = st->tags;
    if (published) {
        target.lastPublishedAt = sp::integrations::nowIso8601();
        target.lastError.clear();
    } else if (!error.empty()) {
        target.lastError = error;
    }
    g.store.save(g.dataPath);
}

void loadPlatform(HWND hwnd) {
    State* st = stateOf(hwnd);
    if (!st) return;
    sp::Stream* stream = g.store.findStream(st->streamId);
    st->tags.clear();
    st->catIds.clear();
    st->catNames.clear();
    st->cursor.clear();
    st->connected = false;
    st->catId.clear();
    st->catName.clear();
    st->channelLoaded = false;
    SendMessageW(st->categories, LB_RESETCONTENT, 0, 0);
    setText(st->catLabel, L"Categoría: (sin seleccionar)");

    std::string title = st->previewTitle;
    if (stream) {
        if (const sp::StreamTarget* t = stream->findTarget(keyFor(st->platformIndex))) {
            if (!t->title.empty()) title = t->title;
            st->tags = t->tags;
            st->catId = t->categoryId;
            st->catName = t->categoryName;
            st->channelLoaded = !t->tags.empty();
            updateCatLabel(st, t->categoryId, t->categoryName);
        }
    }
    st->loading = true;
    setText(st->title, title);
    st->loading = false;
    refreshTags(st);
    setStatus(st, "Comprobando sesión...");
    setText(st->connect, L"Conectar");

    const int platform = st->platformIndex;
    providerFor(platform).restore(
        [hwnd, platform](const sp::integrations::Result& result,
                         const sp::integrations::Account& account) {
            if (!IsWindow(hwnd)) return;
            State* s = stateOf(hwnd);
            if (!s || s->platformIndex != platform) return;
            if (!result.ok()) {
                s->connected = false;
                setText(s->connect, L"Conectar");
                setStatus(s, result.message.empty() ? "No conectado." : result.message);
                return;
            }
            s->connected = true;
            setText(s->connect, L"Desconectar");
            setStatus(s, "Conectado como " +
                             (account.displayName.empty() ? account.login : account.displayName));
            providerFor(platform)
                .fetchChannel([hwnd, platform](const sp::integrations::Result& r,
                                     const sp::integrations::ChannelSnapshot& snap) {
                    if (!IsWindow(hwnd)) return;
                    State* s2 = stateOf(hwnd);
                    if (!s2 || s2->platformIndex != platform) return;
                    if (!r.ok()) {
                        setStatus(s2, r.message, true);
                        return;
                    }
                    s2->loading = true;
                    if (!snap.title.empty()) setText(s2->title, snap.title);
                    s2->loading = false;
                    if (snap.tagsKnown) {
                        s2->tags = snap.tags;
                        s2->channelLoaded = true;
                        refreshTags(s2);
                    }
                    s2->catId = snap.categoryId;
                    s2->catName = snap.categoryName;
                    updateCatLabel(s2, snap.categoryId, snap.categoryName);
                    setStatus(s2, "Datos oficiales cargados.");
                });
            providerFor(platform)
                .searchCategories({}, {},
                                  [hwnd, platform](const sp::integrations::Result& r,
                                         const sp::integrations::CategoryPage& page) {
                                      if (!IsWindow(hwnd)) return;
                                      State* s2 = stateOf(hwnd);
                                      if (!s2 || s2->platformIndex != platform) return;
                                      if (!r.ok()) return;
                                      SendMessageW(s2->categories, LB_RESETCONTENT, 0, 0);
                                      s2->catIds.clear();
                                      s2->catNames.clear();
                                      for (const auto& c : page.items) {
                                          s2->catIds.push_back(c.id);
                                          s2->catNames.push_back(c.name);
                                          SendMessageW(s2->categories, LB_ADDSTRING, 0,
                                                       reinterpret_cast<LPARAM>(wstr(c.name).c_str()));
                                      }
                                      s2->cursor = page.cursor;
                                  });
        });
}

void connectPlatform(HWND hwnd) {
    State* st = stateOf(hwnd);
    if (!st) return;
    if (st->connected) {
        providerFor(st->platformIndex).disconnect();
        st->connected = false;
        setText(st->connect, L"Conectar");
        setStatus(st, "Desconectado.");
        return;
    }
    if (providerFor(st->platformIndex).authState() == sp::integrations::AuthState::Connecting) {
        providerFor(st->platformIndex).cancel();
        setText(st->connect, L"Conectar");
        setStatus(st, "Autorización cancelada.");
        return;
    }
    setStatus(st, "Esperando autorización...");
    setText(st->connect, L"Cancelar");
    const int platform = st->platformIndex;
    providerFor(platform).connect(
        [hwnd, platform](const sp::integrations::ConnectResult& result) {
            if (!IsWindow(hwnd)) return;
            State* s = stateOf(hwnd);
            if (!s || s->platformIndex != platform) return;
            if (result.pending) {
                std::string text = "Autoriza en " + result.verificationUri;
                if (!result.userCode.empty()) text += " con el código " + result.userCode;
                setStatus(s, text);
                setText(s->connect, L"Cancelar");
                return;
            }
            if (!result.result.ok()) {
                s->connected = false;
                setText(s->connect, L"Conectar");
                setStatus(s, result.result.message, true);
                return;
            }
            s->connected = true;
            setText(s->connect, L"Desconectar");
            setStatus(s, "Conectado como " +
                             (result.account.displayName.empty() ? result.account.login
                                                                 : result.account.displayName));
            providerFor(platform).fetchChannel(
                [hwnd, platform](const sp::integrations::Result& r,
                                 const sp::integrations::ChannelSnapshot& snap) {
                    if (!IsWindow(hwnd)) return;
                    State* s2 = stateOf(hwnd);
                    if (!s2 || s2->platformIndex != platform || !r.ok()) return;
                    s2->loading = true;
                    if (!snap.title.empty()) setText(s2->title, snap.title);
                    s2->loading = false;
                    if (snap.tagsKnown) {
                        s2->tags = snap.tags;
                        s2->channelLoaded = true;
                        refreshTags(s2);
                    }
                    s2->catId = snap.categoryId;
                    s2->catName = snap.categoryName;
                    updateCatLabel(s2, snap.categoryId, snap.categoryName);
                });
        });
}

void findCategories(HWND hwnd, bool more) {
    State* st = stateOf(hwnd);
    if (!st) return;
    const std::string query = getText(st->search);
    const std::string cursor = more ? st->cursor : std::string();
    setStatus(st, "Buscando categorías...");
    const int platform = st->platformIndex;
    providerFor(platform)
        .searchCategories(query, cursor,
                          [hwnd, more, platform](const sp::integrations::Result& r,
                                       const sp::integrations::CategoryPage& page) {
                              if (!IsWindow(hwnd)) return;
                              State* s = stateOf(hwnd);
                              if (!s || s->platformIndex != platform) return;
                              if (!r.ok()) {
                                  setStatus(s, r.message, true);
                                  return;
                              }
                              if (!more) {
                                  SendMessageW(s->categories, LB_RESETCONTENT, 0, 0);
                                  s->catIds.clear();
                                  s->catNames.clear();
                              }
                              for (const auto& c : page.items) {
                                  s->catIds.push_back(c.id);
                                  s->catNames.push_back(c.name);
                                  SendMessageW(s->categories, LB_ADDSTRING, 0,
                                               reinterpret_cast<LPARAM>(wstr(c.name).c_str()));
                              }
                              s->cursor = page.cursor;
                              setStatus(s, std::to_string(s->catIds.size()) + " categorías.");
                          });
}

void addTag(HWND hwnd) {
    State* st = stateOf(hwnd);
    if (!st) return;
    std::string tag = getText(st->tagEdit);
    while (!tag.empty() && (tag.back() == '\r' || tag.back() == '\n' || tag.back() == ' ')) tag.pop_back();
    while (!tag.empty() && tag.front() == ' ') tag.erase(tag.begin());
    if (tag.empty()) return;
    if (tag.find(' ') != std::string::npos) {
        setStatus(st, "Las etiquetas no pueden contener espacios.", true);
        return;
    }
    const auto caps = providerFor(st->platformIndex).capabilities();
    if (caps.maxTags && st->tags.size() >= caps.maxTags) {
        setStatus(st, "Máximo " + std::to_string(caps.maxTags) + " etiquetas.", true);
        return;
    }
    if (caps.maxTagLen && sp::utf8Length(tag) > caps.maxTagLen) {
        setStatus(st, "La etiqueta supera " + std::to_string(caps.maxTagLen) + " caracteres.", true);
        return;
    }
    for (const auto& existing : st->tags) {
        if (existing.size() == tag.size() &&
            std::equal(existing.begin(), existing.end(), tag.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
            })) {
            setStatus(st, "Esa etiqueta ya existe.", true);
            return;
        }
    }
    st->tags.push_back(tag);
    st->channelLoaded = true;
    refreshTags(st);
    SetWindowTextW(st->tagEdit, L"");
}

void removeTag(HWND hwnd) {
    State* st = stateOf(hwnd);
    if (!st) return;
    const int row = static_cast<int>(SendMessageW(st->tags, LB_GETCURSEL, 0, 0));
    if (row < 0 || row >= static_cast<int>(st->tags.size())) return;
    st->tags.erase(st->tags.begin() + row);
    st->channelLoaded = true;
    refreshTags(st);
}

void publish(HWND hwnd) {
    State* st = stateOf(hwnd);
    if (!st) return;
    if (!st->connected) {
        setStatus(st, "Conecta la plataforma antes de publicar.", true);
        return;
    }
    const std::string title = getText(st->title);
    if (title.empty()) {
        setStatus(st, "Escribe un título antes de publicar.", true);
        return;
    }
    if (st->catId.empty()) {
        setStatus(st, "Elige una categoría antes de publicar.", true);
        return;
    }
    sp::integrations::PublishRequest req;
    req.title = title;
    req.categoryId = st->catId;
    req.categoryName = st->catName;
    req.tags = st->tags;
    req.applyTitle = true;
    req.applyCategory = true;
    req.applyTags = providerFor(st->platformIndex).capabilities().writeTags && st->channelLoaded;
    setText(st->result, L"Publicando...");
    const int platform = st->platformIndex;
    providerFor(platform).publish(
        req, [hwnd, platform](const sp::integrations::Result& r) {
            if (!IsWindow(hwnd)) return;
            State* s = stateOf(hwnd);
            if (!s || s->platformIndex != platform) return;
            if (!r.ok()) {
                setText(s->result, wstr(r.message));
                setStatus(s, r.message, true);
                saveDraft(hwnd, false, r.message);
                return;
            }
            setText(s->result, L"Publicado correctamente.");
            setStatus(s, "Publicado correctamente.");
            saveDraft(hwnd, true, {});
        });
}

BOOL CALLBACK applyFontProc(HWND child, LPARAM font) {
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
    return TRUE;
}

void createControls(HWND hwnd, State* st) {
    const DWORD ED = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
    const DWORD BT = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
    const DWORD LB = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY;
    auto mk = [hwnd](const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w,
                     int h) {
        return CreateWindowExW(0, cls, text, style, x, y, w, h, hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               GetModuleHandleW(nullptr), nullptr);
    };
    auto label = [&](const wchar_t* text, int x, int y, int w, int h, DWORD style = 0) {
        return mk(L"STATIC", text, WS_CHILD | WS_VISIBLE | style, 0, x, y, w, h);
    };

    label(L"Plataforma", 16, 16, 100, 20);
    st->platform = mk(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST, IDC_PLATFORM,
                      120, 14, 180, 120);
    SendMessageW(st->platform, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Twitch"));
    SendMessageW(st->platform, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Kick"));
    SendMessageW(st->platform, CB_SETCURSEL, 0, 0);

    st->status = label(L"", 16, 46, 530, 36, SS_ENDELLIPSIS);
    st->connect = mk(L"BUTTON", L"Conectar", BT, IDC_CONNECT, 556, 42, 110, 28);
    st->refresh = mk(L"BUTTON", L"Leer canal", BT, IDC_REFRESH, 674, 42, 110, 28);

    label(L"Notificación (solo local; no se publica por API)", 16, 88, 500, 20);
    st->notif = label(L"", 16, 108, 768, 34, SS_ENDELLIPSIS);

    label(L"Título oficial", 16, 150, 200, 20);
    st->title = mk(L"EDIT", L"", ED, IDC_TITLE, 16, 170, 768, 26);

    label(L"Buscar categoría", 16, 206, 200, 20);
    st->search = mk(L"EDIT", L"", ED, IDC_SEARCH, 16, 226, 560, 26);
    st->find = mk(L"BUTTON", L"Buscar", BT, IDC_FIND, 584, 226, 90, 26);
    st->more = mk(L"BUTTON", L"Más", BT, IDC_MORE, 682, 226, 100, 26);
    st->categories = mk(L"LISTBOX", L"", LB, IDC_CATEGORIES, 16, 258, 768, 112);
    st->catLabel = label(L"Categoría: (sin seleccionar)", 16, 376, 768, 20);

    label(L"Etiquetas del canal", 16, 404, 200, 20);
    st->tagEdit = mk(L"EDIT", L"", ED, IDC_TAG_EDIT, 16, 424, 560, 26);
    st->tagAdd = mk(L"BUTTON", L"Añadir", BT, IDC_TAG_ADD, 584, 424, 90, 26);
    st->tagDel = mk(L"BUTTON", L"Quitar", BT, IDC_TAG_DEL, 682, 424, 100, 26);
    st->tags = mk(L"LISTBOX", L"", LB, IDC_TAGS, 16, 456, 768, 72);

    st->publish = mk(L"BUTTON", L"Publicar", BT, IDC_PUBLISH, 16, 542, 150, 32);
    st->result = label(L"", 180, 542, 604, 32);
    mk(L"BUTTON", L"Cerrar", BT, IDC_CLOSE, 674, 584, 110, 28);

    EnumChildWindows(hwnd, applyFontProc, reinterpret_cast<LPARAM>(g.font));
}

void onCommand(HWND hwnd, int id, int code) {
    State* st = stateOf(hwnd);
    if (!st) return;
    switch (id) {
        case IDC_PLATFORM:
            if (code == CBN_SELCHANGE) {
                const int next = static_cast<int>(SendMessageW(st->platform, CB_GETCURSEL, 0, 0));
                if (next == st->platformIndex) return;
                saveDraft(hwnd, false, {});
                st->platformIndex = next;
                loadPlatform(hwnd);
            }
            break;
        case IDC_CONNECT: connectPlatform(hwnd); break;
        case IDC_REFRESH:
            if (st->connected) {
                const int platform = st->platformIndex;
                providerFor(platform).fetchChannel(
                [hwnd, platform](const sp::integrations::Result& r,
                       const sp::integrations::ChannelSnapshot& snap) {
                    if (!IsWindow(hwnd)) return;
                    State* s = stateOf(hwnd);
                    if (!s || s->platformIndex != platform || !r.ok()) return;
                    s->loading = true;
                    if (!snap.title.empty()) setText(s->title, snap.title);
                    s->loading = false;
                    if (snap.tagsKnown) {
                        s->tags = snap.tags;
                        s->channelLoaded = true;
                        refreshTags(s);
                    }
                    s->catId = snap.categoryId;
                    s->catName = snap.categoryName;
                    updateCatLabel(s, snap.categoryId, snap.categoryName);
                });
            } else {
                setStatus(st, "Conecta la plataforma primero.", true);
            }
            break;
        case IDC_FIND: findCategories(hwnd, false); break;
        case IDC_MORE: findCategories(hwnd, true); break;
        case IDC_CATEGORIES:
            if (code == LBN_SELCHANGE) {
                const int row = static_cast<int>(SendMessageW(st->categories, LB_GETCURSEL, 0, 0));
                if (row >= 0 && row < static_cast<int>(st->catIds.size())) {
                    st->catId = st->catIds[static_cast<std::size_t>(row)];
                    st->catName = st->catNames[static_cast<std::size_t>(row)];
                    updateCatLabel(st, st->catId, st->catName);
                }
            }
            break;
        case IDC_TAG_ADD: addTag(hwnd); break;
        case IDC_TAG_DEL: removeTag(hwnd); break;
        case IDC_PUBLISH: publish(hwnd); break;
        case IDC_CLOSE: SendMessageW(hwnd, WM_CLOSE, 0, 0); break;
    }
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            auto* st = new State;
            st->streamId = g.selStream;
            const sp::Stream* stream = g.store.findStream(st->streamId);
            if (stream) {
                // Título del plan actual (vista previa de la ventana principal).
                st->previewTitle = U8(GetText(g.stTitlePrev));
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            createControls(hwnd, st);
            setText(st->notif, U8(GetText(g.stNotifPrev)));
            loadPlatform(hwnd);
            return 0;
        }
        case WM_COMMAND:
            onCommand(hwnd, LOWORD(wp), HIWORD(wp));
            return 0;
        case WM_CLOSE:
            saveDraft(hwnd, false, {});
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: {
            State* st = stateOf(hwnd);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete st;
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            State* st = stateOf(hwnd);
            if (st && reinterpret_cast<HWND>(lp) == st->status) {
                HDC dc = reinterpret_cast<HDC>(wp);
                SetTextColor(dc, st->statusError ? RGB(200, 0, 0) : RGB(30, 100, 30));
                SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void openPublishDialog(HWND owner) {
    if (!g.integrations) {
        warn(owner, L"El módulo de integraciones no está disponible.");
        return;
    }
    if (!g.store.findStream(g.selStream)) {
        warn(owner, L"Selecciona un stream del kanban antes de publicar.");
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        wc.lpszClassName = L"StreamPlannerPublish";
        RegisterClassExW(&wc);
        registered = true;
    }

    RECT rc;
    GetWindowRect(owner, &rc);
    const int w = 800, h = 650;
    const int x = rc.left + ((rc.right - rc.left) - w) / 2;
    const int y = rc.top + ((rc.bottom - rc.top) - h) / 2;

    EnableWindow(owner, FALSE);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"StreamPlannerPublish",
                                L"Publicar en plataformas", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        EnableWindow(owner, TRUE);
        return;
    }

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
}
