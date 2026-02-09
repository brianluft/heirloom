/********************************************************************

   wfutil.c

   Windows File System String Utility Functions

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#include "winfile.h"
#include "lfn.h"
#include "wfdos.h"
#include "wfutil.h"
#include "wfcomman.h"
#include "wftree.h"
#include "wfdrives.h"
#include "stringconstants.h"
#include <commctrl.h>
#include <stdlib.h>

LPWSTR CurDirCache[26];

#define MAXHISTORY 32
DWORD historyCur = 0;
typedef struct HistoryDir {
    HWND hwnd;
    WCHAR szDir[MAXPATHLEN];
} HistoryDir;
HistoryDir rghistoryDir[MAXHISTORY];

void SaveHistoryDir(HWND hwnd, LPWSTR szDir) {
    if (rghistoryDir[historyCur].hwnd == hwnd && lstrcmpi(rghistoryDir[historyCur].szDir, szDir) == 0)
        return;

    historyCur = (historyCur + 1) % MAXHISTORY;

    rghistoryDir[historyCur].hwnd = hwnd;
    lstrcpy(rghistoryDir[historyCur].szDir, szDir);

    // always leave one NULL entry after current
    DWORD historyT = (historyCur + 1) % MAXHISTORY;
    rghistoryDir[historyT].hwnd = NULL;
    rghistoryDir[historyT].szDir[0] = '\0';
}

BOOL GetPrevHistoryDir(BOOL forward, HWND* phwnd, LPWSTR szDir) {
    DWORD historyNext = (historyCur + 1) % MAXHISTORY;
    DWORD historyPrev = (historyCur == 0 ? MAXHISTORY : historyCur) - 1;
    DWORD historyT = forward ? historyNext : historyPrev;

    if (rghistoryDir[historyT].hwnd == NULL)
        return FALSE;

    historyCur = historyT;

    *phwnd = rghistoryDir[historyCur].hwnd;
    lstrcpy(szDir, rghistoryDir[historyCur].szDir);
    return TRUE;
}

LPWSTR
pszNextComponent(LPWSTR p) {
    BOOL bInQuotes = FALSE;

    while (*p == L' ' || *p == L'\t')
        p++;

    //
    // Skip executable name
    //
    while (*p) {
        if ((*p == L' ' || *p == L'\t') && !bInQuotes)
            break;

        if (*p == CHAR_DQUOTE)
            bInQuotes = !bInQuotes;

        p++;
    }

    if (*p) {
        *p++ = CHAR_NULL;

        while (*p == L' ' || *p == L'\t')
            p++;
    }

    return p;
}

// If a string starts and ends with a quote, truncate the ending quote and
// return a pointer to the new string start.  Note that parsing such as
// pszNextComponent above support quotes in the middle of strings, which
// this function makes no attempt to remove.
LPWSTR
pszRemoveSurroundingQuotes(LPWSTR p) {
    if (*p == CHAR_DQUOTE) {
        size_t len;

        len = wcslen(p);

        // Length needs to be at least 2 to ensure there are 2 quotes rather
        // than counting the same quote twice
        if (len > 1 && p[len - 1] == CHAR_DQUOTE) {
            p[len - 1] = '\0';
            p++;
        }
    }

    return p;
}

// Set the status bar text for a given pane

void CDECL SetStatusText(int nPane, UINT nFlags, LPCWSTR szFormat, ...) {
    WCHAR szTemp[120 + MAXPATHLEN];
    WCHAR szTempFormat[120 + MAXPATHLEN];

    va_list vaArgs;

    if (!hwndStatus)
        return;

    if (nFlags & SST_RESOURCE) {
        if (!LoadString(hAppInstance, (DWORD)(LONG_PTR)szFormat, szTempFormat, COUNTOF(szTempFormat)))

            return;

        szFormat = szTempFormat;
    }

    if (nFlags & SST_FORMAT) {
        va_start(vaArgs, szFormat);
        wvsprintf(szTemp, szFormat, vaArgs);
        va_end(vaArgs);

        szFormat = szTemp;
    }

    SendMessage(hwndStatus, SB_SETTEXT, nPane, (LPARAM)szFormat);
}

// drive   zero based drive number (0 = A, 1 = B)
// returns:
//  TRUE    we have it saved pszPath gets path
//  FALSE   we don't have it saved

BOOL GetSavedDirectory(DRIVE drive, LPWSTR pszPath) {
    if (CurDirCache[drive]) {
        lstrcpy(pszPath, CurDirCache[drive]);
        return TRUE;
    } else
        return FALSE;
}

void SaveDirectory(LPWSTR pszPath) {
    DRIVE drive;

    drive = DRIVEID(pszPath);

    if (CurDirCache[drive])
        LocalFree((HANDLE)CurDirCache[drive]);

    CurDirCache[drive] = (LPWSTR)LocalAlloc(LPTR, ByteCountOf(lstrlen(pszPath) + 1));

    if (CurDirCache[drive])
        lstrcpy(CurDirCache[drive], pszPath);
}

/*
 *  GetSelectedDrive() -
 *
 *  Get the selected drive from the currently active window
 *
 *  should be in wfutil.c
 *
 *  Precond: Active MDI window MUST be a drive window
 *           (search windows NOT allowed)
 */

