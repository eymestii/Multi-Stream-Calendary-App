// Diálogo Win32 para publicar título/categoría/etiquetas en Twitch y Kick.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

void openPublishDialog(HWND owner);
