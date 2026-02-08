#pragma once

#include <windows.h>

typedef HWND* PHWND;

BOOL CompactPath(HDC hdc, LPWSTR szPath, DWORD dx);
void ResizeWindows(HWND hwndParent, int dxWindow, int dyWindow);
void GetTreeWindows(HWND hwnd, PHWND phwndTree, PHWND phwndDir);
HWND GetTreeFocus(HWND hWnd);
void SwitchDriveSelection(HWND hwndActive);
HICON GetTreeIcon(HWND hWnd);
LRESULT CALLBACK TreeWndProc(HWND hWnd, UINT wMsg, WPARAM wParam, LPARAM lParam);