int GetSelectedDrive() {
    HWND hwnd;

    hwnd = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L);
    return (int)SendMessage(hwnd, FS_GETDRIVE, 0, 0L) - (int)CHAR_A;
}

/*
 *  GetSelectedDirectory() -
 *
 *  Gets the directory selected for the drive. uses the windows
 *  z-order to give precedence to windows higher in the order.
 *
 *  works like GetCurrentDirectory() except it looks through
 *  the window list for directories first (and returns ANSI)
 *
 *  returns:
 *  lpDir   ANSI string of current dir
 *  NOTE: when drive != 0, string will be empty for an invalid drive.
 */

void GetSelectedDirectory(DRIVE drive, LPWSTR pszDir) {
    HWND hwnd;
    DRIVE driveT;

    if (drive) {
        for (hwnd = GetWindow(hwndMDIClient, GW_CHILD); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
            driveT = (DRIVE)SendMessage(hwnd, FS_GETDRIVE, 0, 0L);
            if (drive == (DRIVE)(driveT - CHAR_A + 1))
                goto hwndfound;
        }
        if (!GetSavedDirectory(drive - 1, pszDir)) {
            GetDriveDirectory(drive, pszDir);
        }
        return;
    } else
        hwnd = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L);

hwndfound:
    SendMessage(hwnd, FS_GETDIRECTORY, MAXPATHLEN, (LPARAM)pszDir);

    StripBackslash(pszDir);
}

BOOL GetDriveDirectory(int iDrive, LPWSTR pszDir) {
    WCHAR drvstr[4];
    DWORD ret;

    pszDir[0] = '\0';

    if (iDrive != 0) {
        drvstr[0] = ('A') - 1 + iDrive;
        drvstr[1] = (':');
        drvstr[2] = ('.');
        drvstr[3] = ('\0');
    } else {
        drvstr[0] = ('.');
        drvstr[1] = ('\0');
    }

    if (GetFileAttributes(drvstr) == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    //  if (!CheckDirExists(drvstr))
    //      return FALSE;

    ret = GetFullPathName(drvstr, MAXPATHLEN, pszDir, NULL);

    return ret != 0;
}

// similar to GetSelectedDirectory but for all already listed directories
// doesn't change the current directory of the drives, but returns a list of them
void GetAllDirectories(LPWSTR rgszDirs[]) {
    HWND mpdrivehwnd[MAX_DRIVES];
    HWND hwnd;
    DRIVE driveT;

    for (driveT = 0; driveT < MAX_DRIVES; driveT++) {
        rgszDirs[driveT] = NULL;
        mpdrivehwnd[driveT] = NULL;
    }

    for (hwnd = GetWindow(hwndMDIClient, GW_CHILD); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
        driveT = (DRIVE)SendMessage(hwnd, FS_GETDRIVE, 0, 0L) - CHAR_A;
        if (mpdrivehwnd[driveT] == NULL)
            mpdrivehwnd[driveT] = hwnd;
    }

    for (driveT = 0; driveT < MAX_DRIVES; driveT++) {
        WCHAR szDir[MAXPATHLEN];

        if (mpdrivehwnd[driveT] != NULL) {
            SendMessage(mpdrivehwnd[driveT], FS_GETDIRECTORY, MAXPATHLEN, (LPARAM)szDir);

            StripBackslash(szDir);
        } else if (!GetSavedDirectory(driveT, szDir))
            szDir[0] = '\0';

        if (szDir[0] != '\0') {
            rgszDirs[driveT] = (LPWSTR)LocalAlloc(LPTR, ByteCountOf(lstrlen(szDir) + 1));
            lstrcpy(rgszDirs[driveT], szDir);
        }
    }
}

/////////////////////////////////////////////////////////////////////
//
// Name:     RefreshWindow
//
// Synopsis: Refreshes any type of MDI Window
// Return:
// Assumes:
// Effects:
// Notes:
//
/////////////////////////////////////////////////////////////////////

void RefreshWindow(HWND hwndActive, BOOL bUpdateDriveList, BOOL bFlushCache) {
    HWND hwndTree, hwndDir;
    DRIVE drive;

    //
    // make it optional for speed.
    //
    if (bUpdateDriveList) {
        UpdateDriveList();
    }

    //
    // make sure the thing is still there (floppy drive, net drive)
    //
    drive = (DRIVE)GetWindowLongPtr(hwndActive, GWL_TYPE);

    if ((drive >= 0) && !CheckDrive(hwndActive, drive, FUNC_SETDRIVE)) {
        return;
    }

    //
    // If bFlushCache, remind ourselves to try it
    //
    if (bFlushCache) {
        aDriveInfo[drive].bShareChkTried = FALSE;
    }

    // NOTE: similar to CreateDirWindow

    //
    // update the dir part first so tree can steal later
    //
    if (hwndDir = HasDirWindow(hwndActive)) {
        SendMessage(hwndDir, FS_CHANGEDISPLAY, CD_PATH, 0L);
    }

    if (hwndTree = HasTreeWindow(hwndActive)) {
        //
        // update the drives windows
        //
        SendMessage(hwndActive, FS_CHANGEDRIVES, 0, 0L);

        //
        // update the tree
        //
        SendMessage(hwndTree, TC_SETDRIVE, MAKELONG(MAKEWORD(FALSE, TRUE), TRUE), 0L);
    }

    if (hwndActive == hwndSearch) {
        SendMessage(hwndActive, FS_CHANGEDISPLAY, CD_PATH, 0L);
    }
}

//
// Assumes there are 2 extra spaces in the array.
//

void CheckEsc(LPWSTR szFile) {
    // DrivesDropObject calls w/ 2*MAXPATHLEN

    WCHAR szT[2 * MAXPATHLEN];

    WCHAR *p, *pT;

    for (p = szFile; *p; p++) {
        switch (*p) {
            case CHAR_SPACE:
            case CHAR_COMMA:
            case CHAR_SEMICOLON:
            case CHAR_DQUOTE: {
                // this path contains an annoying character
                lstrcpy(szT, szFile);
                p = szFile;
                *p++ = CHAR_DQUOTE;
                for (pT = szT; *pT;) {
                    *p++ = *pT++;
                }
                *p++ = CHAR_DQUOTE;
                *p = CHAR_NULL;
                return;
            }
        }
    }
}

HWND GetRealParent(HWND hwnd) {
    // run up the parent chain until you find a hwnd
    // that doesn't have WS_CHILD set

    while (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CHILD)
        hwnd = (HWND)GetWindowLongPtr(hwnd, GWLP_HWNDPARENT);

    return hwnd;
}

// atoi with decimal comma separators
//

// #ifdef INLIBRARY
LPWSTR
AddCommasInternal(LPWSTR szBuf, DWORD dw) {
    WCHAR szTemp[40];
    LPWSTR pTemp;
    int count, len;
    LPWSTR p;
    int iCommaLen;
    int i;

    // if *szComma[0] == NULL, get out now

    if (!szComma[0]) {
        wsprintf(szBuf, L"%lu", dw);
        return szBuf;
    }

    len = wsprintf(szTemp, L"%lu", dw);
    iCommaLen = lstrlen(szComma);

    pTemp = szTemp + len - 1;

    // szComma size may be < 1 !

    p = szBuf + len + ((len - 1) / 3) * iCommaLen;

    *p-- = CHAR_NULL;  // null terminate the string we are building

    count = 1;
    while (pTemp >= szTemp) {
        *p-- = *pTemp--;
        if (count == 3) {
            count = 1;
            if (p > szBuf) {
                for (i = iCommaLen - 1; i >= 0; i--)
                    *p-- = szComma[i];
            }
        } else
            count++;
    }
    return szBuf;
}
// #endif

BOOL IsLastWindow() {
    HWND hwnd;
    int count;

    count = 0;

    // count all non title/search windows to see if close is allowed

    for (hwnd = GetWindow(hwndMDIClient, GW_CHILD); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT))
        if (!GetWindow(hwnd, GW_OWNER) && ((int)GetWindowLongPtr(hwnd, GWL_TYPE) >= 0))
            count++;

    return count == 1;
}

