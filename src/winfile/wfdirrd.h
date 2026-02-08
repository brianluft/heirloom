#pragma once

#include <windows.h>
#include "wfmem.h"

BOOL InitDirRead();
void DestroyDirRead();
LPXDTALINK CreateDTABlock(HWND hwnd, LPWSTR pPath, BOOL bDontSteal);
void FreeDTA(HWND hwnd);
void DirReadDestroyWindow(HWND hwndDir);
LPXDTALINK DirReadDone(HWND hwndDir, LPXDTALINK lpStart, int iError);
void BuildDocumentString();
void BuildDocumentStringWorker();
