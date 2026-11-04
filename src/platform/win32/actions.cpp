// Acciones del usuario y despacho de WM_COMMAND.
#include "platform/win32/app.h"

#include "platform/win32/integration/credentials_dialog.h"
#include "platform/win32/integration/publish_dialog.h"

static void actNewSeries() {
    int id = g.store.addSeries("Nueva serie");
    persist();
    selectSeries(id);
    SetFocus(g.edName);
    SendMessageW(g.edName, EM_SETSEL, 0, -1);
}

static void actSaveSeries() {
    sp::Series* s = cur();
    if (!s) return;
    if (g.titleOver || g.notifOver) {
        warn(g.hwnd, L"El título o la notificación superan el límite de Twitch. Acórtalos antes de guardar.");
        return;
    }
    if (g.tagsOver) {
        warn(g.hwnd, L"Las plantillas usan más de " + std::to_wstring(sp::kMaxTags) +
                     L" etiquetas personalizadas. Quita algunas antes de guardar.");
        return;
    }
    s->name = U8(GetText(g.edName));
    s->titleTemplate = U8(GetText(g.edTitle));
    s->notifTemplate = U8(GetText(g.edNotif));
    persist();
    refreshSeriesList();
    updatePreview();
}

static void actToggleActive() {
    sp::Series* s = cur();
    if (!s) return;
    s->active = !s->active;
    persist();
    refreshSeriesList();
    updateStats();
}

static void actDeleteSeries() {
    sp::Series* s = cur();
    if (!s) return;
    if (MessageBoxW(g.hwnd, L"¿Borrar la serie y todos sus streams?", L"Stream Planner",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    g.store.removeSeries(s->id);
    persist();
    selectSeries(g.store.series.empty() ? 0 : g.store.series.front().id);
}

static void actAddStream() {
    sp::Series* s = cur();
    if (!s) { warn(g.hwnd, L"Primero crea una serie."); return; }
    const int day = readDay();
    if (day <= 0) {
        warn(g.hwnd, L"Escribe un día válido (1 o más).");
        SetFocus(g.edDay);
        return;
    }
    std::string date = U8(GetText(g.edDate));
    if (date.empty()) date = today();

    // Valores de las etiquetas que tengan texto
    std::vector<sp::TagValue> tags;
    for (size_t i = 0; i < g.tags.size() && i < sp::kMaxTags; ++i) {
        std::string v = U8(GetText(g.tagEd[i]));
        if (!v.empty()) tags.push_back({g.tags[i], std::move(v)});
    }

    // Validar contra el límite de Twitch usando la plantilla GUARDADA de la serie
    sp::Stream probe;
    probe.number = day;
    probe.date = date;
    probe.tags = tags;
    size_t lt = sp::utf8Length(sp::render(s->titleTemplate, *s, probe));
    size_t ln = sp::utf8Length(sp::render(s->notifTemplate, *s, probe));
    if (lt > sp::kTwitchLimit || ln > sp::kTwitchLimit) {
        warn(g.hwnd, L"Con esos datos el título queda en " + std::to_wstring(lt) + L" caracteres y la notificación en " +
             std::to_wstring(ln) + L" (máximo " + std::to_wstring(sp::kTwitchLimit) + L"). Acorta los valores.");
        return;
    }
    g.selStream = g.store.addStream(s->id, day, date, std::move(tags));
    persist();

    g.loading = true;  // limpiar valores sin disparar vistas previas intermedias
    for (size_t i = 0; i < g.tags.size() && i < sp::kMaxTags; ++i) SetWindowTextW(g.tagEd[i], L"");
    g.loading = false;

    refreshKanban();
    updatePreview();
    updateStats();
}

static void actMoveStream(int dir) {
    sp::Stream* t = g.store.findStream(g.selStream);
    if (!t) return;
    int ns = (int)t->status + dir;
    if (ns < 0 || ns > 2) return;
    t->status = (sp::Status)ns;
    persist();
    refreshKanban();
    updateStats();
}

static void actDeleteStream() {
    if (!g.store.findStream(g.selStream)) return;
    g.store.removeStream(g.selStream);
    g.selStream = 0;
    persist();
    refreshKanban();
    updatePreview();
    updateStats();
}

void onCommand(int id, int code) {
    if (id == IDC_CREDENTIALS) {
        if (openCredentialsDialog(g.hwnd, false)) reloadIntegrations();
        return;
    }
    if (id == IDC_SERIES && code == LBN_SELCHANGE) {
        int i = (int)SendMessageW(g.lbSeries, LB_GETCURSEL, 0, 0);
        if (i >= 0) selectSeries((int)SendMessageW(g.lbSeries, LB_GETITEMDATA, i, 0));
    } else if (id >= IDC_COL0 && id < IDC_COL0 + 3 && code == LBN_SELCHANGE) {
        int c = id - IDC_COL0;
        int i = (int)SendMessageW(g.lbCol[c], LB_GETCURSEL, 0, 0);
        if (i >= 0) {
            g.selStream = (int)SendMessageW(g.lbCol[c], LB_GETITEMDATA, i, 0);
            for (int k = 0; k < 3; ++k)
                if (k != c) SendMessageW(g.lbCol[k], LB_SETCURSEL, (WPARAM)-1, 0);
            updatePreview();
        }
    } else if ((id == IDC_EDNAME || id == IDC_EDTITLE || id == IDC_EDNOTIF) && code == EN_CHANGE && !g.loading) {
        if (id != IDC_EDNAME) syncTagFields();  // las etiquetas se crean al escribir {nombre}
        updatePreview();
    } else if ((id == IDC_EDDAY || id == IDC_EDDATE || (id >= IDC_TAG0 && id < IDC_TAG0 + (int)sp::kMaxTags)) &&
               code == EN_CHANGE && !g.loading) {
        updatePreview();
    } else if (code == BN_CLICKED) {
        switch (id) {
            case IDC_NEWS: actNewSeries(); break;
            case IDC_TOGGLE: actToggleActive(); break;
            case IDC_DELS: actDeleteSeries(); break;
            case IDC_SAVE: actSaveSeries(); break;
            case IDC_COPYT: copyToClipboard(g.hwnd, GetText(g.stTitlePrev)); break;
            case IDC_COPYN: copyToClipboard(g.hwnd, GetText(g.stNotifPrev)); break;
            case IDC_ADD: actAddStream(); break;
            case IDC_LEFT: actMoveStream(-1); break;
            case IDC_RIGHT: actMoveStream(+1); break;
            case IDC_DELT: actDeleteStream(); break;
            case IDC_OPENPUBLISH: openPublishDialog(g.hwnd); break;
        }
    }
}