/////////////////////////////////////////////////////////////////////
//
// Name:     GetMDIWindowText
//
// Synopsis: Goofy way to store directory name
//           Returns number of MDI window & text w/ # stripped off
//
// IN   hwnd     MDIWindow to get from
// OUT  szTitle  target string of title
// IN   size     cch of szTitle
//
// Return:   0   = no number
//           >0  title number
//
// Assumes:
//
// Effects:
//
//
// Notes:
//
/////////////////////////////////////////////////////////////////////

int GetMDIWindowText(HWND hwnd, LPWSTR szTitle, int size) {
    //
    // Need temp buffer due to filter + path
    //
    WCHAR szTemp[2 * MAXPATHLEN + 40];

    LPWSTR lpLast;
    int iWindowNumber;

    EnterCriticalSection(&CriticalSectionPath);

    InternalGetWindowText(hwnd, szTemp, COUNTOF(szTemp));

    if (GetWindow(hwnd, GW_OWNER) || GetWindowLongPtr(hwnd, GWL_TYPE) == -1L)
        lpLast = NULL;
    else {
        lpLast = szTemp + GetWindowLongPtr(hwnd, GWL_PATHLEN);
        if (lpLast == szTemp || !*lpLast)
            lpLast = NULL;
    }

    LeaveCriticalSection(&CriticalSectionPath);

    //
    // save the window #
    //
    if (lpLast) {
        iWindowNumber = atoi(lpLast + 1);

        //
        // Delimit title (we just want part of the title)
        //
        *lpLast = CHAR_NULL;

    } else {
        iWindowNumber = 0;
    }

    // After SetMDIWindowText, the window title contains only the directory
    // path (e.g., "C:\temp").  Append "\*.*" to reconstruct the internal
    // path that callers expect.  GWL_PATHLEN is non-zero once
    // SetMDIWindowText has been called; before that the title still
    // contains the original path with filespec, so we must not append.
    if (GetWindowLongPtr(hwnd, GWL_TYPE) != -1L && GetWindowLongPtr(hwnd, GWL_PATHLEN) != 0) {
        AddBackslash(szTemp);
        lstrcat(szTemp, kStarDotStar);
    }

    //
    // Make sure the strcpy below doesn't blow up if COUNTOF( szTemp ) > size
    //
    if (COUNTOF(szTemp) > size)
        szTemp[size - 1] = CHAR_NULL;

    lstrcpy(szTitle, szTemp);

    return iWindowNumber;
}

