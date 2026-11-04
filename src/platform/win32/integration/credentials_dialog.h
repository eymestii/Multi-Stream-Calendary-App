// Diálogo Win32 de registro/verificación de credenciales de aplicación.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Muestra el diálogo de forma modal. Devuelve true si se verificaron y
// guardaron las credenciales. `required` cambia el texto de la puerta de
// arranque (sin credenciales no se abre la aplicación).
bool openCredentialsDialog(HWND owner, bool required);
