#pragma once

#include <windows.h>

typedef int DRIVE;
typedef int DRIVEIND;

// Per-instance data for each toolbar (DrivesWndProc) window.
// Heap-allocated on WM_CREATE, stored in GWLP_USERDATA, freed on WM_DESTROY.
struct ChildToolbarData {
    HWND hwndToolbarCtrl;
    HWND hwndLocationCombo;
    HIMAGELIST himlDriveIcons;
    HIMAGELIST himlToolbarButtons;
};

// Get the toolbar HWND for a given tree MDI child (returns NULL for search/non-tree windows)
inline HWND GetChildToolbar(HWND hwndMdiChild) {
    if (!hwndMdiChild)
        return NULL;
    return (HWND)GetWindowLongPtr(hwndMdiChild, GWL_HWND_TOOLBAR);
}

// Get the status bar HWND for a given tree MDI child (returns NULL for search/non-tree windows)
inline HWND GetChildStatusBar(HWND hwndMdiChild) {
    if (!hwndMdiChild)
        return NULL;
    return (HWND)GetWindowLongPtr(hwndMdiChild, GWL_HWND_STATUS);
}

// Get the ChildToolbarData for a toolbar HWND
inline ChildToolbarData* GetToolbarData(HWND hwndToolbar) {
    if (!hwndToolbar)
        return NULL;
    return (ChildToolbarData*)GetWindowLongPtr(hwndToolbar, GWLP_USERDATA);
}

// Get the location combo HWND from a toolbar HWND
inline HWND GetLocationCombo(HWND hwndToolbar) {
    ChildToolbarData* data = GetToolbarData(hwndToolbar);
    return data ? data->hwndLocationCombo : NULL;
}

BOOL CheckDrive(HWND hwnd, DRIVE drive, DWORD dwFunc);
void NewTree(DRIVE drive, HWND hWnd);
void UpdateToolbarState(HWND hwndActive);
void RefreshToolbarDriveList();
void NavigateToPath(LPCWSTR pszPath);
void ConfigureStatusBarParts(HWND hwndChildStatus);
LRESULT CALLBACK DrivesWndProc(HWND hWnd, UINT wMsg, WPARAM wParam, LPARAM lParam);