/////////////////////////////////////////////////////////////////////
//
// Name:     SetMDIWindowText
//
// Synopsis: Sets the titlebar of a MDI window based on the path/filter
//
// IN        hwnd    --  wnd to modify
// INC       szTitle --  path/filter to add (modified but restored)
//
// Return:   void
//
//
// Assumes:
//
// Effects:  title bar of window
//
//
// Notes:    modifies szTitle but restores it.
//
// set the MDI window text and add a ":#" on the end if
// there is another window with the same title.  this is to
// avoid confusion when there are multiple MDI children
// with the same title.  be sure to use GetMDIWindowText to
// strip off the number stuff.
//
/////////////////////////////////////////////////////////////////////

void SetMDIWindowText(HWND hwnd, LPWSTR szTitle) {
    WCHAR szTemp[MAXPATHLEN * 2 + 10];
    WCHAR szNumber[20];
    WCHAR szDisplay[MAXPATHLEN * 2 + 10];
    HWND hwndT;
    int num, max_num, cur_num;
    LPWSTR lpszVolShare;
    LPWSTR lpszVolName;

    DRIVE drive;
    BOOL bNotSame;

    UINT uTitleLen;

    cur_num = GetMDIWindowText(hwnd, szTemp, COUNTOF(szTemp));

    bNotSame = lstrcmp(szTemp, szTitle);

    max_num = 0;

    for (hwndT = GetWindow(hwndMDIClient, GW_CHILD); hwndT; hwndT = GetWindow(hwndT, GW_HWNDNEXT)) {
        num = GetMDIWindowText(hwndT, szTemp, COUNTOF(szTemp));

        if (!lstrcmp(szTemp, szTitle)) {
            if (hwndT == hwnd)
                continue;

            if (!max_num && !num) {
                // Construct display title for the other window: strip filespec
                lstrcpy(szDisplay, szTemp);
                StripFilespec(szDisplay);
                StripBackslash(szDisplay);

                DWORD Length = lstrlen(szDisplay);

                lstrcat(szDisplay, SZ_COLONONE);

                SetWindowText(hwndT, szDisplay);
                max_num = 1;
                SetWindowLongPtr(hwndT, GWL_PATHLEN, Length);
            }
            max_num = max(max_num, num);
        }
    }

    drive = (DWORD)GetWindowLongPtr(hwnd, GWL_TYPE);

    uTitleLen = lstrlen(szTitle);

    if (max_num) {
        if (bNotSame || !cur_num) {
            max_num++;
        } else {
            max_num = cur_num;
        }

        wsprintf(szNumber, L":%d", max_num);
    }

    // Construct display title: strip filespec to show just the directory path
    lstrcpy(szDisplay, szTitle);
    StripFilespec(szDisplay);
    StripBackslash(szDisplay);

    UINT uDisplayLen = lstrlen(szDisplay);

    if (max_num) {
        lstrcat(szDisplay, szNumber);
    }

    if (drive != (DRIVE)-1) {
        // Track remote drive volume name for change detection (used by wfinfo.cpp)
        lpszVolName = (LPWSTR)GetWindowLongPtr(hwnd, GWL_VOLNAME);

        if (lpszVolName)
            LocalFree(lpszVolName);

        if (GetVolShare(drive, &lpszVolShare, ALTNAME_REG) || !IsRemoteDrive(drive)) {
            lpszVolName = NULL;
        } else {
            lpszVolName = (LPWSTR)LocalAlloc(LPTR, ByteCountOf(lstrlen(lpszVolShare) + 1));

            if (lpszVolName) {
                lstrcpy(lpszVolName, lpszVolShare);
            }
        }

        SetWindowLongPtr(hwnd, GWL_VOLNAME, (LONG_PTR)lpszVolName);
    }

    EnterCriticalSection(&CriticalSectionPath);

    SetWindowLongPtr(hwnd, GWL_PATHLEN, uDisplayLen);
    SetWindowText(hwnd, szDisplay);

    LeaveCriticalSection(&CriticalSectionPath);

    //
    // Now delimit szTitle to keep it the same
    //
    szTitle[uTitleLen] = CHAR_NULL;

    SaveHistoryDir(hwnd, szTitle);
}

#define ISDIGIT(c) ((c) >= TEXT('0') && (c) <= TEXT('9'))

int atoiW(LPCWSTR sz) {
    int n = 0;
    BOOL bNeg = FALSE;

    if (*sz == CHAR_DASH) {
        bNeg = TRUE;
        sz++;
    }

    while (ISDIGIT(*sz)) {
        n *= 10;
        n += *sz - TEXT('0');
        sz++;
    }
    return bNeg ? -n : n;
}

