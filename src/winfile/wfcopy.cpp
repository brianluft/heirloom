/********************************************************************

   wfcopy.c

   Windows File System File Copying Routines

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#include "winfile.h"
#include "lfn.h"
#include "wfcopy.h"
#include "wfrecyclebin.h"
#include "wfchgnot.h"
#include "wfcomman.h"
#include "wfutil.h"
#include "wfdir.h"
#include "wfdirsrc.h"
#include "wftree.h"
#include "wfdrives.h"
#include "stringconstants.h"

BOOL* pbConfirmAll;
BOOL* pbConfirmReadOnlyAll;

int ManySource;

void wfYield();

int CopyMoveRetry(LPWSTR, int, PBOOL);
DWORD CopyError(LPWSTR, LPWSTR, DWORD, DWORD, int, BOOL, BOOL);
BOOL IsRootDirectory(LPWSTR pPath);

DWORD ConfirmDialog(
    HWND hDlg,
    DWORD dlg,
    LPWSTR pFileDest,
    PLFNDTA pDTADest,
    LPWSTR pFileSource,
    PLFNDTA pDTASource,
    BOOL bConfirmByDefault,
    BOOL* pbAll,
    BOOL bConfirmReadOnlyByDefault,
    BOOL* pbReadOnlyAll);

DWORD IsInvalidPath(LPWSTR pPath);
DWORD GetNextPair(
    PCOPYROOT pcr,
    LPWSTR pFrom,
    LPWSTR pToPath,
    LPWSTR pToSpec,
    DWORD dwFunc,
    PDWORD pdwError,
    BOOL bIsLFNDriveDest);
int CheckMultiple(LPWSTR pInput);
void DialogEnterFileStuff(HWND hwnd);
DWORD SafeFileRemove(LPWSTR szFileOEM);
BOOL IsWindowsFile(LPWSTR szFileOEM);

INT_PTR CALLBACK ReplaceDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam);

BOOL IsValidChar(TUCHAR ch, BOOL fPath, BOOL bLFN) {
    switch (ch) {
        case CHAR_SEMICOLON:  // terminator
        case CHAR_COMMA:      // terminator
            return bLFN;

        case CHAR_PIPE:     // pipe
        case CHAR_GREATER:  // redir
        case CHAR_LESS:     // redir
        case CHAR_DQUOTE:   // quote
            return FALSE;

        case CHAR_QUESTION:   // wc           we only do wilds here because they're
        case CHAR_STAR:       // wc           legal for qualifypath
        case CHAR_BACKSLASH:  // path separator
        case CHAR_COLON:      // drive colon
        case CHAR_SLASH:      // path sep
        case CHAR_SPACE:      // space: valid on NT FAT, but winball can't use.
            return fPath;
    }

    //
    // cannot be a control character or space
    //
    return ch > CHAR_SPACE;
}

//--------------------------------------------------------------------------
//
// StripColon() -
//
// removes trailing colon if not a drive letter.
// this is to support DOS character devices (CON:, COM1: LPT1:).  DOS
// can't deal with these things having a colon on the end (so we strip it).
//
//--------------------------------------------------------------------------

LPWSTR
StripColon(LPWSTR pPath) {
    int cb = lstrlen(pPath);

    if (cb > 2 && pPath[cb - 1] == CHAR_COLON)
        pPath[cb - 1] = CHAR_NULL;

    return pPath;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  FindFileName() -                                                        */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* Returns a pointer to the last component of a path string. */

