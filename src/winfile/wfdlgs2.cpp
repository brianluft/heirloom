/********************************************************************

   wfdlgs2.c

   More Windows File System Dialog procedures

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#include "winfile.h"
#include "lfn.h"
#include "wfcopy.h"
#include "wnetcaps.h"  // WNetGetCaps()
#include "commdlg.h"
#include "wfdos.h"
#include "wfutil.h"
#include "wfdir.h"
#include "wftree.h"
#include "treectl.h"
#include "wfsearch.h"
#include "stringconstants.h"
#include <shlobj.h>

BOOL NoQuotes(LPWSTR szT);

// Return pointers to various bits of a path.
// ie where the dir name starts, where the filename starts and where the
// params are.
void GetPathInfo(LPWSTR szTemp, LPWSTR* ppDir, LPWSTR* ppFile, LPWSTR* ppPar) {
    // handle quoted things
    BOOL bInQuotes = FALSE;

    // strip leading spaces

    for (*ppDir = szTemp; **ppDir == CHAR_SPACE; (*ppDir)++)
        ;

    // locate the parameters

    // Use bInQuotes and add if clause
    for (*ppPar = *ppDir; **ppPar && (**ppPar != CHAR_SPACE || bInQuotes); (*ppPar)++)
        if (CHAR_DQUOTE == **ppPar)
            bInQuotes = !bInQuotes;

    // locate the start of the filename and the extension.

    for (*ppFile = *ppPar; *ppFile > *ppDir; --(*ppFile)) {
        if (((*ppFile)[-1] == CHAR_COLON) || ((*ppFile)[-1] == CHAR_BACKSLASH))
            break;
    }
}

//
// Strips off the path portion and replaces the first part of an 8-dot-3
// filename with an asterisk.
//

void StarFilename(LPWSTR pszPath) {
    LPWSTR p;
    WCHAR szTemp[MAXPATHLEN];

    // Remove any leading path information.
    StripPath(pszPath);

    lstrcpy(szTemp, pszPath);

    p = GetExtension(szTemp);

    if (*p) {
        pszPath[0] = CHAR_STAR;
        lstrcpy(pszPath + 1, p - 1);
    } else {
        lstrcpy(pszPath, kStarDotStar);
    }
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  SearchDlgProc() -                                                       */
/*                                                                          */
/*--------------------------------------------------------------------------*/

