#pragma once

#include <windows.h>

extern const WCHAR kLocationIconClass[];

// Custom message to update the icon's current path.
// lParam = LPCWSTR path (without trailing backslash)
#define LIM_SETPATH (WM_USER + 0)

HWND CreateLocationIcon(HWND hParent, HINSTANCE hInst);
void UpdateLocationIcon(HWND hwndIcon, LPCWSTR pszCurrentPath);
LRESULT CALLBACK LocationIconWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
