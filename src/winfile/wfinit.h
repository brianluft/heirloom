#pragma once

#include <windows.h>
#include "wfdocb.h"

typedef int DRIVE;

void GetInternational();
BOOL InitFileManager(HINSTANCE hInstance, LPWSTR lpCmdLine, int nCmdShow);
void InitExtensions();
void FreeFileManager();
BOOL CreateSavedWindows(LPCWSTR pszInitialDir);
void InitExtensions();
int GetDriveOffset(DRIVE drive);
void InitMenus();
void LoadFailMessage();
UINT FillDocType(PPDOCBUCKET ppDoc, LPCWSTR pszSection, LPCWSTR pszDefault);
BOOL CheckDirExists(LPWSTR szDir);