INT_PTR
CALLBACK
SearchDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    LPWSTR p;
    WCHAR szStart[MAXFILENAMELEN];

    switch (wMsg) {
        case WM_INITDIALOG:

            SendDlgItemMessage(hDlg, IDD_DIR, EM_LIMITTEXT, COUNTOF(SearchInfo.szSearch) - 1, 0L);
            SendDlgItemMessage(hDlg, IDD_NAME, EM_LIMITTEXT, COUNTOF(szStart) - 1, 0L);

            GetSelectedDirectory(0, SearchInfo.szSearch);
            SetDlgItemText(hDlg, IDD_DIR, SearchInfo.szSearch);

            p = GetSelection(1, NULL);

            if (p) {
                GetNextFile(p, szStart, COUNTOF(szStart));
                StarFilename(szStart);
                SetDlgItemText(hDlg, IDD_NAME, szStart);
                LocalFree((HANDLE)p);
            }

            CheckDlgButton(hDlg, IDD_SEARCHALL, !SearchInfo.bDontSearchSubs);
            CheckDlgButton(hDlg, IDD_INCLUDEDIRS, SearchInfo.bIncludeSubDirs);

            if (SearchInfo.ftSince.dwHighDateTime != 0 || SearchInfo.ftSince.dwLowDateTime != 0) {
                FILETIME ftLocal;
                FileTimeToLocalFileTime(&SearchInfo.ftSince, &ftLocal);
                SYSTEMTIME st;
                FileTimeToSystemTime(&ftLocal, &st);
                DateTime_SetSystemtime(GetDlgItem(hDlg, IDD_DATE), GDT_VALID, &st);
            } else {
                DateTime_SetSystemtime(GetDlgItem(hDlg, IDD_DATE), GDT_NONE, NULL);
            }
            break;

        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam)) {
                case IDD_HELP:
                    goto DoHelp;

                case IDCANCEL:
                    EndDialog(hDlg, FALSE);
                    break;

                case IDOK: {
                    GetDlgItemText(hDlg, IDD_DIR, SearchInfo.szSearch, COUNTOF(SearchInfo.szSearch));
                    QualifyPath(SearchInfo.szSearch);

                    SearchInfo.ftSince.dwHighDateTime = SearchInfo.ftSince.dwLowDateTime = 0;
                    {
                        SYSTEMTIME st;
                        LRESULT dateResult = DateTime_GetSystemtime(GetDlgItem(hDlg, IDD_DATE), &st);
                        if (dateResult == GDT_VALID) {
                            FILETIME ftLocal;
                            SystemTimeToFileTime(&st, &ftLocal);
                            // SearchInfo.ftSince is in UTC (as are FILETIME in files to which this will be compared)
                            LocalFileTimeToFileTime(&ftLocal, &SearchInfo.ftSince);
                        }
                    }

                    GetDlgItemText(hDlg, IDD_NAME, szStart, COUNTOF(szStart));

                    KillQuoteTrailSpace(szStart);

                    AppendToPath(SearchInfo.szSearch, szStart);

                    SearchInfo.bDontSearchSubs = !IsDlgButtonChecked(hDlg, IDD_SEARCHALL);
                    SearchInfo.bIncludeSubDirs = IsDlgButtonChecked(hDlg, IDD_INCLUDEDIRS);

                    EndDialog(hDlg, TRUE);

                    SearchInfo.iDirsRead = 0;
                    SearchInfo.iFileCount = 0;
                    SearchInfo.eStatus = _SEARCH_INFO::SEARCH_NULL;
                    SearchInfo.bCancel = FALSE;

                    // Retrieve state of search window
                    BOOL bMaximized = FALSE;
                    HWND hwndMDIChild = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, (LPARAM)&bMaximized);

                    /* Is the search window already up? */
                    if (hwndSearch == NULL) {
                        //
                        // !! BUGBUG !!
                        //
                        // This is safe since szMessage = MAXPATHLEN*2+MAXSUGGESTLEN
                        // but it's not portable
                        //
                        LoadString(hAppInstance, IDS_SEARCHTITLE, szMessage, COUNTOF(szMessage));
                        lstrcat(szMessage, SearchInfo.szSearch);

                        // Create max or normal based on current mdi child
                        DWORD style = bMaximized ? WS_MAXIMIZE : WS_OVERLAPPED;

                        hwndSearch = CreateMDIWindow(
                            kSearchClass, szMessage, style, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, hwndMDIClient,
                            hAppInstance, 0);
                    }

                    SendMessage(hwndSearch, FS_CHANGEDISPLAY, CD_PATH, (LPARAM)SearchInfo.szSearch);

                    // Show search window immediatley
                    ShowWindow(hwndSearch, bMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
                    SetWindowPos(hwndSearch, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

                    break;
                }

                case IDD_BROWSE: {
                    IFileOpenDialog* pfd = NULL;
                    if (SUCCEEDED(
                            CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
                        DWORD dwOptions;
                        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
                            pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
                        }
                        pfd->SetTitle(L"Select a folder to search");
                        if (SUCCEEDED(pfd->Show(hDlg))) {
                            IShellItem* psi = NULL;
                            if (SUCCEEDED(pfd->GetResult(&psi))) {
                                LPWSTR pszPath = NULL;
                                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                                    SetDlgItemText(hDlg, IDD_DIR, pszPath);
                                    CoTaskMemFree(pszPath);
                                }
                                psi->Release();
                            }
                        }
                        pfd->Release();
                    }
                    break;
                }

                default:
                    return FALSE;
            }
            break;

        default:
            if (wMsg == wHelpMessage) {
            DoHelp:
                return TRUE;
            } else
                return FALSE;
    }
    return TRUE;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  RunDlgProc() -                                                          */
/*                                                                          */
/*--------------------------------------------------------------------------*/

