#pragma once

#include <windows.h>

void RedoDriveWindows(HWND);
void ChangeFileSystem(DWORD dwOper, LPCWSTR lpPath, LPCWSTR lpTo);
HWND CreateDirWindow(LPWSTR szPath, BOOL bReplaceOpen, HWND hwndActive);
HWND CreateTreeWindow(LPWSTR szPath, int x, int y, int dx, int dy, int dxSplit);
void SwitchToSafeDrive();
DWORD ReadMoveStatus();
void UpdateMoveStatus(DWORD dwEffect);
BOOL AppCommandProc(DWORD id);
void ApplyColumnsToAllWindows();
