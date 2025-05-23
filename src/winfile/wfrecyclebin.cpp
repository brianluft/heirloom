/********************************************************************

   wfrecyclebin.cpp

   Recycle Bin functionality for Windows File Manager

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#include "winfile.h"
#include "wfdir.h"
#include <shellapi.h>

// Include the SHQueryRecycleBin API declaration
#include <shlobj.h>

BOOL bRecycleBinEmpty{ TRUE };
LARGE_INTEGER qRecycleBinSize{};

// Check if the Recycle Bin is empty
BOOL IsRecycleBinEmpty() {
    SHQUERYRBINFO rbInfo;
    rbInfo.cbSize = sizeof(SHQUERYRBINFO);

    if (SUCCEEDED(SHQueryRecycleBin(NULL, &rbInfo))) {
        return (rbInfo.i64NumItems == 0);
    }

    return TRUE;  // Assume empty if we can't query
}

// Get the Recycle Bin size
BOOL GetRecycleBinSize(PLARGE_INTEGER pliSize) {
    SHQUERYRBINFO rbInfo;
    rbInfo.cbSize = sizeof(SHQUERYRBINFO);

    if (SUCCEEDED(SHQueryRecycleBin(NULL, &rbInfo))) {
        pliSize->QuadPart = rbInfo.i64Size;
        return TRUE;
    }

    pliSize->QuadPart = 0;
    return FALSE;
}

// Format the Recycle Bin size for display
void FormatRecycleBinSize(PLARGE_INTEGER pliSize, LPWSTR szBuffer) {
    PutSize(pliSize, szBuffer);
}

// Move a file to the Recycle Bin instead of permanently deleting it
DWORD MoveFileToRecycleBin(LPWSTR pszFile) {
    SHFILEOPSTRUCT shfos;
    WCHAR szFrom[MAXPATHLEN + 1];  // +1 for double null terminator

    // Copy the file path and ensure it's double null-terminated
    lstrcpy(szFrom, pszFile);
    szFrom[lstrlen(szFrom) + 1] = '\0';

    // Initialize the file operation structure
    ZeroMemory(&shfos, sizeof(SHFILEOPSTRUCT));
    shfos.hwnd = hwndFrame;
    shfos.wFunc = FO_DELETE;
    shfos.pFrom = szFrom;
    shfos.fFlags = FOF_ALLOWUNDO | FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI;

    // Perform the file operation
    int ret = SHFileOperation(&shfos);

    // Convert SHFileOperation return value to a Windows error code
    if (ret != 0)
        return ret == 1 ? ERROR_CANCELLED : ERROR_GEN_FAILURE;

    return ERROR_SUCCESS;
}

// Empty the Recycle Bin
BOOL EmptyRecycleBin(HWND hwnd) {
    HRESULT hr = SHEmptyRecycleBin(hwnd, NULL, 0);

    // Update recycle bin state
    bRecycleBinEmpty = TRUE;
    qRecycleBinSize.QuadPart = 0;

    return SUCCEEDED(hr);
}