INT_PTR
CALLBACK
RunDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    LPWSTR p, pDir, pFile, pPar;
    DWORD ret;
    LPWSTR pDir2;
    WCHAR szTemp[MAXPATHLEN];
    WCHAR szTemp2[MAXPATHLEN];
    WCHAR sz3[MAXPATHLEN];

    switch (wMsg) {
        case WM_INITDIALOG:
            SetDlgDirectory(hDlg, NULL);
            SetWindowDirectory();  // and really set the DOS current directory

            SendDlgItemMessage(hDlg, IDD_NAME, EM_LIMITTEXT, COUNTOF(szTemp) - 1, 0L);

            p = GetSelection(1, NULL);

            if (p) {
                SetDlgItemText(hDlg, IDD_NAME, p);
                LocalFree((HANDLE)p);
            }
            break;

        case WM_SIZE:
            SetDlgDirectory(hDlg, NULL);
            break;

        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam)) {
                case IDD_HELP:
                    goto DoHelp;

                case IDCANCEL:
                    EndDialog(hDlg, FALSE);
                    break;

                case IDOK: {
                    BOOL bLoadIt, bRunAs;

                    GetDlgItemText(hDlg, IDD_NAME, szTemp, COUNTOF(szTemp));
                    GetPathInfo(szTemp, &pDir, &pFile, &pPar);

                    // copy away parameters
                    lstrcpy(sz3, pPar);
                    *pPar = CHAR_NULL;  // strip the params from the program

                    // REVIEW HACK Hard code UNC style paths.
                    if (*pDir == CHAR_BACKSLASH && *(pDir + 1) == CHAR_BACKSLASH) {
                        // This is a UNC style filename so NULLify directory.
                        pDir2 = NULL;
                    } else {
                        GetSelectedDirectory(0, szTemp2);
                        pDir2 = szTemp2;
                    }

                    bLoadIt = IsDlgButtonChecked(hDlg, IDD_LOAD);
                    bRunAs = IsDlgButtonChecked(hDlg, IDD_RUNAS);

                    // Stop SaveBits flickering by invalidating the SaveBitsStuff.
                    // You can't just hide the window because it messes up the
                    // the activation.

                    SetWindowPos(
                        hDlg, 0, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

                    ret = ExecProgram(pDir, sz3, pDir2, bLoadIt, bRunAs);
                    if (ret) {
                        MyMessageBox(hDlg, IDS_EXECERRTITLE, ret, MB_OK | MB_ICONEXCLAMATION | MB_SYSTEMMODAL);
                        SetWindowPos(
                            hDlg, 0, 0, 0, 0, 0,
                            SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
                    } else
                        EndDialog(hDlg, TRUE);
                    break;
                }

                default:
                    return (FALSE);
            }
            break;

        default:
            if (wMsg == wHelpMessage || wMsg == wBrowseMessage) {
            DoHelp:
                return TRUE;
            } else
                return FALSE;
    }
    return TRUE;
}

void EnableCopy(HWND hDlg, BOOL bCopy) {
    HWND hwnd;

    // turn these off

    hwnd = GetDlgItem(hDlg, IDD_STATUS);
    if (hwnd) {
        EnableWindow(hwnd, !bCopy);
        ShowWindow(hwnd, !bCopy ? SW_SHOWNA : SW_HIDE);
    }

    hwnd = GetDlgItem(hDlg, IDD_NAME);
    if (hwnd) {
        EnableWindow(hwnd, !bCopy);
        ShowWindow(hwnd, !bCopy ? SW_SHOWNA : SW_HIDE);
    }
}

void MessWithRenameDirPath(LPWSTR pszPath) {
    WCHAR szPath[MAXPATHLEN];
    LPWSTR lpsz;

    // absolute path? don't tamper with it!

    // Also allow "\"f:\joe\me\""  ( ->   "f:\joe\me  )

    //
    // !! LATER !!
    //
    // Should we allow backslashes here also ?
    // CheckSlashes(pszPath); or add || clause.
    //

    lpsz = (CHAR_DQUOTE == pszPath[0]) ? pszPath + 1 : pszPath;

    if (CHAR_COLON == lpsz[1] && CHAR_BACKSLASH == lpsz[2] || lstrlen(pszPath) > (COUNTOF(szPath) - 4))

        return;

    //
    // prepend "..\" to this non absolute path
    //
    lstrcpy(szPath, L"..\\");
    lstrcat(szPath, pszPath);
    lstrcpy(pszPath, szPath);
}

