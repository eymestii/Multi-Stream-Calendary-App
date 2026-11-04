// Estado global de la UI, IDs de controles y declaraciones compartidas entre módulos.
#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/integrations/integration_manager.h"
#include "core/integrations/token_store.h"
#include "core/model.h"
#include "platform/win32/integration/win_platform_services.h"
#include "platform/win32/winutil.h"

enum {
    IDC_SERIES = 100, IDC_NEWS, IDC_TOGGLE, IDC_DELS,
    IDC_EDNAME = 120, IDC_EDTITLE, IDC_EDNOTIF, IDC_SAVE, IDC_COPYT, IDC_COPYN,
    IDC_COL0 = 200,  // 200, 201, 202 = Planeado, En vivo, Realizado
    IDC_EDDAY = 210, IDC_EDDATE, IDC_ADD, IDC_LEFT, IDC_RIGHT, IDC_DELT,
    IDC_TAG0 = 300,  // 300..309 = campos de valor de las etiquetas
    IDC_OPENPUBLISH = 400, IDC_CREDENTIALS
};

struct App {
    sp::Store store;
    std::filesystem::path dataPath;
    std::filesystem::path legacyDataPath;
    int curSeries = 0;
    int selStream = 0;
    bool loading = true;
    bool titleOver = false, notifOver = false, tagsOver = false;
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    HWND lblSeries, lbSeries, btnNew, btnToggle, btnDelS;
    HWND lblName, edName, lblTags, stTags;
    HWND lblTitle, edTitle, stTitlePrev, stTitleCount;
    HWND lblNotif, edNotif, stNotifPrev, stNotifCount;
    HWND btnSave, btnCopyT, btnCopyN, stStats;
    HWND stCol[3], lbCol[3];
    HWND tagLbl[sp::kMaxTags], tagEd[sp::kMaxTags];  // un campo por etiqueta personalizada
    HWND lblDay, edDay, lblDate, edDate, btnAdd, btnLeft, btnRight, btnDelT;
    HWND btnPublish;  // abre el diálogo de publicación en Twitch/Kick

    std::vector<std::string> tags;  // etiquetas personalizadas detectadas en las plantillas (máx. kMaxTags)
    std::wstring tagsInfo;          // último texto mostrado en stTags (evita redibujar sin cambios)

    // Integraciones oficiales. Se crean en WM_CREATE y viven hasta el cierre.
    // El orden de destrucción (inverso) garantiza que el gestor muera antes que
    // los servicios y el almacén de tokens que referencia.
    std::unique_ptr<WinPlatformServices> platformServices;
    std::unique_ptr<sp::integrations::FileTokenStore> tokenStore;
    std::unique_ptr<sp::integrations::IntegrationManager> integrations;
    // Gestores anteriores tras cambiar credenciales. Se conservan vivos hasta
    // el cierre para que ninguna respuesta ya encolada use memoria liberada.
    std::vector<std::unique_ptr<sp::integrations::IntegrationManager>> retiredIntegrations;
};
extern App g;  // definido en main.cpp

// Relee integrations.json y crea un gestor nuevo (las credenciales cambiaron).
void reloadIntegrations();

inline void persist() { g.store.save(g.dataPath); }
inline sp::Series* cur() { return g.store.findSeries(g.curSeries); }

// controls.cpp - creación y posicionado de controles
void createControls();
void layout(int cw, int ch);
void relayout();

// view.cpp - refrescos de UI
void refreshSeriesList();
void refreshKanban();
void updateStats();
void updatePreview();
void syncTagFields(bool force = false);  // detecta {etiquetas} en las plantillas y ajusta los campos
int readDay();                           // día escrito en el formulario; 0 si no es válido
void loadFields();
void selectSeries(int id);

// actions.cpp - acciones y despacho de WM_COMMAND
void onCommand(int id, int code);
