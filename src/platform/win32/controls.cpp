#include <algorithm>

#include "platform/win32/app.h"

static HWND mk(const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g.hwnd,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}

static BOOL CALLBACK setFontProc(HWND h, LPARAM f) {
    SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
    return TRUE;
}

void createControls() {
    const DWORD LB = LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER;
    const DWORD ED = ES_AUTOHSCROLL | WS_BORDER;

    // Panel izquierdo
    g.lblSeries = mk(L"STATIC", L"Series", 0, 0);
    g.lbSeries = mk(L"LISTBOX", L"", LB, IDC_SERIES);
    g.btnNew = mk(L"BUTTON", L"Nueva serie", BS_PUSHBUTTON, IDC_NEWS);
    g.btnToggle = mk(L"BUTTON", L"Activa / Finalizada", BS_PUSHBUTTON, IDC_TOGGLE);
    g.btnDelS = mk(L"BUTTON", L"Borrar serie", BS_PUSHBUTTON, IDC_DELS);

    // Panel derecho, en este orden: nombre, etiquetas, plantillas, streams
    g.lblName = mk(L"STATIC", L"Nombre de la serie", 0, 0);
    g.edName = mk(L"EDIT", L"", ED, IDC_EDNAME);

    g.lblTags = mk(L"STATIC", L"Etiquetas   (fijas: {series} {day} {date}; las demás se crean escribiéndolas entre llaves en una plantilla)", 0, 0);
    g.stTags = mk(L"STATIC", L"", SS_ENDELLIPSIS, 0);

    g.lblTitle = mk(L"STATIC", L"Plantilla del título   (ej: [{series}] Día {day} | {topic})", 0, 0);
    g.edTitle = mk(L"EDIT", L"", ED, IDC_EDTITLE);
    g.stTitlePrev = mk(L"STATIC", L"", 0, 0);
    g.stTitleCount = mk(L"STATIC", L"", 0, 0);
    g.lblNotif = mk(L"STATIC", L"Plantilla de la notificación a la comunidad", 0, 0);
    g.edNotif = mk(L"EDIT", L"", ED, IDC_EDNOTIF);
    g.stNotifPrev = mk(L"STATIC", L"", 0, 0);
    g.stNotifCount = mk(L"STATIC", L"", 0, 0);

    g.btnSave = mk(L"BUTTON", L"Guardar serie", BS_PUSHBUTTON, IDC_SAVE);
    g.btnCopyT = mk(L"BUTTON", L"Copiar título", BS_PUSHBUTTON, IDC_COPYT);
    g.btnCopyN = mk(L"BUTTON", L"Copiar notificación", BS_PUSHBUTTON, IDC_COPYN);
    g.btnPublish = mk(L"BUTTON", L"Publicar en plataformas...", BS_PUSHBUTTON, IDC_OPENPUBLISH);
    g.stStats = mk(L"STATIC", L"", 0, 0);

    const wchar_t* heads[3] = {L"Planeado", L"En vivo", L"Realizado"};
    for (int c = 0; c < 3; ++c) {
        g.stCol[c] = mk(L"STATIC", heads[c], 0, 0);
        g.lbCol[c] = mk(L"LISTBOX", L"", LB, IDC_COL0 + c);
    }

    // Campos de etiquetas: se crean todos (ocultos) y se muestran solo los necesarios
    for (size_t i = 0; i < sp::kMaxTags; ++i) {
        g.tagLbl[i] = mk(L"STATIC", L"", 0, 0);
        g.tagEd[i] = mk(L"EDIT", L"", ED, IDC_TAG0 + (int)i);
        SendMessageW(g.tagEd[i], EM_LIMITTEXT, sp::kTwitchLimit, 0);
        ShowWindow(g.tagLbl[i], SW_HIDE);
        ShowWindow(g.tagEd[i], SW_HIDE);
    }

    g.lblDay = mk(L"STATIC", L"Día", 0, 0);
    g.edDay = mk(L"EDIT", L"1", ED | ES_NUMBER, IDC_EDDAY);
    SendMessageW(g.edDay, EM_LIMITTEXT, 5, 0);
    g.lblDate = mk(L"STATIC", L"Fecha", 0, 0);
    g.edDate = mk(L"EDIT", W(today()).c_str(), ED, IDC_EDDATE);
    g.btnAdd = mk(L"BUTTON", L"Añadir stream", BS_PUSHBUTTON, IDC_ADD);
    g.btnLeft = mk(L"BUTTON", L"<", BS_PUSHBUTTON, IDC_LEFT);
    g.btnRight = mk(L"BUTTON", L">", BS_PUSHBUTTON, IDC_RIGHT);
    g.btnDelT = mk(L"BUTTON", L"Borrar", BS_PUSHBUTTON, IDC_DELT);

    SendMessageW(g.edDate, EM_SETCUEBANNER, FALSE, (LPARAM)L"AAAA-MM-DD");

    g.font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    EnumChildWindows(g.hwnd, setFontProc, (LPARAM)g.font);
}