//--------------------------------------------------------------------------*/
//                                                                          */
//  SuperDlgProc() -                                                        */
//                                                                          */
//--------------------------------------------------------------------------*/

// This proc handles the Print, Move, Copy, Delete, and Rename functions.
// The calling routine (AppCommandProc()) sets 'dwSuperDlgMode' before
// calling DialogBox() to indicate which function is being used.

INT_PTR
CALLBACK
SuperDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    UINT len;
    int iCtrl;
    LPWSTR pszFrom;
    //
    // WFMoveCopyDrive tries to append \*.* to directories and
    // probably other nasty stuff.  2* for safety.
    //
    WCHAR szTo[2 * MAXPATHLEN];
    static BOOL bTreeHasFocus;

    WCHAR szStr[256];

    static PCOPYINFO pCopyInfo;

    switch (wMsg) {
        case WM_INITDIALOG: {
            LPWSTR p;
            HWND hwndActive;

            pCopyInfo = NULL;

            SetDlgDirectory(hDlg, NULL);

            EnableCopy(hDlg, dwSuperDlgMode == IDM_COPY);

            hwndActive = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L);
            bTreeHasFocus = (hwndActive != hwndSearch) && (GetTreeFocus(hwndActive) == HasTreeWindow(hwndActive));

            switch (dwSuperDlgMode) {
                case IDM_COPY:

                    p = GetSelection(0, NULL);

                    LoadString(hAppInstance, IDS_COPY, szTitle, COUNTOF(szTitle));
                    SetWindowText(hDlg, szTitle);

                    LoadString(hAppInstance, IDS_KK_COPYFROMSTR, szStr, COUNTOF(szStr));
                    SetDlgItemText(hDlg, IDD_KK_TEXTFROM, szStr);
                    LoadString(hAppInstance, IDS_KK_COPYTOSTR, szStr, COUNTOF(szStr));
                    SetDlgItemText(hDlg, IDD_KK_TEXTTO, szStr);

                    break;
                case IDM_HARDLINK:

                    p = GetSelection(0, NULL);

                    LoadString(hAppInstance, IDS_HARDLINK, szTitle, COUNTOF(szTitle));
                    SetWindowText(hDlg, szTitle);

                    LoadString(hAppInstance, IDS_KK_HARDLINKFROMSTR, szStr, COUNTOF(szStr));
                    SetDlgItemText(hDlg, IDD_KK_TEXTFROM, szStr);
                    LoadString(hAppInstance, IDS_KK_HARDLINKTOSTR, szStr, COUNTOF(szStr));
                    SetDlgItemText(hDlg, IDD_KK_TEXTTO, szStr);

                    break;
                case IDM_SYMLINK:

                    p = GetSelection(0, NULL);

                    LoadString(hAppInstance, IDS_SYMLINK, szTitle, COUNTOF(szTitle));
                    SetWindowText(hDlg, szTitle);

                    LoadString(hAppInstance, IDS_KK_SYMLINKFROMSTR, szStr, COUNTOF(szStr));
                    SetDlgItemText(hDlg, IDD_KK_TEXTFROM, szStr);
                    LoadString(hAppInstance, IDS_KK_SYMLINKTOSTR, szStr, COUNTOF(szStr));
                    SetDlgItemText(hDlg, IDD_KK_TEXTTO, szStr);

                    break;
                case IDM_RENAME:

                    LoadString(hAppInstance, IDS_RENAME, szTitle, COUNTOF(szTitle));
                    SetWindowText(hDlg, szTitle);

                    LoadString(hAppInstance, IDS_KK_RENAMEFROMSTR, szStr, COUNTOF(szStr));
                    SetDlgItemText(hDlg, IDD_KK_TEXTFROM, szStr);
                    LoadString(hAppInstance, IDS_KK_RENAMETOSTR, szStr, COUNTOF(szStr));
                    SetDlgItemText(hDlg, IDD_KK_TEXTTO, szStr);

                    // when renaming the current directory we cd up a level
                    // (not really) and apply the appropriate adjustments

                    if (bTreeHasFocus) {
                        p = GetSelection(16, NULL);
                        lstrcpy(szTo, p);
                        StripFilespec(szTo);

                        SetDlgDirectory(hDlg, szTo);  // make the user think this!
                        StripPath(p);                 // name part of dir

                        CheckEsc(p);
                    } else {
                        p = GetSelection(8, NULL);
                    }

                    break;

                default:

                    p = GetSelection(0, NULL);
            }

            SetDlgItemText(hDlg, IDD_FROM, p);

            if (dwSuperDlgMode == IDM_DELETE)
                iCtrl = IDD_FROM;
            else {
                WCHAR szDirs[MAXPATHLEN];
                LPWSTR rgszDirs[MAX_DRIVES];
                int drive, driveCur;
                BOOL fFirst = TRUE;

                iCtrl = IDD_TO;
                if (dwSuperDlgMode == IDM_RENAME)
                    SetDlgItemText(hDlg, IDD_TO, p);

                driveCur = (int)GetWindowLongPtr(hwndActive, GWL_TYPE);

                LoadString(hAppInstance, IDS_CURDIRSARE, szDirs, COUNTOF(szDirs));

                GetAllDirectories(rgszDirs);

                for (drive = 0; drive < MAX_DRIVES; drive++) {
                    if (drive != driveCur && rgszDirs[drive] != NULL) {
                        if (!fFirst) {
                            wcsncat_s(szDirs, MAXPATHLEN, L";", 1);
                        }
                        fFirst = FALSE;

                        // NOTE: this call may truncate the result that goes in szDirs,
                        // but due to the limited width of the dialog, we can't see it all anyway.
                        wcsncat_s(szDirs, MAXPATHLEN, rgszDirs[drive], _TRUNCATE);

                        LocalFree(rgszDirs[drive]);
                    }
                }

                SetDlgItemText(hDlg, IDD_DIRS, szDirs);
            }

            SendDlgItemMessage(hDlg, iCtrl, EM_LIMITTEXT, COUNTOF(szTo) - 1, 0L);
            LocalFree((HANDLE)p);

            if (dwSuperDlgMode == IDM_RENAME) {
                SetFocus(GetDlgItem(hDlg, IDD_TO));
                return FALSE;
            }
            break;
        }

        case WM_SIZE: {
            SetDlgDirectory(hDlg, NULL);
            break;
        }

        case WM_NCACTIVATE:
            if (IDM_RENAME == dwSuperDlgMode) {
                size_t ich1, ich2;
                LPWSTR pchDot;

                GetDlgItemText(hDlg, IDD_TO, szTo, COUNTOF(szTo));
                ich1 = 0;
                ich2 = wcslen(szTo);

                // Search for extension
                pchDot = wcsrchr(szTo, '.');
                if (pchDot != NULL) {
                    WCHAR szTemp[MAXPATHLEN];
                    lstrcpy(szTemp, szTo);
                    QualifyPath(szTemp);

                    // Is this a file or directory
                    if (GetFileAttributes(szTemp) & FILE_ATTRIBUTE_DIRECTORY) {
                        if (szTo[ich2 - 1] == '\"')
                            ich2--;
                    } else {
                        ich2 = pchDot - szTo;
                    }
                }
                // Make sure we handle " properly with selection
                if (*szTo == '\"') {
                    ich1 = 1;
                    if (pchDot == NULL)
                        ich2--;
                }
                SendDlgItemMessage(hDlg, IDD_TO, EM_SETSEL, ich1, ich2);
            }
            return FALSE;

        case FS_COPYDONE:

            //
            // Only cancel out if pCopyInfo == lParam
            // This indicates that the proper thread quit.
            //
            // wParam holds return value
            //

            if (lParam == (LPARAM)pCopyInfo) {
                SPC_SET_HITDISK(qFreeSpace);  // force status info refresh

                EndDialog(hDlg, wParam);
            }
            break;

        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam)) {
                case IDD_HELP:
                    goto DoHelp;

                case IDCANCEL:

                    if (pCopyInfo)
                        pCopyInfo->bUserAbort = TRUE;

                SuperDlgExit:

                    EndDialog(hDlg, 0);
                    break;

                case IDOK:

                    len = (UINT)(SendDlgItemMessage(hDlg, IDD_FROM, EM_LINELENGTH, (WPARAM)-1, 0L) + 1);

                    //
                    // make sure the pszFrom buffer is big enough to
                    // add the "..\" stuff in MessWithRenameDirPath()
                    //
                    len += 4;

                    pszFrom = (LPWSTR)LocalAlloc(LPTR, ByteCountOf(len));
                    if (!pszFrom)
                        goto SuperDlgExit;

                    GetDlgItemText(hDlg, IDD_FROM, pszFrom, len);
                    GetDlgItemText(hDlg, IDD_TO, szTo, COUNTOF(szTo));

                    //
                    // if dwSuperDlgMode is copy, rename, symlink, hardlink, or move, do checkesc.
                    // Only if no quotes in string!
                    //
                    switch (dwSuperDlgMode) {
                        case IDM_RENAME:
                        case IDM_MOVE:
                        case IDM_COPY:
                        case IDM_SYMLINK:
                        case IDM_HARDLINK:
                            if (NoQuotes(szTo))
                                CheckEsc(szTo);
                    }

                    if (!szTo[0]) {
                        switch (dwSuperDlgMode) {
                            case IDM_RENAME:
                            case IDM_MOVE:
                            case IDM_COPY:
                            case IDM_SYMLINK:
                            case IDM_HARDLINK: {
                                szTo[0] = CHAR_DOT;
                                szTo[1] = CHAR_NULL;
                                break;
                            }
                        }
                    }

                    EnableCopy(hDlg, FALSE);

                    hdlgProgress = hDlg;
                    {
                        if (dwSuperDlgMode == IDM_RENAME && bTreeHasFocus) {
                            MessWithRenameDirPath(pszFrom);
                            MessWithRenameDirPath(szTo);
                        }

                        //
                        // Setup pCopyInfo structure
                        //
                        // Note that everything must be malloc'd!!
                        // (Freed by thread)
                        //

                        pCopyInfo = (PCOPYINFO)LocalAlloc(LPTR, sizeof(COPYINFO));

                        if (!pCopyInfo) {
                        Error:
                            FormatError(TRUE, szMessage, COUNTOF(szMessage), GetLastError());
                            LoadString(hAppInstance, IDS_WINFILE, szTitle, COUNTOF(szTitle));

                            MessageBox(hwndFrame, szMessage, szTitle, MB_OK | MB_ICONEXCLAMATION);

                            LocalFree(pszFrom);
                            goto SuperDlgExit;
                        }

                        pCopyInfo->pFrom = pszFrom;
                        pCopyInfo->pTo = (LPWSTR)LocalAlloc(LMEM_FIXED, sizeof(szTo));

                        if (!pCopyInfo->pTo) {
                            goto Error;
                        }

                        // Map IDM_* to FUNC_*
                        switch (dwSuperDlgMode) {
                            case IDM_MOVE:
                                pCopyInfo->dwFunc = FUNC_MOVE;
                                break;
                            case IDM_COPY:
                                pCopyInfo->dwFunc = FUNC_COPY;
                                break;
                            case IDM_DELETE:
                                pCopyInfo->dwFunc = FUNC_DELETE;
                                break;
                            case IDM_RENAME:
                                pCopyInfo->dwFunc = FUNC_RENAME;
                                break;
                            case IDM_SYMLINK:
                                pCopyInfo->dwFunc = FUNC_LINK;
                                break;
                            case IDM_HARDLINK:
                                pCopyInfo->dwFunc = FUNC_HARD;
                                break;
                        }

                        pCopyInfo->bUserAbort = FALSE;

                        lstrcpy(pCopyInfo->pTo, szTo);

                        //
                        // Move/Copy things.
                        //
                        if (WFMoveCopyDriver(pCopyInfo)) {
                            LoadString(hAppInstance, IDS_COPYERROR + pCopyInfo->dwFunc, szTitle, COUNTOF(szTitle));

                            FormatError(TRUE, szMessage, COUNTOF(szMessage), GetLastError());

                            MessageBox(hDlg, szMessage, szTitle, MB_ICONSTOP | MB_OK);

                            EndDialog(hDlg, GetLastError());

                        } else {
                            //
                            // Disable all but the cancel button on the notify dialog
                            //
                            DialogEnterFileStuff(hdlgProgress);
                        }
                    }
                    break;

                default:
                    return (FALSE);
            }
            break;

        default:
            if (wMsg == wHelpMessage) {
            DoHelp:
                return TRUE;
            } else {
                return FALSE;
            }
    }
    return TRUE;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  MakeDirDlgProc() -                                                      */