//
// IsCDROM()  - determine if a drive is a CDROM drive
//
//      drive      drive index (0=A, 1=B, ...)
//
// return TRUE/FALSE
//
BOOL IsCDRomDrive(DRIVE drive) {
    if (aDriveInfo[drive].uType == DRIVE_CDROM)
        return (TRUE);
    return (FALSE);
}

BOOL IsNTFSDrive(DRIVE drive) {
    U_VolInfo(drive);

    //
    // Return false if the return value was an error condition.
    //
    if (GETRETVAL(VolInfo, drive))
        return FALSE;

    //
    // See if it's an NTFS drive.
    //
    return (!lstrcmpi(aDriveInfo[drive].szFileSysName, SZ_NTFSNAME)) ? TRUE : FALSE;
}

//
// NOTE:  This function really says whether or not the drive is not a
//        FAT drive.  In other words:
//            If it IS a FAT drive, it returns FALSE.
//            If it is NOT a FAT drive, it returns TRUE.
//
BOOL IsCasePreservedDrive(DRIVE drive) {
    U_VolInfo(drive);

    //
    // Return false if the return value was an error condition.
    //
    if (GETRETVAL(VolInfo, drive))
        return FALSE;

    //
    // Can no longer check the FS_CASE_IS_PRESERVED bit to see if it's a
    // FAT drive (now that FAT is case preserving and stores Unicode
    // on disk.
    //
    // Instead, we will check the file system string to see if the drive
    // is FAT.
    //
    // OLD CODE:
    //   return (aDriveInfo[drive].dwFileSystemFlags & FS_CASE_IS_PRESERVED) ?
    //           TRUE : FALSE;
    //
    return (!lstrcmpi(aDriveInfo[drive].szFileSysName, SZ_FATNAME)) ? FALSE : TRUE;
}

BOOL IsRemovableDrive(DRIVE drive) {
    return aDriveInfo[drive].uType == DRIVE_REMOVABLE;
}

BOOL IsRemoteDrive(DRIVE drive) {
    return aDriveInfo[drive].uType == DRIVE_REMOTE;
}

BOOL IsRamDrive(DRIVE drive) {
    return aDriveInfo[drive].uType == DRIVE_RAMDISK;
}

BOOL IsValidDisk(DRIVE drive) {
    U_Type(drive);
    return ((aDriveInfo[drive].uType != DRIVE_UNKNOWN) && (aDriveInfo[drive].uType != DRIVE_NO_ROOT_DIR));
}

DWORD
GetVolShare(DRIVE drive, LPWSTR* ppszVolShare, DWORD dwType) {
    if (IsRemoteDrive(drive)) {
        return WFGetConnection(drive, ppszVolShare, FALSE, dwType);
    } else {
        return GetVolumeLabel(drive, ppszVolShare, TRUE);
    }
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  IsLFNSelected() -                                                       */
/*                                                                          */
/*--------------------------------------------------------------------------*/

BOOL IsLFNSelected() {
    HWND hwndActive;
    BOOL fDir;
    LPWSTR p;

    hwndActive = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L);

    p = (LPWSTR)SendMessage(hwndActive, FS_GETSELECTION, 2, (LPARAM)&fDir);
    if (p) {
        LocalFree((HANDLE)p);
    }

    return (fDir);
}

//--------------------------------------------------------------------------
//
//  GetSelection() -
//  caller must free LPWSTR returned.
//
//  LPWSTR must have 2 chars for checkesc safety at end
//
//--------------------------------------------------------------------------

LPWSTR
GetSelection(int iSelType, PBOOL pbDir) {
    HWND hwndActive;

    hwndActive = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L);

    return (LPWSTR)SendMessage(hwndActive, FS_GETSELECTION, (WPARAM)iSelType, (LPARAM)pbDir);
}

//
// in:
//      pFrom   pointer that is used as start of selection search.
//              on subsequent calls pass in the previous non NULL
//              return value
//
// out:
//      pTo     buffer that receives the next file in the list
//              for non NULL return
//
// returns:
//      NULL    if no more files in this list (szFile) is undefined
//      pointer to be passed to subsequent calls to this function
//      to enumerate thorough the file list
//

LPWSTR
GetNextFile(LPWSTR pFrom, LPWSTR pTo, int cchMax) {
    int i;

    // Originally, this code tested if the first char was a double quote,
    // then either (1) scanned for the next space or (2) scanned for the next
    // quote.  CMD, however, will let you put quotes anywhere.
    // Now, the bInQuotes boolean is used instead.

    BOOL bInQuotes = FALSE;

    if (!pFrom)
        return NULL;

    // Skip over leading spaces and commas.
    while (*pFrom && (*pFrom == CHAR_SPACE || *pFrom == CHAR_COMMA))
        pFrom++;

    if (!*pFrom)
        return (NULL);

    // Find the next delimiter (space, comma (valid in bInQuotes only))

    for (i = 0; *pFrom && ((*pFrom != CHAR_SPACE && *pFrom != CHAR_COMMA) || bInQuotes);) {
        // bQuotes flipped if encountered.  ugly.
        // Note: it is also TAKEN OUT!
        if (CHAR_DQUOTE == *pFrom) {
            bInQuotes = !bInQuotes;
            ++pFrom;

            if (!*pFrom)
                break;

            // Must continue, or else the space (pFrom was inc'd!) will
            // be stored in the pTo string, which is FATAL!
            continue;
        }

        if (i < cchMax - 1) {
            i++;
            *pTo++ = *pFrom++;

            // increment to kill off present file name
        } else
            pFrom++;
    }

    // Kill off trailing spaces
    while (CHAR_SPACE == *(--pTo))
        ;

    *(++pTo) = CHAR_NULL;

    return (pFrom);
}