void layout(int cw, int ch) {
    const int m = 10, lw = 250, x0 = lw + 2 * m, aw = std::max(300, cw - x0 - m);
    auto mv = [](HWND h, int x, int y, int w, int hh) { MoveWindow(h, x, y, w, hh, TRUE); };

    // Panel izquierdo
    mv(g.lblSeries, m, 10, lw, 20);
    mv(g.lbSeries, m, 32, lw, ch - 32 - 110);
    mv(g.btnNew, m, ch - 100, lw, 28);
    mv(g.btnToggle, m, ch - 68, lw, 28);
    mv(g.btnDelS, m, ch - 36, lw, 28);

    // Panel derecho: nombre -> etiquetas -> título -> notificación
    mv(g.lblName, x0, 10, aw, 20);
    mv(g.edName, x0, 30, aw, 26);
    mv(g.lblTags, x0, 62, aw, 20);
    mv(g.stTags, x0, 82, aw, 20);
    mv(g.lblTitle, x0, 108, aw, 20);
    mv(g.edTitle, x0, 128, aw, 26);
    mv(g.stTitlePrev, x0, 158, aw, 38);
    mv(g.stTitleCount, x0, 196, aw, 20);
    mv(g.lblNotif, x0, 222, aw, 20);
    mv(g.edNotif, x0, 242, aw, 26);
    mv(g.stNotifPrev, x0, 272, aw, 38);
    mv(g.stNotifCount, x0, 310, aw, 20);

    mv(g.btnSave, x0, 338, 140, 30);
    mv(g.btnCopyT, x0 + 148, 338, 140, 30);
    mv(g.btnCopyN, x0 + 296, 338, 180, 30);
    mv(g.btnPublish, x0 + 484, 338, 200, 30);
    mv(g.stStats, x0, 376, aw, 22);

    // Formulario inferior: filas de etiquetas (según cuántas haya) + fila de día/fecha/botones
    const int rowH = 50, fieldW = 190;
    const int n = (int)std::min(g.tags.size(), sp::kMaxTags);
    const int cols = std::max(1, (aw + m) / (fieldW + m));
    const int tagRows = n ? (n + cols - 1) / cols : 0;
    const int formTop = ch - m - rowH * (tagRows + 1);

    // Kanban: ocupa lo que queda entre las estadísticas y el formulario
    const int top = 406;
    const int colW = (aw - 2 * m) / 3, listH = std::max(60, formTop - (top + 22) - m);
    for (int c = 0; c < 3; ++c) {
        int x = x0 + c * (colW + m);
        mv(g.stCol[c], x, top, colW, 20);
        mv(g.lbCol[c], x, top + 22, colW, listH);
    }

    const int cellW = (aw - (cols - 1) * m) / cols;
    for (int i = 0; i < n; ++i) {
        int x = x0 + (i % cols) * (cellW + m), y = formTop + (i / cols) * rowH;
        mv(g.tagLbl[i], x, y, cellW, 18);
        mv(g.tagEd[i], x, y + 20, cellW, 26);
    }

    const int by = formTop + tagRows * rowH;
    mv(g.lblDay, x0, by, 60, 18);
    mv(g.edDay, x0, by + 20, 60, 26);
    mv(g.lblDate, x0 + 68, by, 120, 18);
    mv(g.edDate, x0 + 68, by + 20, 120, 26);
    int x = x0 + 196;
    mv(g.btnAdd, x, by + 18, 120, 28); x += 128;
    mv(g.btnLeft, x, by + 18, 40, 28); x += 48;
    mv(g.btnRight, x, by + 18, 40, 28); x += 48;
    mv(g.btnDelT, x, by + 18, 90, 28);
}

void relayout() {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    layout(rc.right, rc.bottom);
}