/*                                                                          */
/*--------------------------------------------------------------------------*/

INT_PTR
CALLBACK
MakeDirDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    //
    // Must be at least MAXPATHLEN
    //
    WCHAR szPath[MAXPATHLEN * 2];
    int ret;

    switch (wMsg) {
        case WM_INITDIALOG:
            SetDlgDirectory(hDlg, NULL);
            SendDlgItemMessage(hDlg, IDD_NAME, EM_LIMITTEXT, MAXPATHLEN - 1, 0L);
            break;

        case WM_SIZE:
            SetDlgDirectory(hDlg, NULL);
            break;

        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam)) {
                case IDD_HELP:
                    goto DoHelp;

                case IDCANCEL:
                    EndDialog(hDlg, FALSE);
                    break;

                case IDOK:

                    GetDlgItemText(hDlg, IDD_NAME, szPath, MAXPATHLEN);
                    EndDialog(hDlg, TRUE);

                    //
                    // If "a b" typed in (no quotes, just a b) do we create two
                    // directors, "a" and "b," or create just one: "a b."
                    // For now, create just one.  (No need to call checkesc!)
                    //
                    // put it back in to handle quoted things.
                    // Now, it _ignores_ extra files on the line.  We may wish to return
                    // an error; that would be smart...
                    //
                    if (NoQuotes(szPath)) {
                        CheckEsc(szPath);
                    }

                    GetNextFile(szPath, szPath, COUNTOF(szPath));

                    QualifyPath(szPath);

                    hdlgProgress = hDlg;

                    SendMessage(hwndFrame, FS_DISABLEFSC, 0, 0L);

                    ret = WF_CreateDirectory(hDlg, szPath, NULL);

                    if (ret && ret != DE_OPCANCELLED) {
                        // Handle error messages cleanly.
                        // Special case ERROR_ALREADY_EXISTS

                        if (ERROR_ALREADY_EXISTS == ret) {
                            ret = WFIsDir(szPath) ? DE_MAKEDIREXISTS : DE_DIREXISTSASFILE;
                        }

                        LoadString(hAppInstance, IDS_MAKEDIRERR, szMessage, COUNTOF(szMessage));
                        FormatError(FALSE, szMessage, COUNTOF(szMessage), ret);

                        GetWindowText(hDlg, szTitle, COUNTOF(szTitle));
                        MessageBox(hwndFrame, szMessage, szTitle, MB_OK | MB_ICONSTOP);
                    }

                    SendMessage(hwndFrame, FS_ENABLEFSC, 0, 0L);

                    break;

                default:
                    return FALSE;
            }
            break;

        default:

            if (wMsg == wHelpMessage) {
            DoHelp:
                return TRUE;
            } else
                return FALSE;
    }
    return TRUE;
}

// Check if szT has quote in it.
// could use strchr...

BOOL NoQuotes(LPWSTR szT) {
    while (*szT) {
        if (CHAR_DQUOTE == *szT)
            return FALSE;

        szT++;
    }

    return TRUE;
}
