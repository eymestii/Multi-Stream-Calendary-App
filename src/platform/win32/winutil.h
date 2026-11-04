// Utilidades Win32 genéricas (sin estado de la aplicación).
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>

#include <string>
#include <string_view>

void appendW(std::wstring& out, std::string_view utf8);  // añade UTF-8 -> UTF-16 sin temporales
std::wstring W(std::string_view utf8);
std::string U8(std::wstring_view w);
std::wstring GetText(HWND h);
std::string today();  // YYYY-MM-DD
void warn(HWND owner, const std::wstring& msg);
void copyToClipboard(HWND owner, const std::wstring& s);
