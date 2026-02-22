/********************************************************************

   wfdlgs3.c

   Windows File System Dialog procedures

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#include "winfile.h"
#include "lfn.h"
#include "wfcopy.h"
#include "resize.h"
#include "wfutil.h"
#include "wftree.h"
#include "wfsearch.h"
#include "stringconstants.h"
#include <shlobj.h>

#define LABEL_NTFS_MAX 32
#define LABEL_FAT_MAX 11
#define CCH_VERSION 40
#define CCH_DRIVE 3

DWORD WINAPI FormatDrive(IN PVOID ThreadParameter);
DWORD WINAPI CopyDiskette(IN PVOID ThreadParameter);
void SwitchToSafeDrive();
void MDIClientSizeChange(HWND hwndActive, int iFlags);

BOOL GetProductVersion(WORD* pwMajor, WORD* pwMinor, WORD* pwBuild, WORD* pwRevision);

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  AboutDlgProc() -  DialogProc callback function for ABOUTDLG             */
/*                                                                          */
/*--------------------------------------------------------------------------*/

INT_PTR
CALLBACK
AboutDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    WORD wMajorVersion = 0;
    WORD wMinorVersion = 0;
    WORD wBuildNumber = 0;
    WORD wRevisionNumber = 0;
    WCHAR szVersion[CCH_VERSION] = { 0 };

    switch (wMsg) {
        case WM_INITDIALOG:
            if (GetProductVersion(&wMajorVersion, &wMinorVersion, &wBuildNumber, &wRevisionNumber)) {
                if (SUCCEEDED(StringCchPrintf(
                        szVersion, CCH_VERSION, L"Version %d.%d.%d.%d", (int)wMajorVersion, (int)wMinorVersion,
                        (int)wBuildNumber, (int)wRevisionNumber))) {
                    SetDlgItemText(hDlg, IDD_VERTEXT, szVersion);
                }
            }
            return TRUE;
        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam)) {
                case IDOK:
                case IDCANCEL:
                    EndDialog(hDlg, IDOK);
                    return TRUE;
            }
    }
    return FALSE;
}

DWORD
WINAPI
FormatDrive(IN PVOID ThreadParameter) {
    // Not supported and should never be called.
    exit(-1);
    return 0;
}

DWORD
WINAPI
CopyDiskette(IN PVOID ThreadParameter) {
    // Not supported and should never be called.
    exit(-1);
    return 0;
}

/////////////////////////////////////////////////////////////////////
//
// Name:     ProgressDialogProc
//
// Synopsis: Modal dialog box for mouse move/copy progress
//
//
//
//
//
//
// Return:
//
//
// Assumes:
//
// Effects:
//
//
// Notes:
//
/////////////////////////////////////////////////////////////////////

INT_PTR
CALLBACK
ProgressDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    static PCOPYINFO pCopyInfo;
    WCHAR szTitle[MAXTITLELEN];

    switch (wMsg) {
        case WM_INITDIALOG:

            hdlgProgress = hDlg;
            pCopyInfo = (PCOPYINFO)lParam;

            // Set the destination directory in the dialog.
            // use IDD_TONAME 'cause IDD_TO gets disabled....

            // The dialog title defaults to "Moving..."
            if (pCopyInfo->dwFunc == FUNC_COPY) {
                LoadString(hAppInstance, IDS_COPYINGTITLE, szTitle, COUNTOF(szTitle));

                SetWindowText(hdlgProgress, szTitle);

            } else {
                SetDlgItemText(hdlgProgress, IDD_TOSTATUS, kEmptyString);
            }

            //
            // Move/Copy things.
            //

            if (WFMoveCopyDriver(pCopyInfo)) {
                //
                // Error message!!
                //

                EndDialog(hDlg, GetLastError());
            }
            break;

        case FS_COPYDONE:

            //
            // Only cancel out if pCopyInfo == lParam
            // This indicates that the proper thread quit.
            //
            // wParam holds return value
            //

            if (lParam == (LPARAM)pCopyInfo) {
                EndDialog(hDlg, wParam);
            }
            break;

        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam)) {
                case IDCANCEL:

                    pCopyInfo->bUserAbort = TRUE;

                    //
                    // What should be the return value??
                    //
                    EndDialog(hDlg, 0);

                    break;

                default:
                    return FALSE;
            }
            break;

        default:
            return FALSE;
    }
    return TRUE;
}

// update all the windows and things after drives have been connected
// or disconnected.