// sets the DOS current directory based on the currently active window

void SetWindowDirectory() {
    // Actually, only needs to be MAX MDI Title = (MAXPATHLEN + few + MAX diskname)
    // like "d:\aaa .. aaa\filter.here - [albertt]"
    // which _could_ be > 2*MAXPATHLEN
    WCHAR szTemp[MAXPATHLEN * 2];

    GetSelectedDirectory(0, szTemp);
    SetCurrentDirectory(szTemp);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  SetDlgDirectory() -                                                     */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* Sets the IDD_DIR field of 'hDlg' to whatever the active window says is the
 * active directory.
 *
 * this does not really change the DOS current directory
 */

void SetDlgDirectory(HWND hDlg, LPWSTR pszPath) {
    HDC hDC;
    SIZE size;
    RECT rc;
    HWND hDlgItem;
    HANDLE hFont;
    HANDLE hFontBak;
    WCHAR szPath[MAXPATHLEN + 5];
    WCHAR szTemp[MAXPATHLEN + 20];
    WCHAR szMessage[MAXMESSAGELEN];

    hFontBak = NULL;

    if (pszPath)
        lstrcpy(szPath, pszPath);
    else
        GetSelectedDirectory(0, szPath);

    /* Make sure that the current directory fits inside the static text field. */
    hDlgItem = GetDlgItem(hDlg, IDD_DIR);
    GetClientRect(hDlgItem, &rc);

    if (LoadString(hAppInstance, IDS_CURDIRIS, szMessage, COUNTOF(szMessage))) {
        hDC = GetDC(hDlg);
        hFont = (HANDLE)SendMessage(hDlgItem, WM_GETFONT, 0, 0L);

        //
        // This is required because Japanese Windows uses System font
        // for dialog box
        //
        if (hFont) {
            hFontBak = SelectObject(hDC, hFont);
        }

        GetTextExtentPoint32(hDC, szMessage, lstrlen(szMessage), &size);
        CompactPath(hDC, szPath, (rc.right - rc.left - size.cx));

        if (hFont) {
            SelectObject(hDC, hFontBak);
        }

        ReleaseDC(hDlg, hDC);
        wsprintf(szTemp, szMessage, szPath);
        SetDlgItemText(hDlg, IDD_DIR, szTemp);
    }
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  WritePrivateProfileBool() -                                             */
/*                                                                          */
/*--------------------------------------------------------------------------*/

void WritePrivateProfileBool(LPCWSTR szKey, BOOL bParam) {
    WCHAR szBool[6];

    wsprintf(szBool, SZ_PERCENTD, bParam);
    WritePrivateProfileString(kSettings, szKey, szBool, szTheINIFile);
}

/////////////////////////////////////////////////////////////////////
//
// Name:     CleanupMessages
//
// Synopsis: Make sure all the WM_FSC messages have been processed.
//
//
// Return:   void
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

void CleanupMessages() {
    MSG msg;

    while (PeekMessage(&msg, NULL, 0, 0, TRUE)) {
        if (!IsDialogMessage(hdlgProgress, &msg))
            DispatchMessage(&msg);
    }
    return;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  IsWild() -                                                              */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* Returns TRUE iff the path contains * or ? */

BOOL IsWild(LPWSTR lpszPath) {
    while (*lpszPath) {
        if (*lpszPath == CHAR_QUESTION || *lpszPath == CHAR_STAR)
            return (TRUE);
        lpszPath++;
    }

    return (FALSE);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  CheckSlashes() -                                                        */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* Replaces frontslashes (evil) with backslashes (good). */

void CheckSlashes(LPWSTR lpT) {
    while (*lpT) {
        if (*lpT == CHAR_SLASH)
            *lpT = CHAR_BACKSLASH;
        lpT++;
    }
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  AddBackslash() -                                                        */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* Ensures that a path ends with a backslash. */

UINT AddBackslash(LPWSTR lpszPath) {
    UINT uLen = lstrlen(lpszPath);

    if (*(lpszPath + uLen - 1) != CHAR_BACKSLASH) {
        lpszPath[uLen++] = CHAR_BACKSLASH;
        lpszPath[uLen] = CHAR_NULL;
    }

    return uLen;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  StripBackslash() -                                                      */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* Removes a trailing backslash from a proper directory name UNLESS it is
 * the root directory.  Assumes a fully qualified directory path.
 */

void StripBackslash(LPWSTR lpszPath) {
    UINT len;

    len = (lstrlen(lpszPath) - 1);
    if ((len == 2) || (lpszPath[len] != CHAR_BACKSLASH))
        return;

    lpszPath[len] = CHAR_NULL;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  StripFilespec() -                                                       */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* Remove the filespec portion from a path (including the backslash). */

void StripFilespec(LPWSTR lpszPath) {
    LPWSTR p;

    p = lpszPath + lstrlen(lpszPath);
    while ((*p != CHAR_BACKSLASH) && (*p != CHAR_COLON) && (p != lpszPath))
        p--;

    if (*p == CHAR_COLON)
        p++;

    /* Don't strip backslash from root directory entry. */
    if (p != lpszPath) {
        if ((*p == CHAR_BACKSLASH) && (*(p - 1) == CHAR_COLON))
            p++;
    }

    *p = CHAR_NULL;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  StripPath() -                                                           */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* Extract only the filespec portion from a path. */

void StripPath(LPWSTR lpszPath) {
    LPWSTR p;

    p = lpszPath + lstrlen(lpszPath);
    while ((*p != CHAR_BACKSLASH) && (*p != CHAR_COLON) && (p != lpszPath))
        p--;

    if (p != lpszPath)
        p++;

    if (p != lpszPath)
        lstrcpy(lpszPath, p);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  GetExtension() -                                                        */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* Returns the extension part of a filename. */

LPWSTR
GetExtension(LPWSTR pszFile) {
    LPWSTR p, pSave = NULL;

    p = pszFile;
    while (*p) {
        if (*p == CHAR_DOT)
            pSave = p;
        p++;
    }

    if (!pSave)
        return (p);

    return pSave + 1;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  MyMessageBox() -                                                        */
/*                                                                          */
/*--------------------------------------------------------------------------*/

int MyMessageBox(HWND hwnd, DWORD idTitle, DWORD idMessage, DWORD wStyle) {
    WCHAR szTitle[MAXTITLELEN];
    WCHAR szMessage[MAXMESSAGELEN];
    WCHAR szTemp[MAXMESSAGELEN];

    HWND hwndT;

    LoadString(hAppInstance, idTitle, szTitle, COUNTOF(szTitle));

    if (idMessage < 32) {
        LoadString(hAppInstance, IDS_UNKNOWNMSG, szTemp, COUNTOF(szTemp));
        wsprintf(szMessage, szTemp, idMessage);
    } else {
        LoadString(hAppInstance, idMessage, szMessage, COUNTOF(szMessage));
    }

    if (hwnd)
        hwndT = GetLastActivePopup(hwnd);
    else
        hwndT = hwnd;

    return MessageBox(hwndT, szMessage, szTitle, wStyle | MB_TASKMODAL);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ExecProgram() -                                                         */
/*                                                                          */
/*--------------------------------------------------------------------------*/

// Returns 0 for success.  Otherwise returns a IDS_ string code.

// Note: many funcs call this with weird parms like:
//  lpPath and lpParms = "f:\\\"note pad\" \"foo\"."
//          = f:\"note pad" "foo".         (without escape sequences)
//
//  While this appears illegal, it works just fine since quotes only cause
//  the spaces to be "non-delimiters."  The quote itself isn't a delimiter
//  and seems therefore legal.
//
//  (wfsearch.c 885 does this: it appends a period to the lpParms if
//  there is no extension.  even if it's already quoted!)

DWORD
ExecProgram(LPCWSTR lpPath, LPCWSTR lpParms, LPCWSTR lpDir, BOOL bLoadIt, BOOL bRunAs) {
    DWORD_PTR ret;
    int iCurCount;
    int i;
    HCURSOR hCursor;
    LPCWSTR lpszTitle;

    ret = 0;

    hCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    iCurCount = ShowCursor(TRUE) - 1;

    //
    // Shell Execute takes ansi string.
    //

    // Set title to file spec only
    // Note: this is set to the app, so
    // drag, drop, execute title shows app, not file

    // Set up title
    for (lpszTitle = lpPath + lstrlen(lpPath);
         *lpszTitle != CHAR_BACKSLASH && *lpszTitle != CHAR_COLON && lpszTitle != lpPath; lpszTitle--)
        ;

    // If we encountered a \ or : then skip it

    if (lpszTitle != lpPath)
        lpszTitle++;

    SetErrorMode(0);
    ret = (DWORD_PTR)ShellExecute(
        hwndFrame, bRunAs ? L"runas" : NULL, lpPath, lpParms, lpDir, bLoadIt ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL);

    SetErrorMode(1);

    switch (ret) {
        case 0:
        case 8:
            ret = IDS_NOMEMORYMSG;
            break;

        case 2:
            ret = IDS_FILENOTFOUNDMSG;
            break;

        case 3:
            ret = IDS_BADPATHMSG;
            break;

        case 5:
            ret = IDS_NOACCESSFILE;
            break;

        case 11:
            ret = IDS_EXECERRTITLE;
            break;

        case SE_ERR_SHARE:
            ret = IDS_SHAREERROR;
            break;

        case SE_ERR_ASSOCINCOMPLETE:
            ret = IDS_NOASSOCMSG;
            break;

        case SE_ERR_DDETIMEOUT:
        case SE_ERR_DDEFAIL:
        case SE_ERR_DDEBUSY:
            ret = IDS_DDEFAIL;
            break;

        case SE_ERR_NOASSOC:
            ret = IDS_NOASSOCMSG;
            break;

        default:
            if (ret < 32)
                goto EPExit;

            if (bMinOnRun && !bLoadIt)
                ShowWindow(hwndFrame, SW_SHOWMINNOACTIVE);
            ret = 0;
    }

EPExit:
    i = ShowCursor(FALSE);

    SetCursor(hCursor);

    return (DWORD)ret;
}

/////////////////////////////////////////////////////////////////////
//
// Name:     IsProgramFile
//
// Synopsis: Returns TRUE if the Path points to a file which has one of
//           the extensions listed in the "Programs=" part of win.ini.
//
// INC       lpszPath -- Path to check
// INC       ppDocBucket -- Bucket to search in
//
// Return:   pBucketTRUE=found
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

PDOCBUCKET
IsBucketFile(LPWSTR lpszPath, PPDOCBUCKET ppBucket) {
    LPWSTR szExt;

    //
    // Get the file's extension.
    //
    szExt = GetExtension(lpszPath);

    if (!*szExt) {
        //
        // The specified path didn't have an extension.  It can't be a program.
        //
        return (FALSE);
    }

    return DocFind(ppBucket, szExt);
}

// string always upper case
// returns true if additional characters; false if first one
// repeating the first character leaves only one character
// ch == '\0' resets the tick and buffer
BOOL TypeAheadString(WCHAR ch, LPWSTR szT) {
    static DWORD tick64 = 0;

    // Ringbuffer for typed chracters
    static WCHAR rgchTA[MAXPATHLEN] = { '\0' };

    // When typing characters all characters so far are the same
    static BOOL sameChar = TRUE;
    DWORD tickT;
    size_t ich;

    if (ch == '\0') {
        tick64 = 0;
        rgchTA[0] = '\0';
        return FALSE;
    }

    tickT = GetTickCount();
#pragma warning(disable : 4302)  // CharUpper smuggles a character through a pointer
    ch = reinterpret_cast<WCHAR>(CharUpper((LPWSTR)ch));
#pragma warning(default : 4302)
    ich = wcslen(rgchTA);

    // if out of space or more than .5s since last char, start over
    if (tickT - tick64 > 500 || ich > MAXPATHLEN - 2) {
        ich = 0;
        sameChar = TRUE;
    }

    rgchTA[ich] = ch;
    rgchTA[ich + 1] = '\0';

    tick64 = tickT;

    if (rgchTA[0] == ch && TRUE == sameChar) {
        // Same consecutive character as the first was pressed so jump ahead by one
        szT[0] = ch;
        szT[1] = '\0';

        return FALSE;
    } else {
        // not the same character as the first
        sameChar = FALSE;
    }

    lstrcpy(szT, rgchTA);

    return ich != 0;
}

LPWSTR GetFullPathInSystemDirectory(LPCWSTR FileName) {
    UINT LengthRequired;
    UINT LengthReturned;
    UINT FileNameLength;
    LPWSTR FullPath;

    LengthRequired = GetSystemDirectory(NULL, 0);
    if (LengthRequired == 0) {
        return NULL;
    }

    FileNameLength = lstrlen(FileName);
    FullPath = (LPWSTR)LocalAlloc(LMEM_FIXED, (LengthRequired + 1 + FileNameLength + 1) * sizeof(WCHAR));
    if (FullPath == NULL) {
        return NULL;
    }

    LengthReturned = GetSystemDirectory(FullPath, LengthRequired);
    if (LengthReturned == 0 || LengthReturned > LengthRequired) {
        LocalFree(FullPath);
        return NULL;
    }

    FullPath[LengthReturned] = '\\';
    lstrcpy(&FullPath[LengthReturned + 1], FileName);
    return FullPath;
}

HMODULE LoadSystemLibrary(LPCWSTR FileName) {
    LPWSTR FullPath;
    HMODULE Module;

    FullPath = GetFullPathInSystemDirectory(FileName);
    if (FullPath == NULL) {
        return NULL;
    }

    Module = LoadLibrary(FullPath);
    LocalFree(FullPath);
    return Module;
}

void SetCurrentPathOfWindow(LPWSTR szPath) {
    WCHAR szFullPath[MAXPATHLEN];
    LPWSTR szFilePart;
    DWORD result;
    HWND hwndActive;
    HWND hwndNew;
    HWND hwndTree;

    result = GetFullPathName(szPath, COUNTOF(szFullPath), szFullPath, &szFilePart);
    if (result == 0 || result >= COUNTOF(szFullPath) || ISUNCPATH(szFullPath)) {
        return;
    }

    hwndActive = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L);
    hwndNew = CreateDirWindow(szFullPath, TRUE, hwndActive);
    hwndTree = HasTreeWindow(hwndNew);

    if (hwndTree) {
        SetFocus(hwndTree);
    }
}
