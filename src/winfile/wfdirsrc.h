#pragma once

#include <windows.h>

void DrawItem(HWND hwnd, DWORD dwViewOpts, LPDRAWITEMSTRUCT lpLBItem, BOOL bHasFocus);
void DSSetSelection(HWND hwndLB, BOOL bSelect, LPWSTR szSpec, BOOL bSearch);
int FixTabsAndThings(HWND hwndLB, WORD* pwTabs, int iMaxWidthFileName, int iMaxWidthNTFSFileName, DWORD dwViewOpts);
LPWSTR SkipPathHead(LPWSTR lpszPath);