LPWSTR
FindFileName(LPWSTR pPath) {
    LPWSTR pT;

    for (pT = pPath; *pPath; pPath++) {
        if ((pPath[0] == CHAR_BACKSLASH || pPath[0] == CHAR_COLON) && pPath[1])
            pT = pPath + 1;
    }

    return (pT);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  AppendToPath() -                                                        */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* Appends a filename to a path.  Checks the \ problem first
 *  (which is why one can't just use lstrcat())
 * Also don't append a \ to : so we can have drive-relative paths...
 * this last bit is no longer appropriate since we qualify first!
 *
 * is this relative junk needed anymore?  if not this can be
 * replaced with AddBackslash(); lstrcat()
 */

void AppendToPath(LPWSTR pPath, LPCWSTR pMore) {
    /* Don't append a \ to empty paths. */
    if (*pPath) {
        while (*pPath)
            pPath++;

        if (pPath[-1] != CHAR_BACKSLASH)
            *pPath++ = CHAR_BACKSLASH;
    }

    /* Skip any initial terminators on input. */
    while (*pMore == CHAR_BACKSLASH)
        pMore++;

    lstrcpy(pPath, pMore);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  RemoveLast() -                                                          */
/*                                                                          */
/*--------------------------------------------------------------------------*/

// Deletes the last component of a filename in a string.
//

// Warning: assumes BACKSLASH or : exists in string

UINT RemoveLast(LPWSTR pFile) {
    LPWSTR pT;
    UINT uChars = 0;

    for (pT = pFile; *pFile; pFile++) {
        if (*pFile == CHAR_BACKSLASH) {
            pT = pFile;
            uChars = 0;

        } else if (*pFile == CHAR_COLON) {
            if (pFile[1] == CHAR_BACKSLASH) {
                pFile++;
            }

            pT = pFile + 1;

            uChars = 0;
            continue;
        }
        uChars++;
    }

    *pT = CHAR_NULL;
    return uChars;
}

/////////////////////////////////////////////////////////////////////
//
// Name:     QualifyPath
//
// Synopsis: Qualify a DOS/LFN file name based on current window
//
// INOUT     lpszPath   --  Filename to qualify: turns into path
// INC       bClearDots --  When set, clears trailing dots/spaces
//
// Return:   BOOL   T/F  Valid/Invalid path
//
//
// Assumes:
//
// Effects:
//
//
// Notes:    Drive based on current active window
//
//
// BUGBUG !!!
//
// Almost all of this code can be replaced with one call to
// the Win32 GetFullPathName() api!
//
/////////////////////////////////////////////////////////////////////

BOOL QualifyPath(LPWSTR lpszPath) {
    int cb, nSpaceLeft, i, j;
    WCHAR szTemp[MAXPATHLEN];
    DRIVE drive = 0;
    LPWSTR pOrig, pT;
    BOOL flfn = FALSE;
    BOOL fQuote = FALSE;

    WCHAR szDrive[] = SZ_ACOLONSLASH;

    LPWSTR lpszDot;
    UINT uLen;

    //
    // Adapted from [#1743 28-Aug-93 v-katsuy]
    //
    // We must do a conversion to DBCS for FAT and HPFS since they
    // store characters as DBCS.  Since a character may be 2 bytes,
    // an 8.3 name that the user types in may actually take more space.
    //
    // Therefore, we must do nSpaceLeft calculations based on DBCS
    // sizing.  We can still do copying using UNICODE since the conversion
    // will be done later also.
    //
    // !! BUGBUG !!
    // 1. We really don't know when we're using HPFS, so this case isn't
    // handled.
    //
    // 2. We don't correctly handle the case "\joker\..\doc.foo" where
    // "joker" takes > lstrlen("joker").  This is because RemoveLast
    // calculates size using lstrlen, and doesn't consider DBCS.
    //
    // Allocate enough space for 8.3 conversion to DBCS here (each
    // part done individually, so 8 chars is enough).
    //
    LPSTR pOrigA = NULL;

    //
    // Save it away.
    //
    StrNCpy(szTemp, lpszPath, COUNTOF(szTemp));
    CheckSlashes(szTemp);
    StripColon(szTemp);

    nSpaceLeft = MAXPATHLEN - 1;

    //
    // Strip off Surrounding Quotes
    //
    for (pT = pOrig = szTemp; *pOrig; pOrig++) {
        if (*pOrig != CHAR_DQUOTE) {
            *pT++ = *pOrig;
        } else {
            fQuote = TRUE;
        }
    }

    pOrig = szTemp;

    if (ISUNCPATH(pOrig)) {
        //
        // Stop at"\\foo\bar"
        //
        for (i = 0, j = 2, pOrig += 2; *pOrig && i < 2; pOrig++, j++) {
            if (CHAR_BACKSLASH == *pOrig)
                i++;
        }

        //
        // "\\foo" is an invalid path, but "\\foo\bar" is ok
        //

        if (!i)
            return FALSE;

        flfn = IsLFNDrive(lpszPath);

        //
        // If i == 2, then we found the trailing \, axe it off
        //
        if (2 == i) {
            j--;
            lpszPath[j] = CHAR_NULL;
        }

        nSpaceLeft -= j;

        goto GetComps;
    }

    if (pOrig[0] && CHAR_COLON == pOrig[1]) {
        //
        // Check for valid pOrig[1]!
        // Else seg fault off of search window
        //

        if (!(pOrig[0] >= CHAR_A && pOrig[0] <= CHAR_Z) && !(pOrig[0] >= CHAR_a && pOrig[0] <= TEXT('z'))) {
            //
            // Invalid drive string; return FALSE!
            //

            return FALSE;
        }

        drive = DRIVEID(pOrig);

        //
        // Skip over the drive letter.
        //
        pOrig += 2;

    } else {
        drive = GetSelectedDrive();
    }

    DRIVESET(szDrive, drive);

    flfn = IsLFNDrive(szDrive);

    //
    // on FAT _AND_ lfn devices, replace any illegal chars with underscores
    //

    for (pT = pOrig; *pT; pT++) {
        if (!IsValidChar(*pT, TRUE, flfn))
            *pT = CHAR_UNDERSCORE;
    }

    if (fQuote) {
        lpszPath[0] = CHAR_DQUOTE;
        lpszPath++;
    }

    if (CHAR_BACKSLASH == pOrig[0]) {
        lpszPath[0] = (WCHAR)drive + (WCHAR)'A';
        lpszPath[1] = CHAR_COLON;
        lpszPath[2] = CHAR_BACKSLASH;
        lpszPath[3] = CHAR_NULL;
        nSpaceLeft -= 3;
        pOrig++;

    } else {
        //
        // Get current dir of drive in path.  Also returns drive.
        //
        GetSelectedDirectory(drive + 1, lpszPath);
        nSpaceLeft -= lstrlen(lpszPath);
    }

GetComps:

    while (*pOrig && nSpaceLeft > 0) {
        //
        // If the component is parent dir, go up one dir.
        // If its the current dir, skip it, else add it normally
        //
        if (CHAR_DOT == pOrig[0]) {
            if (CHAR_DOT == pOrig[1]) {
                if (CHAR_BACKSLASH == pOrig[2] || !pOrig[2]) {
                    nSpaceLeft += RemoveLast(lpszPath);
                } else {
                    goto AddComponent;
                }

            } else if (pOrig[1] && CHAR_BACKSLASH != pOrig[1])
                goto AddComponent;

            while (*pOrig && CHAR_BACKSLASH != *pOrig)
                pOrig++;

            if (*pOrig)
                pOrig++;

        } else {
        AddComponent:
            LPWSTR pT = NULL;
            uLen = AddBackslash(lpszPath);
            nSpaceLeft = MAXPATHLEN - 1 - uLen;

            pT = lpszPath + uLen;

            if (flfn) {
                lpszDot = NULL;

                //
                // copy the component
                //
                while (*pOrig && CHAR_BACKSLASH != *pOrig && nSpaceLeft > 0) {
                    //
                    // Keep track of last dot here.
                    //

                    if (CHAR_SPACE == *pOrig || CHAR_DOT == *pOrig) {
                        //
                        // Let this be a dot only if there isn't one
                        // already, and the previous character wasn't
                        // a "*"
                        //
                        if (!lpszDot && CHAR_STAR != pT[-1])
                            lpszDot = pT;
                    } else {
                        lpszDot = NULL;
                    }

                    *pT++ = *pOrig++;
                    nSpaceLeft--;
                }

                //
                // If the last character was a {dot|space}+, then axe them
                // off and restore nSpaceLeft.
                //
                if (lpszDot) {
                    nSpaceLeft += (int)(pT - lpszDot);
                    pT = lpszDot;
                }

            } else {
                //
                // copy the filename (up to 8 chars)
                //
                for (cb = 0; *pOrig && CHAR_BACKSLASH != *pOrig && CHAR_DOT != *pOrig && nSpaceLeft > 0; pOrig++) {
                    if (cb < 8) {
                        cb++;
                        *pT++ = *pOrig;
                        nSpaceLeft--;
                    }
                    pOrigA++;
                }

                //
                // if there's an extension, copy it, up to 3 chars
                //
                if (CHAR_DOT == *pOrig && nSpaceLeft > 0) {
                    *pT++ = CHAR_DOT;
                    nSpaceLeft--;
                    pOrig++;

                    for (cb = 0; *pOrig && CHAR_BACKSLASH != *pOrig && nSpaceLeft > 0; pOrig++) {
                        if (CHAR_DOT == *pOrig)
                            cb = 3;

                        if (cb < 3) {
                            cb++;
                            *pT++ = *pOrig;
                            nSpaceLeft--;
                        }
                        pOrigA++;
                    }

                    //
                    // Get rid of extra dots
                    //
                    if (CHAR_DOT == pT[-1] && CHAR_STAR != pT[-2]) {
                        nSpaceLeft++;
                        pT--;
                    }
                }
            }

            //
            // skip the backslash
            //
            if (*pOrig)
                pOrig++;

            //
            // null terminate for next pass...
            //
            *pT = CHAR_NULL;
        }
    }

    StripBackslash(lpszPath);

    if (fQuote) {
        lpszPath--;
        pOrig = lpszPath + lstrlen(lpszPath);
        *(pOrig++) = CHAR_DQUOTE;
        *pOrig = CHAR_NULL;
    }

    return TRUE;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  IsRootDirectory() -                                                     */
/*                                                                          */
/*--------------------------------------------------------------------------*/

BOOL IsRootDirectory(LPWSTR pPath) {
    if (!lstrcmpi(pPath + 1, L":\\"))
        return (TRUE);
    if (!lstrcmpi(pPath, SZ_BACKSLASH))
        return (TRUE);
    if (!lstrcmpi(pPath + 1, SZ_COLON))
        return (TRUE);

    // checking unc!

    if (*pPath == CHAR_BACKSLASH && *(pPath + 1) == CHAR_BACKSLASH) { /* some sort of UNC name */
        LPWSTR p;
        int cBackslashes = 0;

        for (p = pPath + 2; *p;) {
            if (*p == CHAR_BACKSLASH && (++cBackslashes > 1))
                return FALSE; /* not a bare UNC name, therefore not a root dir */

            p++;
        }
        return TRUE; /* end of string with only 1 more backslash */
                     /* must be a bare UNC, which looks like a root dir */
    }

    return (FALSE);
}

// returns:
//  TRUE    if pPath is a directory, including the root and
//      relative paths "." and ".."
//  FALSE   not a dir

BOOL IsDirectory(LPWSTR pPath) {
    LPWSTR pT;
    WCHAR szTemp[MAXPATHLEN];

    if (IsRootDirectory(pPath))
        return TRUE;

    // check for "." and ".."
    pT = FindFileName(pPath);

    if (ISDOTDIR(pT)) {
        return TRUE;
    }

    lstrcpy(szTemp, pPath);

    //
    // QualifyPath
    //
    QualifyPath(szTemp);

    return WFIsDir(szTemp);
}

//
// note: this has the side effect of setting the
// current drive to the new disk if it is successful
//
// Return values:
//  0 = fail (general)
//  1 = succeed

BOOL IsTheDiskReallyThere(HWND hwnd, LPWSTR pPath, DWORD dwFunc, BOOL bModal) {
    DRIVE drive;
    WCHAR szTemp[MAXMESSAGELEN];
    WCHAR szMessage[MAXMESSAGELEN];
    WCHAR szTitle[128];
    int err = 0;
    DWORD dwError;

    BOOL bTriedRoot = FALSE;
    WCHAR szDrive[] = SZ_ACOLONSLASH;
    HCURSOR hCursor;

    STKCHK();

    if (pPath[1] == CHAR_COLON)
        drive = DRIVEID(pPath);
    else
        return 1;

    // First chance error if drive is busy (format/diskcopy)

    if (aDriveInfo[drive].iBusy) {
        LoadString(hAppInstance, aDriveInfo[drive].iBusy, szTemp, COUNTOF(szTemp));
        LoadString(hAppInstance, IDS_COPYERROR + FUNC_SETDRIVE, szTitle, COUNTOF(szTitle));

        wsprintf(szMessage, szTemp, drive + CHAR_A);
        MessageBox(hwnd, szMessage, szTitle, MB_ICONHAND);

        return 0;
    }

Retry:

    // Put up the hourglass cursor since this
    // could take a long time

    hCursor = LoadCursor(NULL, IDC_WAIT);

    if (hCursor)
        hCursor = SetCursor(hCursor);
    ShowCursor(TRUE);

    err = !GetDriveDirectory(drive + 1, szTemp);

    if (hCursor)
        SetCursor(hCursor);
    ShowCursor(FALSE);

    if (err) {
        goto DiskNotThere;
    }

    return 1;

DiskNotThere:
    dwError = GetLastError();

    switch (dwError) {
        case ERROR_PATH_NOT_FOUND:
        case ERROR_FILE_NOT_FOUND:

            if (bTriedRoot)
                break;

            //
            // The directory may have been removed.
            // Go to the root directory and try once.
            //
            bTriedRoot = TRUE;
            DRIVESET(szDrive, drive);

            SetCurrentDirectory(szDrive);

            goto Retry;

        case ERROR_INVALID_PARAMETER:

            // Handle INVALID PARAMETER from GetDriveDirectory from GetFileAttributes
            dwError = ERROR_NOT_READY;
            break;

        case ERROR_ACCESS_DENIED:

            //
            // If failed due to security, return 2
            // (this is non-zero which means sorta error)
            //
            // old: return 2;
            break;

        case ERROR_NOT_READY:

            //
            // WAS ERROR_NO_MEDIA_IN_DRIVE, but changed back due to
            // wow incompatibilities.
            //
            // drive not ready (no disk in the drive)
            //
            LoadString(hAppInstance, IDS_COPYERROR + dwFunc, szTitle, COUNTOF(szTitle));
            LoadString(hAppInstance, IDS_DRIVENOTREADY, szTemp, COUNTOF(szTemp));
            wsprintf(szMessage, szTemp, drive + CHAR_A);
            if (MessageBox(hwnd, szMessage, szTitle, MB_ICONEXCLAMATION | MB_RETRYCANCEL) == IDRETRY)
                goto Retry;
            else
                return 0;
        case ERROR_BAD_NET_NAME:

            MyMessageBox(hwnd, IDS_BADNETNAMETITLE, IDS_BADNETNAME, MB_OK | MB_ICONEXCLAMATION);
            return 0;

        case ERROR_UNRECOGNIZED_VOLUME:  // 0x1F  huh?  What's this?

            // general failure (disk not formatted)

            LoadString(hAppInstance, IDS_COPYERROR + dwFunc, szTitle, COUNTOF(szTitle));

            FormatError(TRUE, szTemp, COUNTOF(szTemp), ERROR_UNRECOGNIZED_VOLUME);
            MessageBox(hwnd, szTemp, szTitle, MB_OK | MB_ICONSTOP);

            return 0;

        default:
            break;
    }

    LoadString(hAppInstance, IDS_COPYERROR + dwFunc, szTitle, COUNTOF(szTitle));

    FormatError(TRUE, szTemp, COUNTOF(szTemp), dwError);
    MessageBox(hwnd, szTemp, szTitle, MB_OK | MB_ICONSTOP);

    return 0;
}

void BuildDateLine(LPWSTR szTemp, PLFNDTA plfndta) {
    wsprintf(szTemp, szBytes, plfndta->fd.nFileSizeLow);
    lstrcat(szTemp, kSpace);
    PutDate(&plfndta->fd.ftLastWriteTime, szTemp + lstrlen(szTemp));
    lstrcat(szTemp, kSpace);
    PutTime(&plfndta->fd.ftLastWriteTime, szTemp + lstrlen(szTemp));
}

typedef struct {
    LPWSTR pFileDest;
    LPWSTR pFileSource;
    PLFNDTA plfndtaDest;
    PLFNDTA plfndtaSrc;
    int bWriteProtect;
    BOOL bNoAccess;
} PARAM_REPLACEDLG, FAR* LPPARAM_REPLACEDLG;

void SetDlgItemPath(HWND hDlg, int id, LPCWSTR pszPath) {
    RECT rc;
    HDC hdc;
    HFONT hFont;
    WCHAR szPath[MAXPATHLEN + 1];  // can have one extra WCHAR
    HWND hwnd;

    hwnd = GetDlgItem(hDlg, id);

    if (!hwnd)
        return;

    lstrcpy(szPath, pszPath);

    GetClientRect(hwnd, &rc);

    hdc = GetDC(hDlg);

    hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0L);
    if (hFont = (HFONT)SelectObject(hdc, hFont)) {
        CompactPath(hdc, szPath, rc.right);
        SelectObject(hdc, hFont);
    }

    ReleaseDC(hDlg, hdc);
    SetWindowText(hwnd, szPath);
}

INT_PTR
CALLBACK
ReplaceDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    WCHAR szMessage[MAXMESSAGELEN];

    switch (wMsg) {
        case WM_INITDIALOG: {
            LPPARAM_REPLACEDLG lpdlgparams = (LPPARAM_REPLACEDLG)lParam;

            if (lpdlgparams->bWriteProtect) {
                LoadString(hAppInstance, IDS_WRITEPROTECTFILE, szMessage, COUNTOF(szMessage));
                SetDlgItemText(hDlg, IDD_STATUS, szMessage);

                LoadString(hAppInstance, IDS_ALLFILES, szMessage, COUNTOF(szMessage));
                SetDlgItemText(hDlg, IDD_OTHER, szMessage);
            }

            EnableWindow(GetDlgItem(hDlg, IDD_YESALL), !lpdlgparams->bNoAccess && ManySource);

            EnableWindow(GetDlgItem(hDlg, IDCANCEL), !lpdlgparams->bNoAccess);

            lstrcpy(szMessage, lpdlgparams->pFileSource);
            lstrcat(szMessage, SZ_QUESTION);
            SetDlgItemPath(hDlg, IDD_FROM, szMessage);

            if (lpdlgparams->pFileDest) {
                BuildDateLine(szMessage, lpdlgparams->plfndtaSrc);
                SetDlgItemText(hDlg, IDD_DATE2, szMessage);

                SetDlgItemPath(hDlg, IDD_TO, lpdlgparams->pFileDest);
                BuildDateLine(szMessage, lpdlgparams->plfndtaDest);
                SetDlgItemText(hDlg, IDD_DATE1, szMessage);
            }

            SetWindowLongPtr(hDlg, GWLP_USERDATA, (LPARAM)lpdlgparams);
            break;
        }

        case WM_COMMAND: {
            WORD id;

            id = GET_WM_COMMAND_ID(wParam, lParam);
            switch (id) {
                case IDD_FLAGS:
                    break;

                case IDD_YESALL:
                    *pbConfirmAll = TRUE;

                    if (((LPPARAM_REPLACEDLG)GetWindowLongPtr(hDlg, GWLP_USERDATA))->bWriteProtect) {
                        *pbConfirmReadOnlyAll = TRUE;
                    }
                    id = IDYES;
                    // fall through
                case IDYES:
                    // fall through
                default:  // this is IDNO and IDCANCEL
                    EndDialog(hDlg, id);
                    return FALSE;
            }
        } break;

        default:
            return FALSE;
    }
    return TRUE;
}

DWORD
ConfirmDialog(
    HWND hDlg,
    DWORD dlg,
    LPWSTR pFileDest,
    PLFNDTA plfndtaDest,
    LPWSTR pFileSource,
    PLFNDTA plfndtaSrc,
    BOOL bConfirmByDefault,
    BOOL* pbAll,
    BOOL bConfirmReadOnlyByDefault,
    BOOL* pbReadOnlyAll) {
    int nRetVal;
    PARAM_REPLACEDLG params;
    WCHAR szMessage[MAXMESSAGELEN];

    params.pFileDest = pFileDest;
    params.pFileSource = pFileSource;
    params.plfndtaDest = plfndtaDest;
    params.plfndtaSrc = plfndtaSrc;
    params.bWriteProtect = FALSE;
    params.bNoAccess = FALSE;
    pbConfirmAll = pbAll;  // set global for dialog box

    pbConfirmReadOnlyAll = pbReadOnlyAll;

    if (plfndtaDest->fd.dwFileAttributes & (ATTR_READONLY | ATTR_SYSTEM | ATTR_HIDDEN)) {
        if ((!bConfirmReadOnlyByDefault && !bConfirmByDefault) || *pbConfirmReadOnlyAll) {
            nRetVal = IDYES;
        } else {
            params.bWriteProtect = TRUE;
            nRetVal = (int)DialogBoxParam(
                hAppInstance, (LPWSTR)MAKEINTRESOURCE(dlg), hDlg, ReplaceDlgProc, (LPARAM)(LPPARAM_REPLACEDLG)&params);
        }

        if (nRetVal == IDYES) {
            if (!(plfndtaDest->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                lstrcpy(szMessage, pFileDest ? pFileDest : pFileSource);

                WFSetAttr(szMessage, plfndtaDest->fd.dwFileAttributes & ~(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM));
            }
        }

    } else if (!bConfirmByDefault || *pbConfirmAll) {
        nRetVal = IDYES;
    } else {
        nRetVal = (int)DialogBoxParam(
            hAppInstance, (LPWSTR)MAKEINTRESOURCE(dlg), hDlg, ReplaceDlgProc, (LPARAM)(LPPARAM_REPLACEDLG)&params);
    }

    if (nRetVal == -1)
        nRetVal = DE_INSMEM;

    return (DWORD)nRetVal;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  IsInvalidPath() -                                                       */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* Checks to see if a file spec is an evil character device or if it is
 * too long...
 */

DWORD
IsInvalidPath(LPWSTR pPath) {
    WCHAR sz[9];
    int n = 0;

    if (lstrlen(pPath) >= MAXPATHLEN)
        return ERROR_FILENAME_EXCED_RANGE;

    pPath = FindFileName(pPath);

    while (*pPath && *pPath != CHAR_DOT && *pPath != CHAR_COLON && n < 8)
        sz[n++] = *pPath++;

    sz[n] = CHAR_NULL;

    if (!lstrcmpi(sz, L"CON"))
        return ERROR_INVALID_NAME;

    if (!lstrcmpi(sz, L"MS$MOUSE"))
        return ERROR_INVALID_NAME;

    if (!lstrcmpi(sz, L"EMMXXXX0"))
        return ERROR_INVALID_NAME;

    if (!lstrcmpi(sz, L"CLOCK$"))
        return ERROR_INVALID_NAME;

    return ERROR_SUCCESS;
}

PLFNDTA
CurPDTA(PCOPYROOT pcr) {
    if (pcr->cDepth) {
        return (pcr->rgDTA + pcr->cDepth - 1);
    } else {
        return pcr->rgDTA;
    }
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  GetNextCleanup() -                                                      */
/*                                                                          */
/*--------------------------------------------------------------------------*/

void GetNextCleanup(PCOPYROOT pcr) {
    while (pcr->cDepth) {
        WFFindClose(CurPDTA(pcr));
        pcr->cDepth--;
    }
}

/////////////////////////////////////////////////////////////////////
//
// Name:     GetNextPair
//
// Synopsis: Gets next pair of files to copy/rename/move/delete
//
// INOUTC  pcr       -- Pointer to structure of recursing dir tree
// OUTC    pFrom     -- Source file/dir to copy
// OUTC    pToPath   -- path to dest file or dir
//         pToSpec   -- Raw dest file or dir name
// INC     dwFunc    -- operation (Any combo):
//
//           FUNC_DELETE - Delete files in pFrom
//           FUNC_RENAME - Rename files (same directory)
//           FUNC_MOVE   - Move files in pFrom to pTo (different disk)
//           FUNC_COPY   - Copy files in pFrom to pTo
//
// OUTC pdwError         -- If error, this holds the err code else 0
// INC  bIsLFNDriveDest  -- Is the dest drive lfn?
//
// Return:   DWORD:
//
//           OPER_ERROR  - Error processing filenames
//           OPER_DOFILE - Go ahead and copy, rename, or delete file
//           OPER_MKDIR  - Make a directory specified in pTo
//           OPER_RMDIR  - Remove directory
//           0           - No more files left
//
//
// Assumes:
//
// Effects:
//
//
// Notes:    If pcr->bMoveFast is true, then pretend the entire directory
//           is empty since it was successfully moved.
//
// Modified by C. Stevens, August, 1991.  Added logic so that we would call
// IsTheDiskReallyThere only once per drive.  Also changed some of the code
// to minimize the number of calls which access the disk.
//
/////////////////////////////////////////////////////////////////////

DWORD
GetNextPair(
    PCOPYROOT pcr,
    LPWSTR pFrom,
    LPWSTR pToPath,
    LPWSTR pToSpec,
    DWORD dwFunc,
    PDWORD pdwError,
    BOOL bIsLFNDriveDest) {
    LPWSTR pT;     // Temporary pointer
    DWORD dwOp;    // Return value (operation to perform
    PLFNDTA pDTA;  // Pointer to file DTA data

    STKCHK();
    *pFrom = CHAR_NULL;
    *pdwError = 0;
    pDTA = NULL;

    //
    // Keep recursing directory structure until we get to the bottom
    //
    while (TRUE) {
        if (pcr->cDepth) {
            //
            // The directory we returned last call needs to be recursed.
            //
            pDTA = pcr->rgDTA + pcr->cDepth - 1;  // use this DTA below

            if (pcr->fRecurse && pcr->cDepth == 1 && !pcr->rgDTA[0].fd.cFileName[0])

                //
                // The last one was the recursion root.
                //
                goto BeginDirSearch;

            if (pcr->cDepth >= (MAXDIRDEPTH - 1)) {  // reached the limit?
                dwOp = OPER_ERROR;
                *pdwError = ERROR_FILENAME_EXCED_RANGE;
                goto ReturnPair;
            }

            if (pcr->fRecurse && (pDTA->fd.dwFileAttributes & ATTR_DIR) &&
                !(pDTA->fd.dwFileAttributes & ATTR_RETURNED)) {
                //
                // Was returned on last call, begin search.
                //
                pDTA->fd.dwFileAttributes |= ATTR_RETURNED;

                if (pcr->bFastMove)
                    goto FastMoveSkipDir;

                // Check if we should skip an entry because it was e.g. an reparse point
                if (pDTA->fd.dwFileAttributes & (ATTR_SYMBOLIC | ATTR_JUNCTION)) {
                    RemoveLast(pcr->szDest);
                    dwOp = OPER_RMDIR;
                    goto ReturnPair;
                }

                pcr->cDepth++;
                pDTA++;

            BeginDirSearch:

                //
                // Search for all subfiles in directory.
                //
                AppendToPath(pcr->sz, kStarDotStar);
                goto BeginSearch;
            }

        SkipThisFile:

            //
            // Search for the next matching file.
            //
            if (!WFFindNext(pDTA)) {
                WFFindClose(pDTA);

            LeaveDirectory:

                //
                // This spec has been exhausted...
                //
                pcr->cDepth--;

                //
                // Remove the child file spec.
                //
                RemoveLast(pcr->sz);

            FastMoveSkipDir:

                RemoveLast(pcr->szDest);

                if (pcr->fRecurse && !pcr->bFastMove) {
                    //
                    // Tell the move/copy driver it can now delete
                    // the source directory if necessary.
                    //
                    // (Don't do on a fast move since the directory was
                    // removed on the move.)

                    dwOp = OPER_RMDIR;
                    goto ReturnPair;
                }

                pcr->bFastMove = FALSE;

                //
                // Not recursing, get more stuff.
                //
                continue;
            }

        ProcessSearchResult:

            //
            // Got a file or dir in the DTA which matches the wild card
            // originally passed in...
            //
            if (pDTA->fd.dwFileAttributes & ATTR_DIR) {
                //
                // Ignore directories if we're not recursing.
                //
                if (!pcr->fRecurse)
                    goto SkipThisFile;

                //
                // Skip the current and parent directories.
                //
                if (ISDOTDIR(pDTA->fd.cFileName)) {
                    goto SkipThisFile;
                }

                // We need to create this directory, and then begin searching
                // for subfiles.

                dwOp = OPER_MKDIR;
                RemoveLast(pcr->sz);
                AppendToPath(pcr->sz, pDTA->fd.cFileName);
                AppendToPath(pcr->szDest, pDTA->fd.cFileName);
                goto ReturnPair;
            }

            if (pcr->fRecurse || !(pDTA->fd.dwFileAttributes & ATTR_DIR)) {
                /* Remove the original spec. */

                RemoveLast(pcr->sz);

                /* Replace it. */

                AppendToPath(pcr->sz, pDTA->fd.cFileName);

                /* Convert to ANSI. */

                pT = FindFileName(pcr->sz);

                // If its a dir, tell the driver to create it
                // otherwise, tell the driver to "operate" on the file.

                dwOp = (pDTA->fd.dwFileAttributes & ATTR_DIR) ? OPER_RMDIR : OPER_DOFILE;
                goto ReturnPair;
            }
            continue;
        } else {
            //
            // Read the next source spec out of the raw source string.
            //
            pcr->fRecurse = FALSE;
            pcr->pSource = GetNextFile(pcr->pSource, pcr->sz, COUNTOF(pcr->sz));

            pcr->szDest[0] = CHAR_NULL;

            if (!pcr->pSource)
                return (0);

            //
            // Fully qualify the path
            //
            QualifyPath(pcr->sz);

            // Ensure the source disk really exists before doing anything.
            // Only call IsTheDiskReallyThere once for each drive letter.
            // Set pcr->cIsDiskThereCheck[DRIVEID] after disk has been
            // checked.  Modified by C. Stevens, August 1991

            if (pcr->sz[1] == CHAR_COLON && !pcr->cIsDiskThereCheck[DRIVEID(pcr->sz)]) {
                if (!IsTheDiskReallyThere(hdlgProgress, pcr->sz, dwFunc, FALSE))
                    return (0);
                pcr->cIsDiskThereCheck[DRIVEID(pcr->sz)] = 1;
            }

            //
            // Classify the input string.
            //
            if (IsWild(pcr->sz)) {
                //
                // Wild card... operate on all matches but not recursively.
                //
                pcr->cDepth = 1;
                pDTA = pcr->rgDTA;
                pcr->pRoot = NULL;

            BeginSearch:
                //
                // Quit if pcr->sz gets too big.
                //
                if (lstrlen(pcr->sz) - lstrlen(FindFileName(pcr->sz)) >= MAXPATHLEN)
                    goto SearchStartFail;

                //
                // Search for the wildcard spec in pcr->sz.
                //
                if (!WFFindFirst(pDTA, pcr->sz, ATTR_ALL)) {
                SearchStartFail:

                    if (pcr->fRecurse) {
                        // We are inside a recursive directory delete, so
                        // instead of erroring out, go back a level

                        goto LeaveDirectory;
                    }
                    lstrcpy(pFrom, pcr->sz);

                    //
                    // Back up as if we completed a search.
                    //
                    RemoveLast(pcr->sz);
                    pcr->cDepth--;

                    //
                    // Find First returned an error.  Return FileNotFound.
                    //
                    dwOp = OPER_ERROR;
                    *pdwError = ERROR_FILE_NOT_FOUND;

                    goto ReturnPair;
                }
                goto ProcessSearchResult;
            } else {
                // This could be a file or a directory.  Fill in the DTA
                // structure for attrib check

                if (!IsRootDirectory(pcr->sz)) {
                    if (!WFFindFirst(pcr->rgDTA, pcr->sz, ATTR_ALL)) {
                        dwOp = OPER_ERROR;
                        *pdwError = GetLastError();

                        goto ReturnPair;
                    }
                    WFFindClose(pcr->rgDTA);

                    // Mega hack fix by adding else clause

                } else {
                    pcr->rgDTA->hFindFile = INVALID_HANDLE_VALUE;
                }

                //
                // Now determine if its a file or a directory
                //
                pDTA = pcr->rgDTA;
                if (IsRootDirectory(pcr->sz) || (pDTA->fd.dwFileAttributes & ATTR_DIR)) {
                    //
                    // Process directory
                    //
                    if (dwFunc == FUNC_RENAME) {
                        if (IsRootDirectory(pcr->sz)) {
                            dwOp = OPER_ERROR;
                            *pdwError = DE_ROOTDIR;
                        } else
                            dwOp = OPER_DOFILE;
                        goto ReturnPair;
                    }

                    // Return reparse point and delete it via OPER_RMDIR
                    if (dwFunc == FUNC_DELETE && pDTA->fd.dwFileAttributes & (ATTR_SYMBOLIC | ATTR_JUNCTION)) {
                        pcr->fRecurse = FALSE;
                        dwOp = OPER_RMDIR;
                        goto ReturnPair;
                    }

                    //
                    // Directory: operation is recursive, but not for junctions and symlinks
                    //
                    if (pDTA->fd.dwFileAttributes & (ATTR_SYMBOLIC | ATTR_JUNCTION))
                        pcr->fRecurse = FALSE;
                    else
                        pcr->fRecurse = TRUE;

                    pcr->cDepth = 1;
                    pDTA->fd.cFileName[0] = CHAR_NULL;
                    pcr->pRoot = FindFileName(pcr->sz);

                    lstrcpy(pcr->szDest, pcr->pRoot);

                    dwOp = OPER_MKDIR;
                    goto ReturnPair;
                } else {
                    //
                    // Process file
                    //
                    pcr->pRoot = NULL;
                    dwOp = OPER_DOFILE;
                    goto ReturnPair;
                }
            }
        }
    }

ReturnPair:

    // The source filespec has been derived into pcr->sz
    // that is copied to pFrom.  pcr->sz and pToSpec are merged into pTo.

    if (!*pFrom)
        lstrcpy(pFrom, pcr->sz);
    QualifyPath(pFrom);

    if (dwFunc != FUNC_DELETE) {
        if (dwFunc == FUNC_RENAME && !*pToPath) {
            lstrcpy(pToPath, pFrom);
            RemoveLast(pToPath);
            AppendToPath(pToPath, pToSpec);
        } else {
            AppendToPath(pToPath, pcr->szDest);
            if (dwOp == OPER_MKDIR)
                RemoveLast(pToPath);

            AppendToPath(pToPath, pToSpec);
        }

        if ((dwOp == OPER_MKDIR || dwOp == OPER_DOFILE) &&

            (!bIsLFNDriveDest) && IsLFN(FindFileName(pFrom)) && (IsWild(pToSpec) || IsLFN(pToSpec))) {
            //
            // Don't check if ntfs, just if has altname!
            //
            if (pDTA->fd.cAlternateFileName[0]) {
                RemoveLast(pToPath);
                AppendToPath(pToPath, pDTA->fd.cAlternateFileName);
                QualifyPath(pToPath);

                if (dwOp == OPER_MKDIR) {
                    RemoveLast(pcr->szDest);
                    AppendToPath(pcr->szDest, pDTA->fd.cAlternateFileName);
                }

            } else {
                goto MergeNames;
            }
        } else {
        MergeNames:
            if (IsWild(pToPath)) {
                LFNMergePath(pToPath, FindFileName(pFrom));
            }
        }
    }

    if (dwOp == OPER_MKDIR) {
        //
        // For a directory copy to the same name, append the "- Copy", "- Symlink", "-Hardlink" suffix
        // to both the target of this operation, and the target directory for
        // recursive operations.  Unlike the regular file case, this does not
        // consider file name extensions, since it is specific to directories.
        //

        if (!_wcsicmp(pFrom, pToPath)) {
            switch (dwFunc) {
                case FUNC_COPY:
                    lstrcat(pToPath, L" - Copy");
                    lstrcat(pcr->szDest, L" - Copy");
                    break;

                case FUNC_LINK:
                    lstrcat(pToPath, L" - Symlink");
                    lstrcat(pcr->szDest, L" - Symlink");
                    break;

                case FUNC_HARD:
                    lstrcat(pToPath, L" - Junction");
                    lstrcat(pcr->szDest, L" - Junction");
                    break;
            }
        } else {
            //
            // Make sure the new directory is not a subdir of the original...
            // Assumes case insensitivity.
            //
            pT = pToPath;

            while (*pFrom && CharUpper((LPWSTR)(TUCHAR)*pFrom) == CharUpper((LPWSTR)(TUCHAR)*pT)) {
                pFrom++;
                pT++;
            }
            if (!*pFrom && (!*pT || *pT == CHAR_BACKSLASH)) {
                // The two fully qualified strings are equal up to the end of the
                //   source directory ==> the destination is a subdir.Must return
                //   an error.

                dwOp = OPER_ERROR;
                *pdwError = DE_DESTSUBTREE;
            }
        }
    }

    return dwOp;
}

void CdDotDot(LPWSTR szOrig) {
    WCHAR szTemp[MAXPATHLEN];

    lstrcpy(szTemp, szOrig);
    StripFilespec(szTemp);
    SetCurrentDirectory(szTemp);
}

/* p is a fully qualified ANSI string. */
BOOL IsCurrentDirectory(LPWSTR p) {
    WCHAR szTemp[MAXPATHLEN];

    GetDriveDirectory(DRIVEID(p) + 1, szTemp);

    return (lstrcmpi(szTemp, p) == 0);
}

//
// test input for "multiple" filespec
//
// examples:
//  0   foo.bar         (single non directory file)
//  1   *.exe           (wild card)
//  1   foo.bar bletch.txt  (multiple files)
//  2   c:\         (directory)
//
// note: this may hit the disk in the directory check
//

int CheckMultiple(LPWSTR pInput) {
    LPWSTR pT;
    WCHAR szTemp[MAXPATHLEN];

    /* Wildcards imply multiple files. */
    if (IsWild(pInput))
        return 1;  // wild card

    /* More than one thing implies multiple files. */
    pT = GetNextFile(pInput, szTemp, COUNTOF(szTemp));
    if (!pT)
        return 0;  // blank string

    StripBackslash(szTemp);

    if (IsDirectory(szTemp))
        return 2;  // directory

    pT = GetNextFile(pT, szTemp, COUNTOF(szTemp));

    return pT ? 1 : 0;  // several files, or just one
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  DialogEnterFileStuff() -                                                */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* Prevents the user from diddling anything other than the cancel button. */

void DialogEnterFileStuff(HWND hwnd) {
    HWND hwndT;

    //
    // set the focus to the cancel button so the user can hit space or esc
    //
    if (hwndT = GetDlgItem(hwnd, IDCANCEL)) {
        SetFocus(hwndT);
        SendMessage(hwnd, DM_SETDEFID, IDCANCEL, 0L);
    }

    //
    // disable the ok button and the edit controls
    //
    if (hwndT = GetDlgItem(hwnd, IDOK))
        EnableWindow(hwndT, FALSE);

    if (hwndT = GetDlgItem(hwnd, IDD_TO))
        EnableWindow(hwndT, FALSE);

    if (hwndT = GetDlgItem(hwnd, IDD_FROM))
        EnableWindow(hwndT, FALSE);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  Notify() -                                                              */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* Sets the status dialog item in the modeless status dialog box. */

// used for both the drag drop status dialogs and the manual user
// entry dialogs so be careful what you change

void Notify(HWND hDlg, WORD idMessage, LPCWSTR szFrom, LPCWSTR szTo) {
    WCHAR szTemp[40];

    if (idMessage) {
        LoadString(hAppInstance, idMessage, szTemp, COUNTOF(szTemp));
        SetDlgItemText(hDlg, IDD_STATUS, szTemp);
        SetDlgItemPath(hDlg, IDD_NAME, szFrom);
    } else {
        SetDlgItemText(hDlg, IDD_STATUS, kEmptyString);
        SetDlgItemText(hDlg, IDD_NAME, kEmptyString);
    }

    // is this the drag/drop status dialog or the move/copy dialog

    SetDlgItemPath(hDlg, IDD_TONAME, szTo);
}

//
// BOOL IsWindowsFile(LPWSTR szFileOEM)
//
// this is a bit strange.  kernel strips off the path info so he
// will match only on the base name of the file.  so if the base
// name matches a currently open windows file we get the full
// path string and compare against that.  that will tell
// us that we have a file that kernel has open.
//
// LFN: detect long names and ignore them?

BOOL IsWindowsFile(LPWSTR szFileOEM) {
    HMODULE hMod;
    WCHAR szModule[MAXPATHLEN];

    //
    // kernel can't load an lfn...
    //
    if (GetNameType(szFileOEM) == FILE_LONG)
        return FALSE;

    // kernel won't accept long paths

    lstrcpy(szModule, szFileOEM);
    StripPath(szModule);

    hMod = GetModuleHandle(szModule);

    // check for one cause that's what's returned if its MSDOS
    // but it isn't really loaded because of xl 2.1c kernel hack
    if (!hMod || hMod == (HANDLE)1)
        return FALSE;

    GetModuleFileName(hMod, szModule, COUNTOF(szModule));

    if (!lstrcmpi(szFileOEM, szModule))  // they are both OEM & we
        return TRUE;                     // just care about equality
    else
        return FALSE;
}

DWORD
SafeFileRemove(LPWSTR szFileOEM) {
    if (IsWindowsFile(szFileOEM))
        return DE_WINDOWSFILE;
    else
        return WFRemove(szFileOEM);
}

DWORD
WF_CreateDirectory(HWND hwndParent, LPWSTR szDest, LPWSTR szSrc) {
    DWORD ret = 0;
    WCHAR szTemp[MAXPATHLEN + 1];  // +1 for AddBackslash()
    LPWSTR p, pLastSpecEnd;

    LFNDTA DTAHack;
    BOOL bLastExists;

    //
    // now create the full dir tree on the destination
    //
    StrNCpy(szTemp, szDest, COUNTOF(szTemp) - 1);
    pLastSpecEnd = szTemp + AddBackslash(szTemp) - 1;

    p = SkipPathHead(szTemp);

    if (!p)
        return ERROR_INVALID_NAME;

    //
    // create each part of the dir in order
    //
    while (*p) {
        //
        // Keep track if the last component exists already
        // so that when we reach the very last one, we know if we need
        // to return the error "Entire dir path exists."
        //
        bLastExists = FALSE;

        while (*p && *p != CHAR_BACKSLASH)
            p++;

        if (*p) {
            *p = CHAR_NULL;

            if (WFFindFirst(&DTAHack, szTemp, ATTR_ALL)) {
                WFFindClose(&DTAHack);

                if (!(DTAHack.fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    return DE_DIREXISTSASFILE;

                bLastExists = TRUE;

            } else {
                //
                // When we reach the last spec, create the directory
                // using the template.
                //
                if (ret = MKDir(szTemp, (p == pLastSpecEnd) ? szSrc : NULL)) {
                    //
                    // Here we must also ignore the ERROR_ALREADY_EXISTS error.
                    // Even though we checked for existence above, on NTFS, we
                    // may only have WX privilege so it was not found by
                    // FindFirstFile.
                    //
                    if (ERROR_ALREADY_EXISTS == ret)
                        ret = ERROR_SUCCESS;
                    else
                        return ret;

                } else {
                    wfYield();
                }
            }

            *p++ = CHAR_BACKSLASH;
        }
    }
    if (bLastExists)
        ret = ERROR_ALREADY_EXISTS;

    return ret;  // return the last error code
}

/////////////////////////////////////////////////////////////////////
//
// Name:     WFMoveCopyDriver
//
// Synopsis: Sets up thread then returns.
//
// INOUT pCopyInfo  Copy information: MUST BE ALL MALLOC'D!
//
//
//
//
// Return:   DWORD 0=success else error code
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

DWORD
WFMoveCopyDriver(PCOPYINFO pCopyInfo) {
    HANDLE hThreadCopy;
    DWORD dwIgnore;

    //
    // Move/Copy things.
    //
    hThreadCopy = CreateThread(NULL, 0L, WFMoveCopyDriverThread, pCopyInfo, 0L, &dwIgnore);

    if (!hThreadCopy) {
        //
        // Must free everything
        //
        LocalFree(pCopyInfo->pFrom);
        LocalFree(pCopyInfo->pTo);
        LocalFree(pCopyInfo);

        return GetLastError();
    }

    CloseHandle(hThreadCopy);

    return 0;
}

/////////////////////////////////////////////////////////////////////
//
// Name:     WFMoveCopyDriverThread
//
// Synopsis: The following function is the mainline function for
//           COPYing, RENAMEing, DELETing, and MOVEing single/multiple files.
//
// pFrom - String containing list of source specs
// pTo   - String containing destination specs
// dwFunc - Operation to be performed.  Possible values are:
//         FUNC_DELETE - Delete files in pFrom
//         FUNC_RENAME - Rename files (same directory)
//         FUNC_MOVE   - Move files in pFrom to pTo (different disk)
//         FUNC_COPY   - Copy files in pFrom to pTo
//
//
// Return: void
//
//         On finish, sends FS_COPYDONE back.
//            wParam == error code
//            lParam == pCopyInfo identifier (to prevent previous aborted
//                      copies from aborting current copy)
//
// Assumes:
//
// Effects:
//
//
// Notes:  Needs to check for pathnames that are too large!
//         HWND hDlg, LPWSTR pFrom, LPWSTR pTo, DWORD dwFunc)
//
/////////////////////////////////////////////////////////////////////

DWORD
WINAPI
WFMoveCopyDriverThread(LPVOID lpParameter) {
    PCOPYINFO pCopyInfo = (PCOPYINFO)lpParameter;
    DWORD ret = 0;                         // Return value from WFMoveCopyDriver
    LPWSTR pSpec = NULL;                   // Pointer to file spec
    DWORD dwAttr;                          // File attributes
    DWORD dwResponse;                      // Response from ConfirmDialog call
    DWORD oper = 0;                        // Disk operation being performed
    WCHAR szDestSpec[MAXFILENAMELEN + 1];  // Dest file spec
    WCHAR szDest[2 * MAXPATHLEN];          // Dest file (ANSI string)

    WCHAR szTemp[MAXPATHLEN];

    WCHAR szSource[MAXPATHLEN];   // Source file (ANSI string)
    LFNDTA DTADest;               // DTA block for reporting dest errors
    PLFNDTA pDTA = NULL;          // DTA pointer for source errors
    PCOPYROOT pcr;                // Structure for searching source tree
    BOOL bReplaceAll = FALSE;     // Replace all flag
    BOOL bSubtreeDelAll = FALSE;  // Delete entire subtree flag
    BOOL bDeleteAll = FALSE;      // Delete all files flag

    BOOL bReplaceReadOnlyAll = FALSE;     // Replace all flag
    BOOL bSubtreeDelReadOnlyAll = FALSE;  // Delete entire subtree flag
    BOOL bDeleteReadOnlyAll = FALSE;      // Delete all files flag

    BOOL bNoAccessAll = FALSE;
    BOOL bDirNotEmpty = FALSE;

    BOOL bFalse = FALSE;  // For cases that aren't disableable
    int CurIDS = 0;       // Current string displayed in status

    BOOL bErrorOnDest = FALSE;
    BOOL bIsLFNDriveDest = FALSE;

    BOOL bSameFile;      // Source, dest same file?
    BOOL bDoMoveRename;  // OPER_DOFILE and FUNC_{RENAME,MOVE}

    BOOL bConfirmed;
    BOOL bFatalError = FALSE;
    BOOL bErrorOccured = FALSE;

    // Initialization stuff.  Disable all file system change processing until
    // we're all done

    SwitchToSafeDrive();

    szDest[0] = szSource[0] = CHAR_NULL;
    SendMessage(hwndFrame, FS_DISABLEFSC, 0, 0L);

    //
    // Change all '/' characters to '\' characters in dest spec
    //
    CheckSlashes(pCopyInfo->pFrom);

    //
    // Check for multiple source files
    //
    ManySource = CheckMultiple(pCopyInfo->pFrom);

    //
    // Allocate buffer for searching the source tree
    //
    pcr = (PCOPYROOT)LocalAlloc(LPTR, sizeof(COPYROOT));
    if (!pcr) {
        ret = DE_INSMEM;
        goto ShowMessageBox;
    }

    // Skip destination specific processing if we are deleting files

    if (pCopyInfo->dwFunc != FUNC_DELETE) {
        // it is an error condition if there are multiple files
        // specified as the dest (but not a single directory)

        pSpec = GetNextFile(pCopyInfo->pTo, szTemp, COUNTOF(szTemp));

        if (GetNextFile(pSpec, szTemp, MAXPATHLEN) != NULL) {
            // move, copy specified with multiple destinations
            // not allowed, error case

            ret = DE_MANYDEST;
            goto ShowMessageBox;
        }

        lstrcpy(pCopyInfo->pTo, szTemp);

        QualifyPath(pCopyInfo->pTo);

        if (pCopyInfo->dwFunc == FUNC_RENAME) {
            // don't let them rename multiple files to one single file

            if ((ManySource == 1) && !IsWild(pCopyInfo->pTo)) {
                ret = DE_MANYSRC1DEST;
                goto ShowMessageBox;
            }

        } else {
            // We are either executing FUNC_COPY or FUNC_MOVE at this point.
            // Check that the destination disk is there.  NOTE: There's a disk
            // access here slowing us down.

            //
            // Change this from IsTheDiskReallyThere to CheckDrive to handle
            // restoring connections.
            //
            if (CHAR_COLON == pCopyInfo->pTo[1]) {
                if (!CheckDrive(hdlgProgress, DRIVEID(pCopyInfo->pTo), pCopyInfo->dwFunc))
                    goto CancelWholeOperation;
            }

            // deal with case where directory is implicit in source
            // move/copy: *.* -> c:\windows, c:\windows -> c:\temp
            // or foo.bar -> c:\temp
            // do this not for junction or symlinks

            if (!IsWild(pCopyInfo->pTo) && (ManySource || IsDirectory(pCopyInfo->pTo)) &&
                pCopyInfo->dwFunc != FUNC_LINK && pCopyInfo->dwFunc != FUNC_HARD) {
                AddBackslash(pCopyInfo->pTo);
                lstrcat(pCopyInfo->pTo, kStarDotStar);
            }
        }

        // FUNC_RENAME or FUNC_MOVE FUNC_COPY with a file name dest
        // (possibly including wildcards).  Save the filespec and the path
        // part of the destination

        pSpec = FindFileName(pCopyInfo->pTo);
        lstrcpy(szDestSpec, pSpec);
        lstrcpy(szDest, pCopyInfo->pTo);
        RemoveLast(szDest);

        pSpec = szDest + lstrlen(szDest);

        bIsLFNDriveDest = IsLFNDrive(pCopyInfo->pTo);
    }
    pcr->pSource = pCopyInfo->pFrom;

    //
    // Set up arguments for queued copy commands
    //

    while (pcr) {
        // Allow the user to abort the operation

        if (pCopyInfo->bUserAbort)
            goto CancelWholeOperation;

        // Clean off the last filespec for multiple file copies

        if (pCopyInfo->dwFunc != FUNC_DELETE) {
            *pSpec = CHAR_NULL;
        }

        oper = GetNextPair(pcr, szSource, szDest, szDestSpec, pCopyInfo->dwFunc, &ret, bIsLFNDriveDest);

        pcr->bFastMove = FALSE;

        // Check for no operation or error

        if (!oper) {
            LocalFree((HANDLE)pcr);
            pcr = NULL;
            break;
        }
        if ((oper & OPER_MASK) == OPER_ERROR) {
            oper = OPER_DOFILE;
            bFatalError = TRUE;
            goto ShowMessageBox;
        }

        pDTA = CurPDTA(pcr);

        if (pCopyInfo->bUserAbort)
            goto CancelWholeOperation;

        // Don't MKDIR on the root.
        // This is the one anomaly of winfile's drag/drop copy.
        // Dragging a folder usually creates the dragged folder in
        // the destination (drag directory "foo" to "bar" and the
        // directory "foo" appears in "bar").  But when dragging the
        // root directory, there is no directory "root" or "\\".
        // Instead, the roots contents are placed directly in the
        // destination folder.

        // On other OSs, dragging a root (i.e., the entire disk) to
        // another folder creates a new folder named after the source
        // disk.

        if ((oper == OPER_MKDIR) && (szSource[lstrlen(szSource) - 1] == CHAR_BACKSLASH)) {
            continue;
        }

        // Fix up source spec

        if (ret = IsInvalidPath(szSource)) {
            goto ShowMessageBox;
        }

        // Moved up for the wacky case of copy to floppy,
        // disk full, swap in new disk, new disk already has
        // the same file on it --> now it will put up the confirm
        // dialog (again).

        bConfirmed = FALSE;

    TRY_COPY_AGAIN:

        if (pCopyInfo->dwFunc != FUNC_DELETE) {
            //
            // If same name and copying, attempt to add a "- Copy" suffix.
            //
            bSameFile = !lstrcmpi(szSource, szDest);
            if (bSameFile &&
                (pCopyInfo->dwFunc == FUNC_COPY || pCopyInfo->dwFunc == FUNC_LINK || pCopyInfo->dwFunc == FUNC_HARD) &&
                (oper != OPER_RMDIR)) {
                // Source and destination are exactly the same
                WCHAR szDestAlt[MAXPATHLEN + 2] = { 0 };
                WCHAR szExtension[MAXPATHLEN + 2] = { 0 };
                LPWSTR pExt;

                lstrcpy(szDestAlt, szDest);

                // Lets try to apply the 'Copy' pattern, e.g. 'file.ext' -> 'file - Copy.ext'
                pExt = PathFindExtension(szDestAlt);
                if (*pExt) {
                    // Split of extension if available
                    lstrcpy(szExtension, pExt);
                    *pExt = '\0';
                }

                // Postfix the operation
                switch (pCopyInfo->dwFunc) {
                    case FUNC_COPY:
                        lstrcat(szDestAlt, L" - Copy");
                        break;

                    case FUNC_LINK:
                        lstrcat(szDestAlt, L" - Symlink");
                        break;

                    case FUNC_HARD:
                        lstrcat(szDestAlt, L" - Hardlink");
                        break;
                }

                lstrcat(szDestAlt, szExtension);

                // We only do a one level '- Copy' postfixing, and do intentionally not go for a '- Copy (n)' postfix
                if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(szDestAlt)) {
                    lstrcpy(szDest, szDestAlt);
                    bSameFile = FALSE;
                } else {
                    // If one already used this '- Copy' postfix, bail out. Just one level.
                    ret = DE_RENAMREPLACE;
                    goto ShowMessageBox;
                }
            }
            if (ret = IsInvalidPath(szDest)) {
                bErrorOnDest = TRUE;
                goto ShowMessageBox;
            }

            //
            // Check to see if we are overwriting an existing file.  If so,
            // better confirm.
            //
            bDoMoveRename = OPER_DOFILE == oper && (FUNC_RENAME == pCopyInfo->dwFunc || FUNC_MOVE == pCopyInfo->dwFunc);
            if (oper == OPER_DOFILE && !(bSameFile && bDoMoveRename)) {
                if (WFFindFirst(&DTADest, szDest, ATTR_ALL)) {
                    WCHAR szShortSource[MAXPATHLEN];
                    WCHAR szShortDest[MAXPATHLEN];

                    WFFindClose(&DTADest);

                    //
                    // We may be renaming a lfn to its shortname or backwards
                    // (e.g., "A Long Filename.txt" to "alongf~1.txt")
                    // Only put up the dialog if this _isn't_ the case.
                    //
                    // We are actually hosed since the any directory
                    // in the path could be a longname/shortname pair.
                    // The only "correct" way of doing this is to use
                    // GetShortPathName.
                    //

                    GetShortPathName(szDest, szShortDest, COUNTOF(szShortDest));

                    GetShortPathName(szSource, szShortSource, COUNTOF(szShortSource));

                    if (!lstrcmpi(szShortSource, szShortDest)) {
                        ret = DE_SAMEFILE;
                        goto ShowMessageBox;
                    }

                    if (pCopyInfo->dwFunc == FUNC_RENAME) {
                        ret = DE_RENAMREPLACE;
                        goto ShowMessageBox;
                    }

                    //
                    //  Save the attributes, since ConfirmDialog may change them.
                    //
                    dwAttr = GetFileAttributes(szDest);

                    // we need to check if we are trying to copy a file
                    // over a directory and give a reasonable error message

                    dwResponse = bConfirmed
                        ? IDYES
                        : ConfirmDialog(
                              hdlgProgress, CONFIRMREPLACE, szDest, &DTADest, szSource, pDTA, bConfirmReplace,
                              &bReplaceAll, bConfirmReadOnly, &bReplaceReadOnlyAll);

                    switch (dwResponse) {
                        case IDYES:  // Perform the delete

                            if ((pCopyInfo->dwFunc == FUNC_MOVE) || (pCopyInfo->dwFunc == FUNC_HARD) ||
                                (pCopyInfo->dwFunc == FUNC_LINK)) {
                                // For FUNC_MOVE/FUNC_HARD/FUNC_LINK we need to delete the
                                // destination first.  Do that now.

                                if (DTADest.fd.dwFileAttributes & ATTR_DIR) {
                                    if (IsCurrentDirectory(szDest))
                                        CdDotDot(szDest);

                                    //
                                    // Remove directory
                                    //

                                    ret = RMDir(szDest);

                                    if (ERROR_SHARING_VIOLATION == ret) {
                                        //
                                        // We could have been watching this
                                        // with the notify system
                                        //

                                        //
                                        // Only do this for non-UNC
                                        //
                                        if (CHAR_COLON == szSource[1]) {
                                            NotifyPause(DRIVEID(szSource), (UINT)-1);
                                            ret = RMDir(szSource);
                                        }
                                    }
                                } else {
                                    //
                                    // On move, must delete destination file
                                    // on copy, the fs does this for us.
                                    //
                                    ret = SafeFileRemove(szDest);

                                    //
                                    //  Reset the file attributes that may have been
                                    //  changed in the ConfirmDialog call.
                                    //
                                    //  This only happens in the ID_YES case for a
                                    //  file (directories are not affected).
                                    //
                                    if (ret) {
                                        SetFileAttributes(szDest, dwAttr);
                                    }
                                }

                                if (ret) {
                                    bErrorOnDest = TRUE;
                                    goto ShowMessageBox;
                                }

                                //
                                // no need to:
                                // else  ret = SafeFileRemove (szDest);
                                //
                                // since the dest will be handled by the fs.
                                // (Note that the "if (ret)" is moved into the
                                // upper if clause since we don't set ret to
                                // the SafeFileRemove return value.
                                //
                            }
                            break;

                        case IDNO:

                            // Don't perform operation on current file

                            continue;

                        case IDCANCEL:
                            goto CancelWholeOperation;

                        default:
                            ret = dwResponse;
                            goto ShowMessageBox;
                    }
                }
            }
        }

        // Now determine which operation to perform

        switch (oper | pCopyInfo->dwFunc) {
            case OPER_MKDIR | FUNC_COPY:  // Create destination directory
            case OPER_MKDIR | FUNC_MOVE:  // Create dest, verify source delete

                CurIDS = IDS_CREATINGMSG;
                Notify(hdlgProgress, IDS_CREATINGMSG, szDest, kEmptyString);

                if (pCopyInfo->dwFunc == FUNC_MOVE) {
                    if ((CHAR_COLON == pcr->sz[1]) && (CHAR_COLON == szDest[1]) &&
                        (DRIVEID(pcr->sz) == DRIVEID(szDest))) {
                        //
                        // Warning: This will not work on winball drives!
                        // There is a problem here: if this is winball,
                        // then we get an ERROR_ACCESS_DENIED when we try to do
                        // the fastmove.  We could continue normally with the
                        // slowmove.  However, in the case of moving NTFS directories,
                        // we may get ERROR_ACCESS_DENIED because we don't have
                        // write permission on the directory.  In this case, we
                        // want to ignore the entire directory.
                        //
                        // For now, this works because DTAFast.err is neither
                        // ERROR_PATH_NOT_FOUND ERROR_FILE_NOT_FOUND, do we're
                        // ok by accident.
                        //

                        pcr->bFastMove = TRUE;
                        goto DoMove;
                    }
                }

            DoMkDir:

                ret = WF_CreateDirectory(hdlgProgress, szDest, szSource);

                if (!ret)
                    //
                    // set attributes of dest to source (not including the
                    // subdir and vollabel bits)
                    //
                    WFSetAttr(szDest, pDTA->fd.dwFileAttributes & ~(ATTR_DIR | ATTR_VOLUME));

                //
                // If it already exists ignore the error return
                // as long as it is a directory and not a file.
                //
                if (ret == ERROR_ALREADY_EXISTS) {
                    ret = WFIsDir(szDest) ? ERROR_SUCCESS : DE_DIREXISTSASFILE;
                }

                if (ret)
                    bErrorOnDest = TRUE;

                break;

            // FUNC_HARD is here, because one can select a directory in the file-pane and have SHIFT+CTRL+ALT pressed to
            // create a junction. In this very case we get a Junction creation 'disguised' as FUNC_HARD
            case OPER_MKDIR | FUNC_HARD:
            case OPER_MKDIR | FUNC_LINK: {
                // Create symbolic link or junction
                if (WFFindFirst(&DTADest, szDest, ATTR_ALL)) {
                    WFFindClose(&DTADest);

                    dwResponse = ConfirmDialog(
                        hdlgProgress, CONFIRMREPLACE, szDest, &DTADest, szSource, pDTA, bConfirmSubDel, &bSubtreeDelAll,
                        bConfirmReadOnly, &bSubtreeDelReadOnlyAll);

                    switch (dwResponse) {
                        case IDYES:  // Perform the delete
                            RMDir(szDest);
                            break;

                        case IDNO:
                            continue;

                        case IDCANCEL:
                            goto CancelWholeOperation;
                    }
                }
                CurIDS = IDS_CREATINGMSG;
                Notify(hdlgProgress, IDS_CREATINGMSG, szDest, kEmptyString);
                switch (pCopyInfo->dwFunc) {
                    case FUNC_LINK:
                        ret = WFSymbolicLink(szSource, szDest, SYMBOLIC_LINK_FLAG_DIRECTORY);
                        break;

                    case FUNC_HARD:
                        ret = WFJunction(szDest, szSource);
                        break;
                }

                //
                // If symlink dir already exists ignore the error. return
                // as long as it is a directory and not a file.
                //
                if (ERROR_ALREADY_EXISTS == ret) {
                    ret = WFIsDir(szDest) ? ERROR_SUCCESS : DE_DIREXISTSASFILE;
                }

                // Don't follow a reparse point in the source. Stop recursion of GetNextPair for this entry
                pcr->cDepth = 0;

                if (ret != ERROR_SUCCESS)
                    bErrorOnDest = TRUE;
            } break;

            case OPER_MKDIR | FUNC_DELETE:

                // Confirm removal of directory on this pass.  The directories
                // are actually removed on the OPER_RMDIR pass

                // We can't delete the root directory, so don't bother
                // confirming it

                if (IsRootDirectory(szSource))
                    break;

                if (bConfirmed)
                    break;

                dwResponse = ConfirmDialog(
                    hdlgProgress, CONFIRMRMDIR, NULL, pDTA, szSource, NULL, bConfirmSubDel, &bSubtreeDelAll,
                    bConfirmReadOnly, &bSubtreeDelReadOnlyAll);

                switch (dwResponse) {
                    case IDYES:
                        break;

                    case IDNO:
                    case IDCANCEL:
                        goto CancelWholeOperation;

                    default:
                        ret = dwResponse;
                        goto ShowMessageBox;
                }
                break;

            case OPER_RMDIR | FUNC_MOVE:
            case OPER_RMDIR | FUNC_DELETE:

                CurIDS = IDS_REMOVINGDIRMSG;
                Notify(hdlgProgress, IDS_REMOVINGDIRMSG, szSource, kEmptyString);
                if (IsRootDirectory(szSource))
                    break;
                if (IsCurrentDirectory(szSource))
                    CdDotDot(szSource);

                //
                // We already confirmed the delete at MKDIR time, so attempt
                // to delete the directory
                //

                if (pCopyInfo->dwFunc == FUNC_DELETE) {
                    // For delete operations, send the entire directory to recycle bin
                    // instead of permanently deleting it
                    ret = MoveFileToRecycleBin(szSource);
                    if (ret == 0)
                        ChangeFileSystem(FSC_RMDIR, szSource, NULL);
                } else {
                    // For move operations, use the original logic
                    //
                    // Tuck away the attribs in case we fail.
                    //
                    dwAttr = GetFileAttributes(szSource);

                    WFSetAttr(szSource, FILE_ATTRIBUTE_NORMAL);

                    ret = RMDir(szSource);

                    if (ERROR_SHARING_VIOLATION == ret) {
                        //
                        // We could have been watching this with the notify system
                        //

                        //
                        // Only do this for non-UNC
                        //
                        if (CHAR_COLON == szSource[1]) {
                            NotifyPause(DRIVEID(szSource), (UINT)-1);
                            ret = RMDir(szSource);
                        }
                    }
                    //
                    // On failure, restore attributes
                    //
                    if (ret && INVALID_FILE_ATTRIBUTES != dwAttr)
                        WFSetAttr(szSource, dwAttr);
                }

                break;

            case OPER_RMDIR | FUNC_COPY:
                break;

            case OPER_DOFILE | FUNC_HARD:
            case OPER_DOFILE | FUNC_LINK:
            case OPER_DOFILE | FUNC_COPY:

                if (IsWindowsFile(szDest)) {
                    ret = DE_WINDOWSFILE;
                    bErrorOnDest = TRUE;
                    break;
                }

                //
                // Now try to process the file.  Do extra error processing only
                //      in 2 cases:
                //
                //  1) If a floppy is full let the user stick in a new disk
                //  2) If the path doesn't exist (the user typed in
                //     an explicit path that doesn't exist) ask if
                //     we should create it.

                // NOTE:  This processing is normally done by WFCopy.  But in
                //              the case where LFN copy support is invoked, we have
                //              to support this error condition here.  Modified by
                //    C. Stevens, August 1991

                switch (pCopyInfo->dwFunc) {
                    case FUNC_COPY:
                        ret = WFCopy(szSource, szDest);
                        break;

                    case FUNC_LINK:
                        ret = WFSymbolicLink(szSource, szDest, 0);
                        break;

                    case FUNC_HARD:
                        ret = WFHardLink(szSource, szDest);
                        break;
                }

                if (pCopyInfo->bUserAbort)
                    goto CancelWholeOperation;

                if (((ret == ERROR_DISK_FULL) && IsRemovableDrive(DRIVEID(szDest))) || (ret == ERROR_PATH_NOT_FOUND)) {
                    //
                    // There was an error, so delete the file that was just written
                    // incorrectly.
                    //
                    // NOTE:  Must first make sure the attributes are clear so that
                    //        the delete will always succeed.
                    //
                    SetFileAttributes(szDest, FILE_ATTRIBUTE_NORMAL);
                    DeleteFile(szDest);

                    //
                    // Show retry popup.
                    //
                    ret = CopyMoveRetry(szDest, ret, &bErrorOnDest);
                    if (!ret)
                        goto TRY_COPY_AGAIN;

                    else if (DE_OPCANCELLED == ret)
                        goto CancelWholeOperation;
                }

                break;

            case OPER_DOFILE | FUNC_RENAME: {
                WCHAR save1, save2;
                LPWSTR p;

                if (CurIDS != IDS_RENAMINGMSG) {
                    CurIDS = IDS_RENAMINGMSG;
                    Notify(hdlgProgress, IDS_RENAMINGMSG, kEmptyString, kEmptyString);
                }

                // Get raw source and dest paths.  Check to make sure the
                // paths are the same

                p = FindFileName(szSource);
                save1 = *p;
                *p = CHAR_NULL;
                p = FindFileName(szDest);
                save2 = *p;
                *p = CHAR_NULL;
                ret = lstrcmpi(szSource, szDest);
                szSource[lstrlen(szSource)] = save1;
                szDest[lstrlen(szDest)] = save2;
                if (ret) {
                    ret = DE_DIFFDIR;
                    break;
                }
                goto DoMoveRename;
            }

            case OPER_DOFILE | FUNC_MOVE:

            DoMove:

                if (CurIDS != IDS_MOVINGMSG) {
                    CurIDS = IDS_MOVINGMSG;
                    Notify(hdlgProgress, IDS_MOVINGMSG, kEmptyString, kEmptyString);
                }
            DoMoveRename:

                // Don't allow the user to rename from or to the root
                // directory

                if (IsRootDirectory(szSource)) {
                    ret = DE_ROOTDIR;
                    break;
                }
                if (IsRootDirectory(szDest)) {
                    ret = DE_ROOTDIR;
                    bErrorOnDest = TRUE;
                    break;
                }

                if (IsCurrentDirectory(szSource))
                    CdDotDot(szSource);

                //
                //  Save the attributes, since ConfirmDialog may change them.
                //
                dwAttr = GetFileAttributes(szSource);

                // Confirm the rename

                if (!bConfirmed) {
                    dwResponse = ConfirmDialog(
                        hdlgProgress, (pCopyInfo->dwFunc == FUNC_MOVE ? CONFIRMMOVE : CONFIRMRENAME), NULL, pDTA,
                        szSource, NULL, FALSE, &bFalse, bConfirmReadOnly, &bReplaceReadOnlyAll);

                    switch (dwResponse) {
                        case IDYES:
                            break;

                        case IDNO:
                            continue;

                        case IDCANCEL:
                            goto CancelWholeOperation;

                        default:
                            ret = dwResponse;
                            goto ShowMessageBox;
                    }
                }

                if (IsWindowsFile(szSource)) {
                    ret = DE_WINDOWSFILE;
                } else {
                    //
                    // always do move!  Even across partitions!
                    //
                    ret = WFMove(szSource, szDest, &bErrorOnDest, pcr->bFastMove);

                    if (ERROR_SHARING_VIOLATION == ret && pDTA->fd.dwFileAttributes & ATTR_DIR) {
                        //
                        // We could have been watching this with the notify system
                        //

                        //
                        // Only do this for non-UNC
                        //
                        if (CHAR_COLON == szSource[1]) {
                            NotifyPause(DRIVEID(szSource), (UINT)-1);
                            ret = WFMove(szSource, szDest, &bErrorOnDest, pcr->bFastMove);
                        }
                    }

                    if (ret == DE_OPCANCELLED)
                        goto CancelWholeOperation;

                    if (pcr->bFastMove) {
                        //
                        // In the case of access denied, don't try and recurse
                        // since on NTFS, this will fail also.
                        //

                        if (ret) {
                            //
                            // We failed.
                            //
                            // Go back to MkDir.  We are no longer doing a fast move.
                            //

                            pcr->bFastMove = FALSE;
                            goto DoMkDir;
                        }
                    }

                    if (!ret) {
                        // set attributes of dest to those of the source
                        WFSetAttr(szDest, pDTA->fd.dwFileAttributes);
                    } else {
                        //
                        //  Reset the attributes on the source file, since they
                        //  may have been changed by ConfirmDialog.
                        //
                        SetFileAttributes(szSource, dwAttr);
                    }

                    if (pCopyInfo->bUserAbort)
                        goto CancelWholeOperation;
                }

                break;

            case OPER_DOFILE | FUNC_DELETE:

                if (CurIDS != IDS_DELETINGMSG) {
                    CurIDS = IDS_DELETINGMSG;
                    Notify(hdlgProgress, IDS_DELETINGMSG, kEmptyString, kEmptyString);
                }

                //
                //  Save the attributes, since ConfirmDialog may change them.
                //
                dwAttr = GetFileAttributes(szSource);

                // Confirm the delete first

                if (!bConfirmed) {
                    dwResponse = ConfirmDialog(
                        hdlgProgress, CONFIRMDELETE, NULL, pDTA, szSource, NULL, bConfirmDelete, &bDeleteAll,
                        bConfirmReadOnly, &bDeleteReadOnlyAll);

                    switch (dwResponse) {
                        case IDYES:
                            break;

                        case IDNO:

                            // Set flag: not all deleted!
                            bDirNotEmpty = TRUE;

                            continue;

                        case IDCANCEL:
                            goto CancelWholeOperation;

                        default:
                            ret = dwResponse;
                            goto ShowMessageBox;
                    }
                }

                // make sure we don't delete any open windows
                // apps or dlls (lets hope this isn't too slow)

                ret = SafeFileRemove(szSource);

                //
                //  Reset the file attributes, since ConfirmDialog may have
                //  changed them.
                //
                if (ret) {
                    SetFileAttributes(szSource, dwAttr);
                }

                break;

            default:
                ret = DE_HOWDIDTHISHAPPEN;  // internal error
                break;
        }

        // Report any errors which have occurred

        if (ret) {
        ShowMessageBox:

            //
            // Currently, deleting a non-empty dir does NOT
            // return an error if an error occurred before.
            // This is allowed because the user may have chose
            // ignore.
            //
            if (ERROR_DIR_NOT_EMPTY == ret) {
                bDirNotEmpty = TRUE;

                if (bErrorOccured)
                    continue;
            }

            // Extra scope to contain errorIndex.
            {
                int errorIndex = pCopyInfo->dwFunc;
                if (errorIndex == FUNC_HARD && IsDirectory(szSource))
                    errorIndex = FUNC_JUNC;
                ret = CopyError(szSource, szDest, ret, errorIndex, oper, bErrorOnDest, bFatalError);
            }

            switch (ret) {
                case DE_RETRY:
                    bConfirmed = TRUE;
                    goto TRY_COPY_AGAIN;

                case DE_OPCANCELLED:

                    //
                    // Since we are cancelling an op, we definitely know that
                    // an error occurred.
                    //
                    bErrorOccured = TRUE;
                    ret = 0;

                    break;

                default:

                CancelWholeOperation:

                    // Force a CopyCleanup in case there are any files in the
                    // copy queue

                    pCopyInfo->bUserAbort = TRUE;
                    goto ExitLoop;
            }
        }
    }

ExitLoop:

    // Copy any outstanding files in the copy queue

    // this happens in error cases where we broke out of the pcr loop
    // without hitting the end

    if (pcr) {
        GetNextCleanup(pcr);
        LocalFree((HANDLE)pcr);
    }

    //
    // goofy way to make sure we've gotten all the WM_FSC messages
    //
    CleanupMessages();

    SendMessage(hwndFrame, FS_ENABLEFSC, 0, 0L);

    //
    // If we left with anything extra, tell user.
    //
    // But not if we the user aborted.
    //
    if (bDirNotEmpty && !pCopyInfo->bUserAbort) {
        WCHAR szMessage[MAXMESSAGELEN];
        WCHAR szTitle[MAXTITLELEN];

        LoadString(hAppInstance, IDS_COPYMOVENOTCOMPLETED, szTitle, COUNTOF(szTitle));
        LoadString(hAppInstance, IDS_DIRREMAINS, szMessage, COUNTOF(szMessage));

        MessageBox(hdlgProgress, szMessage, szTitle, MB_ICONSTOP);
    }

    NotifyResume(-1, (UINT)-1);

    SendMessage(hdlgProgress, FS_COPYDONE, ret, (LPARAM)pCopyInfo);

    LocalFree(pCopyInfo->pFrom);
    LocalFree(pCopyInfo->pTo);
    LocalFree(pCopyInfo);

    return 0;
}

//--------------------------------------------------------------------------*/
//
//  DMMoveCopyHelper() -
//
//--------------------------------------------------------------------------*/

// Used by Danger Mouse to do moves and copies.

DWORD
DMMoveCopyHelper(LPWSTR pFrom, LPWSTR pTo, int iOperation) {
    DWORD dwStatus;
    LPWSTR pTemp;
    PCOPYINFO pCopyInfo;

    WCHAR szConfirmFile[MAXPATHLEN + 1];
    HDC hDC;

    //
    //  If either pointer is null, then return.
    //
    if (!pFrom || !pTo)
        return (0);

    //
    // Confirm mouse operations.
    //
    if (bConfirmMouse) {
        int iConfirmMsg = IDS_MOVEMOUSECONFIRM;
        switch (iOperation) {
            case DROP_COPY:
                iConfirmMsg = IDS_COPYMOUSECONFIRM;
                break;
            case DROP_LINK:
            case DROP_HARD:
                iConfirmMsg = IDS_LINKMOUSECONFIRM;
                break;

            default:
            case DROP_MOVE:
                iConfirmMsg = IDS_MOVEMOUSECONFIRM;
        }
        LoadString(hAppInstance, iConfirmMsg, szTitle, COUNTOF(szTitle));

        lstrcpy(szConfirmFile, pTo);
        pTemp = FindFileName(szConfirmFile);

        // Kill trailing backslash if not to the root directory.
        if ((pTemp - szConfirmFile) > 3)
            pTemp--;

        // Do check by look at end...
        // ( for "\"f:\\this is\\a\"\\test" string  (  "f:\this is\a"\test  )
        // A better test would be to see if pTemp has an odd number of quotes
        if (CHAR_DQUOTE == pTemp[lstrlen(pTemp) - 1]) {
            *pTemp = CHAR_DQUOTE;
            *(pTemp + 1) = CHAR_NULL;
        } else {
            *pTemp = CHAR_NULL;
        }

        hDC = GetDC(NULL);
        CompactPath(hDC, szConfirmFile, GetSystemMetrics(SM_CXSCREEN) / 4 * 3);
        ReleaseDC(NULL, hDC);
        wsprintf(szMessage, szTitle, szConfirmFile);

        LoadString(hAppInstance, IDS_MOUSECONFIRM, szTitle, COUNTOF(szTitle));

        if (MessageBox(hwndFrame, szMessage, szTitle, MB_YESNO | MB_ICONEXCLAMATION | MB_SETFOREGROUND) != IDYES)
            return DE_OPCANCELLED;
    }

    pCopyInfo = (PCOPYINFO)LocalAlloc(LPTR, sizeof(COPYINFO));

    if (!pCopyInfo) {
    Error:

        FormatError(TRUE, szMessage, COUNTOF(szMessage), GetLastError());
        LoadString(hAppInstance, IDS_WINFILE, szTitle, COUNTOF(szTitle));

        MessageBox(hwndFrame, szMessage, szTitle, MB_OK | MB_ICONEXCLAMATION);

        //
        // return error message
        //
        return ERROR_OUTOFMEMORY;
    }

    pCopyInfo->pFrom = (LPWSTR)LocalAlloc(LMEM_FIXED, ByteCountOf(lstrlen(pFrom) + 1));

    pCopyInfo->pTo = (LPWSTR)LocalAlloc(LMEM_FIXED, ByteCountOf(lstrlen(pTo) + 1));

    if (!pCopyInfo->pFrom || !pCopyInfo->pTo) {
        if (pCopyInfo->pFrom)
            LocalFree(pCopyInfo->pFrom);

        if (pCopyInfo->pTo)
            LocalFree(pCopyInfo->pTo);

        goto Error;
    }

    switch (iOperation) {
        case DROP_COPY:
            pCopyInfo->dwFunc = FUNC_COPY;
            break;
        case DROP_LINK:
            pCopyInfo->dwFunc = FUNC_LINK;
            break;
        case DROP_HARD:
            pCopyInfo->dwFunc = FUNC_HARD;
            break;

        default:
        case DROP_MOVE:
            pCopyInfo->dwFunc = FUNC_MOVE;
            break;
    }
    pCopyInfo->bUserAbort = FALSE;

    lstrcpy(pCopyInfo->pFrom, pFrom);
    lstrcpy(pCopyInfo->pTo, pTo);

    dwStatus = (DWORD)DialogBoxParam(
        hAppInstance, (LPWSTR)MAKEINTRESOURCE(DMSTATUSDLG), hwndFrame, ProgressDlgProc, (LPARAM)pCopyInfo);

    return dwStatus;
}

DWORD
FileRemove(LPWSTR pSpec) {
    // Use recycle bin instead of permanent deletion
    return MoveFileToRecycleBin(pSpec);
}

DWORD
FileMove(LPWSTR pFrom, LPWSTR pTo, PBOOL pbErrorOnDest, BOOL bSilent) {
    DWORD result;
    BOOL bTried = FALSE;
    LPWSTR pTemp;

    *pbErrorOnDest = FALSE;

TryAgain:
    if (MoveFile((LPWSTR)pFrom, (LPWSTR)pTo))
        result = 0;
    else
        result = GetLastError();

    // try to create the destination if it is not there

    if (result == ERROR_PATH_NOT_FOUND) {
        if (bSilent) {
            if (!bTried) {
                //
                // Silently create the directory
                //
                pTemp = FindFileName(pTo) - 1;
                *pTemp = CHAR_NULL;

                result = WF_CreateDirectory(hdlgProgress, pTo, NULL);

                *pTemp = CHAR_BACKSLASH;
                bTried = TRUE;

                if (!result)
                    goto TryAgain;
            }
            return result;
        }

        result = CopyMoveRetry(pTo, (int)result, pbErrorOnDest);
        if (!result)
            goto TryAgain;
        else
            return result;
    }
    return (result);
}

/*============================================================================
;
; CopyError
;
; The following function reports an error during a file copy operation
;
; Parameters
;
; lpszSource - Source file name
; lpszDest   - Destination file name
; nError     - dos (or our extended) error code
;
; dwFunc      - Operation being performed during error.  Can be one of:
;              FUNC_DELETE - Delete files in pFrom
;              FUNC_RENAME - Rename files (same directory)
;              FUNC_MOVE   - Move files in pFrom to pTo (different disk)
;              FUNC_COPY   - Copy files in pFrom to pTo
; nOper      - Operation being performed.  Can be one of:
;              OPER_ERROR  - Error processing filenames
;              OPER_DOFILE - Go ahead and copy, rename, or delete file
;              OPER_MKDIR  - Make a directory specified in pTo
;              OPER_RMDIR  - Remove directory
;              0           - No more files left
;
;
; GLOBAL:      ManySource  - many source files?
;
; Return Value: None
;
; Written by C. Stevens, August 1991
;
============================================================================*/

DWORD
CopyError(
    LPWSTR pszSource,
    LPWSTR pszDest,
    DWORD dwError,
    DWORD dwFunc,
    int nOper,
    BOOL bErrorOnDest,
    BOOL bFatalError) {
    WCHAR szVerb[MAXERRORLEN];   /* Verb describing error */
    WCHAR szReason[MAXERRORLEN]; /* Reason for error */
    WCHAR szFile[MAXPATHLEN + 1];
    WCHAR szTitle[MAXTITLELEN];
    WCHAR szMessage[MAXMESSAGELEN];
    HDC hDC;

    if (dwError == DE_OPCANCELLED)  // user abort
        return DE_OPCANCELLED;

    // Make sure the whole path name fits on the screen

    StrCpyN(szFile, bErrorOnDest ? pszDest : pszSource, COUNTOF(szFile));
    hDC = GetDC(NULL);
    CompactPath(hDC, szFile, GetSystemMetrics(SM_CXSCREEN) / 4 * 3);
    ReleaseDC(NULL, hDC);

    LoadString(hAppInstance, IDS_COPYERROR + dwFunc, szTitle, COUNTOF(szTitle));

    // get the verb string

    if (nOper == OPER_DOFILE || !nOper) {
        if (bErrorOnDest)
            // this is bogus, this could be IDS_CREATING as well...
            LoadString(hAppInstance, IDS_REPLACING, szVerb, COUNTOF(szVerb));
        else
            LoadString(hAppInstance, IDS_VERBS + dwFunc, szVerb, COUNTOF(szVerb));

    } else {
        LoadString(hAppInstance, IDS_ACTIONS + (nOper >> 8), szVerb, COUNTOF(szVerb));
    }

    // get the reason string

    FormatError(TRUE, szReason, COUNTOF(szReason), dwError);

    // use the files names or "Selected files" if file list too long

    if (!nOper && (lstrlen(pszSource) > 64))
        LoadString(hAppInstance, IDS_SELECTEDFILES, pszSource, 64);

    wsprintf(szMessage, szVerb, szFile, szReason);

    switch (MessageBox(
        hdlgProgress, szMessage, szTitle,
        bFatalError || !ManySource ? MB_ICONSTOP | MB_OK : MB_ABORTRETRYIGNORE | MB_ICONSTOP | MB_DEFBUTTON2)) {
        case IDRETRY:
            return DE_RETRY;
        case IDIGNORE:
            return DE_OPCANCELLED;
        case IDABORT:
        case IDOK:
        default:
            return dwError;
    }
}

/*============================================================================
;
; CopyMoveRetry
;
; The following function is used to retry failed move/copy operations
; due to out of disk situations or path not found errors
; on the destination.
;
; NOTE: the destination drive must be removable or this function
;   does not make a whole lot of sense
;
; Parameters:
;
; pszDest   - Fully qualified path to destination file
; nError    - Type of error which occurred: DE_NODISKSPACE or DE_PATHNOTFOUND
;
; returns:
;   0   success (destination path has been created)
;   != 0    dos error code including DE_OPCANCELLED
;
============================================================================*/

int CopyMoveRetry(LPWSTR pszDest, int nError, PBOOL pbErrorOnDest) {
    WCHAR szReason[128]; /* Error message string */
    LPWSTR pTemp;        /* Pointer into filename */
    WORD wFlags;         /* Message box flags */
    int result;          /* Return from MessageBox call */
    WCHAR szMessage[MAXMESSAGELEN];
    WCHAR szTitle[128];

    do {  // until the destination path has been created

        *pbErrorOnDest = FALSE;

        GetWindowText(hdlgProgress, szTitle, COUNTOF(szTitle));

        if (nError == ERROR_PATH_NOT_FOUND) {
            LoadString(hAppInstance, IDS_PATHNOTTHERE, szReason, COUNTOF(szReason));

            // Note the -1 below here is valid in both SBCS and DBCS because
            // pszDest is fully qualified and the character preceding the
            // file name must be a backslash

            pTemp = FindFileName(pszDest) - 1;
            *pTemp = CHAR_NULL;
            wsprintf(szMessage, szReason, pszDest);

            *pTemp = CHAR_BACKSLASH;
            wFlags = MB_ICONEXCLAMATION | MB_YESNO;

        } else {
            wFlags = MB_ICONEXCLAMATION | MB_OKCANCEL;
            LoadString(hAppInstance, IDS_DESTFULL, szMessage, COUNTOF(szMessage));
        }

        result = MessageBox(hdlgProgress, szMessage, szTitle, wFlags);

        if (result == IDOK || result == IDYES) {
            // Allow the disk to be formatted
            // BOOL bModal added (TRUE)
            if (!IsTheDiskReallyThere(hdlgProgress, pszDest, FUNC_COPY, TRUE))
                return DE_OPCANCELLED;

            pTemp = FindFileName(pszDest) - 1;
            *pTemp = CHAR_NULL;

            //
            // Is there a problem with this?
            //
            result = WF_CreateDirectory(hdlgProgress, pszDest, NULL);
            *pTemp = CHAR_BACKSLASH;

            // only as once if creating the destination failed

            if (result == DE_OPCANCELLED)
                return DE_OPCANCELLED;

            if (result && (nError == ERROR_PATH_NOT_FOUND)) {
                *pbErrorOnDest = TRUE;
                return result;
            }
        } else
            return DE_OPCANCELLED;

        // If it already exists, that's ok too.

        if (ERROR_ALREADY_EXISTS == result)
            result = 0;

    } while (result);

    return 0;  // success
}