void UpdateConnections(BOOL bUpdateDriveList) {
    HWND hwnd, hwndNext, hwndDrive, hwndTree;
    int i;
    DRIVE drive;
    HCURSOR hCursor;
    LPWSTR lpszVol;
    LPWSTR lpszOldVol;

    hCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    ShowCursor(TRUE);

    if (bUpdateDriveList) {
        UpdateDriveList();
    }

    // close all windows that have the current drive set to
    // the one we just disconnected

    for (hwnd = GetWindow(hwndMDIClient, GW_CHILD); hwnd; hwnd = hwndNext) {
        hwndNext = GetWindow(hwnd, GW_HWNDNEXT);

        // ignore the titles and search window
        if (GetWindow(hwnd, GW_OWNER) || hwnd == hwndSearch)
            continue;

        drive = (DRIVE)GetWindowLongPtr(hwnd, GWL_TYPE);

        //
        // IsValidDisk uses GetDriveType which was updated if
        // bUpdateDriveList == TRUE.
        //

        if (IsValidDisk(drive)) {
            //
            // Invalidate cache to get real one in case the user reconnected
            // d: from \\popcorn\public to \\rastaman\ntwin
            //
            // Previously used MDI window title to determine if the volume
            // has changed.  Now we will just check DriveInfo structure
            // (bypass status bits).
            //

            //
            // Now only do this for remote drives!
            //

            if (IsRemoteDrive(drive)) {
                R_NetCon(drive);

                if (!WFGetConnection(drive, &lpszVol, FALSE, ALTNAME_REG)) {
                    lpszOldVol = (LPWSTR)GetWindowLongPtr(hwnd, GWL_VOLNAME);

                    if (lpszOldVol && lpszVol) {
                        if (lstrcmpi(lpszVol, lpszOldVol)) {
                            //
                            // updatedrivelist/initdrivebitmaps called above;
                            // don't do here
                            //
                            RefreshWindow(hwnd, FALSE, TRUE);
                        }
                    }
                }
            }

        } else {
            //
            // this drive has gone away
            //
            if (IsLastWindow()) {
                // disconnecting the last drive
                // set this guy to the first non floppy / cd rom

                for (i = 0; i < cDrives; i++) {
                    if (!IsRemovableDrive(rgiDrive[i]) && !IsCDRomDrive(rgiDrive[i])) {
                        SendMessage(hwndDriveBar, FS_SETDRIVE, i, 0L);
                        break;
                    }
                }
            } else if ((hwndTree = HasTreeWindow(hwnd)) && GetWindowLongPtr(hwndTree, GWL_READLEVEL)) {
                //
                // abort tree walk
                //
                bCancelTree = TRUE;

            } else {
                SendMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0L);
            }
        }
    }

    // why is this here?  Move it further, right redisplay if at all.
    // Reuse hwndDrive as the current window open!

    hwndDrive = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L);

    i = (int)GetWindowLongPtr(hwndDrive, GWL_TYPE);

    if (TYPE_SEARCH == i) {
        i = DRIVEID(SearchInfo.szSearch);
    }

    SwitchDriveSelection(hwndDrive);

    MDIClientSizeChange(NULL, DRIVEBAR_FLAG); /* update/resize drive bar */

    ShowCursor(FALSE);
    SetCursor(hCursor);
}

//
// GetProductVersion
// Gets the product version values for the current module
//
// Parameters:
//   pwMajor    - [OUT] A pointer to the major version number
//   pwMinor    - [OUT] A pointer to the minor version number
//   pwBuild    - [OUT] A pointer to the build number
//   pwRevision - [OUT] A pointer to the revision number
//
// Returns TRUE if successful
//
BOOL GetProductVersion(WORD* pwMajor, WORD* pwMinor, WORD* pwBuild, WORD* pwRevision) {
    BOOL success = FALSE;
    WCHAR szCurrentModulePath[MAXPATHLEN];
    DWORD cchPath;
    DWORD cbVerInfo;
    LPVOID pFileVerInfo;
    UINT uLen;
    VS_FIXEDFILEINFO* pFixedFileInfo;

    cchPath = GetModuleFileName(NULL, szCurrentModulePath, MAXPATHLEN);

    if (cchPath && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        cbVerInfo = GetFileVersionInfoSize(szCurrentModulePath, NULL);

        if (cbVerInfo) {
            pFileVerInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cbVerInfo);

            if (pFileVerInfo) {
                if (GetFileVersionInfo(szCurrentModulePath, 0, cbVerInfo, pFileVerInfo)) {
                    // Get the pointer to the VS_FIXEDFILEINFO structure
                    if (VerQueryValue(pFileVerInfo, L"\\", (LPVOID*)&pFixedFileInfo, &uLen)) {
                        if (pFixedFileInfo && uLen) {
                            *pwMajor = HIWORD(pFixedFileInfo->dwProductVersionMS);
                            *pwMinor = LOWORD(pFixedFileInfo->dwProductVersionMS);
                            *pwBuild = HIWORD(pFixedFileInfo->dwProductVersionLS);
                            *pwRevision = LOWORD(pFixedFileInfo->dwProductVersionLS);

                            success = TRUE;
                        }
                    }
                }

                HeapFree(GetProcessHeap(), 0, pFileVerInfo);
            }
        }
    }

    return success;
}
