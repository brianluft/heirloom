#pragma once

#include <windows.h>

typedef int DRIVE;

LPWSTR pszNextComponent(LPWSTR pszCmdLine);
LPWSTR pszRemoveSurroundingQuotes(LPWSTR p);
void cdecl SetStatusText(int nPane, UINT nFormat, LPCWSTR szFormat, ...);
void RefreshWindow(HWND hwndActive, BOOL bUpdateDriveList, BOOL bFlushCache);
BOOL IsLastWindow();
LPWSTR AddCommasInternal(LPWSTR szBuf, DWORD dw);

BOOL IsValidDisk(DRIVE drive);
LPWSTR GetSelection(int iSelType, PBOOL pbDir);
LPWSTR GetNextFile(LPWSTR pCurSel, LPWSTR szFile, int size);

void SetWindowDirectory();
void SetDlgDirectory(HWND hDlg, LPWSTR pszPath);
void WritePrivateProfileBool(LPCWSTR szKey, BOOL bParam);
BOOL IsWild(LPWSTR lpszPath);
UINT AddBackslash(LPWSTR lpszPath);
void StripBackslash(LPWSTR lpszPath);
void StripFilespec(LPWSTR lpszPath);
void StripPath(LPWSTR lpszPath);
LPWSTR GetExtension(LPWSTR pszFile);
int MyMessageBox(HWND hWnd, DWORD idTitle, DWORD idMessage, DWORD dwStyle);
DWORD ExecProgram(LPCWSTR, LPCWSTR, LPCWSTR, BOOL, BOOL);
BOOL IsNTFSDrive(DRIVE);
BOOL IsCasePreservedDrive(DRIVE);

BOOL IsRemovableDrive(DRIVE);
BOOL IsRemoteDrive(DRIVE);
void SetMDIWindowText(HWND hwnd, LPWSTR szTitle);
int GetMDIWindowText(HWND hwnd, LPWSTR szTitle, int size);
BOOL ResizeSplit(HWND hWnd, int dxSplit);
void CheckEsc(LPWSTR);
BOOL TypeAheadString(WCHAR ch, LPWSTR szT);

void SaveHistoryDir(HWND hwnd, LPWSTR szDir);
BOOL GetPrevHistoryDir(BOOL forward, HWND* phwnd, LPWSTR szDir);

void GetAllDirectories(LPWSTR rgszDirs[]);
BOOL GetDriveDirectory(int iDrive, LPWSTR pszDir);
void GetSelectedDirectory(DRIVE drive, LPWSTR pszDir);
void SaveDirectory(LPWSTR pszDir);
int GetSelectedDrive();
void GetTextStuff(HDC hdc);
int GetHeightFromPointsString(LPCWSTR szPoints);
int GetDrive(HWND hwnd, POINT pt);
void CheckSlashes(LPWSTR);
BOOL IsCDRomDrive(DRIVE drive);
BOOL IsRamDrive(DRIVE drive);
void CleanupMessages();
HWND GetRealParent(HWND hwnd);
LPWSTR GetFullPathInSystemDirectory(LPCWSTR FileName);
HMODULE LoadSystemLibrary(LPCWSTR FileName);
void SetCurrentPathOfWindow(LPWSTR szPath);
