/********************************************************************

   wfdlgs.c

   Windows File System Dialog procedures

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#include "winfile.h"
#include <commdlg.h>
#include <dlgs.h>
#include "lfn.h"
#include "wfcopy.h"
#include "resize.h"
#include "gitbash.h"
#include "wfutil.h"
#include "wfdir.h"
#include "stringconstants.h"

void MDIClientSizeChange(HWND hwndActive, int iFlags);

extern int maxExt;

void SaveWindows(HWND hwndMain) {
    // 2* added to both lines
    WCHAR szPath[2 * MAXPATHLEN];
    WCHAR buf2[2 * MAXPATHLEN + 6 * 12];

    WCHAR key[10];
    int dir_num;
    HWND hwnd;
    BOOL bCounting;
    RECT rcT;
    DWORD view, sort;
    WINDOWPLACEMENT wp;

    // save main window position

    wp.length = sizeof(WINDOWPLACEMENT);
    if (!GetWindowPlacement(hwndMain, &wp))
        return;

    SystemParametersInfo(SPI_GETWORKAREA, 0, (PVOID)&rcT, 0);

    // WINDOWPLACEMENT coordinates for top-level windows are in Workspace coordinates;
    // we tranlate this into screen coordinates prior to saving;
    // also, the values saved for the third and fourth values are width and height.
    wsprintf(
        buf2, L"%ld,%ld,%ld,%ld, , ,%u", rcT.left + wp.rcNormalPosition.left, rcT.top + wp.rcNormalPosition.top,
        wp.rcNormalPosition.right - wp.rcNormalPosition.left, wp.rcNormalPosition.bottom - wp.rcNormalPosition.top,
        wp.showCmd);

    WritePrivateProfileString(kSettings, kWindow, buf2, szTheINIFile);

    WritePrivateProfileBool(kScrollOnExpand, bScrollOnExpand);

    // write out dir window strings in reverse order
    // so that when we read them back in we get the same Z order

    bCounting = TRUE;
    dir_num = 0;

DO_AGAIN:

    for (hwnd = GetWindow(hwndMDIClient, GW_CHILD); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
        HWND ht = HasTreeWindow(hwnd);
        int nReadLevel = ht ? (int)GetWindowLongPtr(ht, GWL_READLEVEL) : 0;

        // don't save MDI icon title windows or search windows,
        // or any dir window which is currently recursing

        if ((GetWindow(hwnd, GW_OWNER) == NULL) &&
            GetWindowLongPtr(hwnd, GWL_TYPE) != TYPE_SEARCH) /* nReadLevel == 0) */ {
            if (bCounting) {
                dir_num++;
                continue;
            }

            wp.length = sizeof(WINDOWPLACEMENT);
            if (!GetWindowPlacement(hwnd, &wp))
                continue;
            view = (DWORD)GetWindowLongPtr(hwnd, GWL_VIEW);
            sort = (DWORD)GetWindowLongPtr(hwnd, GWL_SORT);

            GetMDIWindowText(hwnd, szPath, COUNTOF(szPath));

            wsprintf(key, kDirKeyFormat, dir_num--);

            // format:
            //   x_win, y_win,
            //   x_win, y_win,
            //   x_icon, y_icon,
            //   show_window, view, sort, split, directory

            // NOTE: MDI child windows are in child coordinats; no translation is done.
            wsprintf(
                buf2, L"%ld,%ld,%ld,%ld,%ld,%ld,%u,%lu,%lu,%d,%s", wp.rcNormalPosition.left, wp.rcNormalPosition.top,
                wp.rcNormalPosition.right, wp.rcNormalPosition.bottom, wp.ptMinPosition.x, wp.ptMinPosition.y,
                wp.showCmd, view, sort, GetSplit(hwnd), szPath);

            // the dir is an ANSI string (?)

            WritePrivateProfileString(kSettings, key, buf2, szTheINIFile);
        }
    }

    if (bCounting) {
        bCounting = FALSE;

        // erase the last dir window so that if they save with
        // fewer dirs open we don't pull in old open windows

        wsprintf(key, kDirKeyFormat, dir_num + 1);
        WritePrivateProfileString(kSettings, key, NULL, szTheINIFile);

        goto DO_AGAIN;
    }
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  OtherDlgProc() -                                                        */
/*                                                                          */
/*--------------------------------------------------------------------------*/

INT_PTR
CALLBACK
OtherDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    DWORD dwView;
    HWND hwndActive;

    UNREFERENCED_PARAMETER(lParam);

    hwndActive = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L);

    switch (wMsg) {
        case WM_INITDIALOG:

            dwView = (DWORD)GetWindowLongPtr(hwndActive, GWL_VIEW);
            CheckDlgButton(hDlg, IDD_SIZE, dwView & VIEW_SIZE);
            CheckDlgButton(hDlg, IDD_DATE, dwView & VIEW_DATE);
            CheckDlgButton(hDlg, IDD_TIME, dwView & VIEW_TIME);
            CheckDlgButton(hDlg, IDD_FLAGS, dwView & VIEW_FLAGS);

            CheckDlgButton(hDlg, IDD_DOSNAMES, dwView & VIEW_DOSNAMES);

            break;

        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam)) {
                case IDD_HELP:
                    goto DoHelp;

                case IDCANCEL:
                    EndDialog(hDlg, FALSE);
                    break;

                case IDOK: {
                    HWND hwnd;

                    dwView = 0;  // VIEW_PLUSES is no longer supported

                    if (IsDlgButtonChecked(hDlg, IDD_SIZE))
                        dwView |= VIEW_SIZE;
                    if (IsDlgButtonChecked(hDlg, IDD_DATE))
                        dwView |= VIEW_DATE;
                    if (IsDlgButtonChecked(hDlg, IDD_TIME))
                        dwView |= VIEW_TIME;
                    if (IsDlgButtonChecked(hDlg, IDD_FLAGS))
                        dwView |= VIEW_FLAGS;

                    if (IsDlgButtonChecked(hDlg, IDD_DOSNAMES))
                        dwView |= VIEW_DOSNAMES;

                    EndDialog(hDlg, TRUE);

                    if (hwnd = HasDirWindow(hwndActive))
                        SendMessage(hwnd, FS_CHANGEDISPLAY, CD_VIEW, dwView);
                    else if (hwndActive == hwndSearch) {
                        SetWindowLongPtr(hwndActive, GWL_VIEW, dwView);

                        SendMessage(hwndActive, FS_CHANGEDISPLAY, CD_VIEW, 0L);
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

INT_PTR
CALLBACK
SelectDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    HWND hwndActive, hwnd;
    WCHAR szList[128];
    WCHAR szSpec[MAXFILENAMELEN];
    LPWSTR p;

    if (ResizeDialogProc(hDlg, wMsg, wParam, lParam)) {
        return TRUE;
    }

    UNREFERENCED_PARAMETER(lParam);

    switch (wMsg) {
        case WM_INITDIALOG:
            SendDlgItemMessage(hDlg, IDD_NAME, EM_LIMITTEXT, COUNTOF(szList) - 1, 0L);
            SetDlgItemText(hDlg, IDD_NAME, kStarDotStar);
            break;

        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam)) {
                case IDD_HELP:
                    goto DoHelp;

                case IDCANCEL:
                    EndDialog(hDlg, FALSE);
                    break;

                case IDOK:   // select
                case IDYES:  // deselect

                    // change "Cancel" to "Close"

                    LoadString(hAppInstance, IDS_ANDCLOSE, szSpec, COUNTOF(szSpec));
                    SetDlgItemText(hDlg, IDCANCEL, szSpec);

                    hwndActive = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L);

                    if (!hwndActive)
                        break;

                    GetDlgItemText(hDlg, IDD_NAME, szList, COUNTOF(szList));

                    if (hwndActive == hwndSearch)
                        hwnd = hwndSearch;
                    else
                        hwnd = HasDirWindow(hwndActive);

                    if (hwnd) {
                        p = szList;

                        while (p = GetNextFile(p, szSpec, COUNTOF(szSpec)))
                            SendMessage(
                                hwnd, FS_SETSELECTION, (BOOL)(GET_WM_COMMAND_ID(wParam, lParam) == IDOK),
                                (LPARAM)szSpec);
                    }

                    if (hwnd != hwndSearch)
                        UpdateStatus(hwndActive);
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

void RepaintDrivesForFontChange(HWND hwndChild) {
    if (bDriveBar)
        MDIClientSizeChange(hwndChild, DRIVEBAR_FLAG);
}

void NewFont() {
    HFONT hOldFont;
    HANDLE hOld;

    HWND hwnd, hwndT, hwndT2;
    HDC hdc;
    LOGFONT lf;
    CHOOSEFONT cf;
    WCHAR szBuf[10];
    int res;
    UINT uOld, uNew;

#define MAX_PT_SIZE 36

    GetObject(hFont, sizeof(lf), (LPVOID)(LPLOGFONT)&lf);

    //
    // As we use 'system' font as default, and set initial size 0 for logfont so
    // that we can get default size on system, we may haven't got real font
    // height yet. mskk.
    //
    uOld = (UINT)abs(lf.lfHeight);

    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hwndFrame;
    cf.lpLogFont = &lf;
    cf.hInstance = hAppInstance;
    cf.nSizeMin = 4;
    cf.nSizeMax = 36;

    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_LIMITSIZE | CF_ANSIONLY;

    res = ChooseFontW(&cf);

    if (!res)
        return;

    wsprintf(szBuf, SZ_PERCENTD, cf.iPointSize / 10);

    uNew = (UINT)abs(lf.lfHeight);

    // Set wTextAttribs BOLD and ITALIC flags

    if (lf.lfItalic != 0)
        wTextAttribs |= TA_ITALIC;
    else
        wTextAttribs &= ~TA_ITALIC;

    WritePrivateProfileString(kSettings, kFace, lf.lfFaceName, szTheINIFile);
    WritePrivateProfileString(kSettings, kSize, szBuf, szTheINIFile);
    WritePrivateProfileBool(kLowerCase, wTextAttribs);
    WritePrivateProfileBool(kFaceWeight, lf.lfWeight);

    hOldFont = hFont;

    hFont = CreateFontIndirect(&lf);

    if (!hFont) {
        DeleteObject(hOldFont);
        return;
    }

    // recalc all the metrics for the new font

    hdc = GetDC(NULL);
    hOld = SelectObject(hdc, hFont);
    GetTextStuff(hdc);
    if (hOld)
        SelectObject(hdc, hOld);
    ReleaseDC(NULL, hdc);

    RepaintDrivesForFontChange((HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L));

    // now update all listboxes that are using the old
    // font with the new font

    for (hwnd = GetWindow(hwndMDIClient, GW_CHILD); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
        if (GetWindow(hwnd, GW_OWNER))
            continue;

        if ((int)GetWindowLongPtr(hwnd, GWL_TYPE) == TYPE_SEARCH) {
            SendMessage((HWND)GetDlgItem(hwnd, IDCW_LISTBOX), WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
            SendMessage((HWND)GetDlgItem(hwnd, IDCW_LISTBOX), LB_SETITEMHEIGHT, 0, (LONG)dyFileName);

            // SearchWin font ext
            // in case font changed, update maxExt

            SendMessage(hwnd, FS_CHANGEDISPLAY, CD_SEARCHFONT, 0L);

        } else {
            if (hwndT = HasDirWindow(hwnd)) {
                hwndT2 = GetDlgItem(hwndT, IDCW_LISTBOX);
                SetLBFont(
                    hwndT, hwndT2, hFont, (DWORD)GetWindowLongPtr(hwnd, GWL_VIEW),
                    (LPXDTALINK)GetWindowLongPtr(hwndT, GWL_HDTA));

                InvalidateRect(hwndT2, NULL, TRUE);
            }

            if (hwndT = HasTreeWindow(hwnd)) {
                // the tree list box

                hwndT = GetDlgItem(hwndT, IDCW_TREELISTBOX);

                SendMessage(hwndT, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
                SendMessage(hwndT, LB_SETITEMHEIGHT, 0, (LONG)dyFileName);

                /*
                 *  Force the recalculation of GWL_XTREEMAX (max text extent).
                 */
                SendMessage(HasTreeWindow(hwnd), TC_RECALC_EXTENT, (WPARAM)hwndT, 0L);
            }
        }
    }
    DeleteObject(hOldFont);  // done with this now, delete it
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ConfirmDlgProc() -                                                      */
/*                                                                          */
/*--------------------------------------------------------------------------*/

INT_PTR
CALLBACK
ConfirmDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);

    switch (wMsg) {
        case WM_INITDIALOG:
            CheckDlgButton(hDlg, IDD_DELETE, bConfirmDelete);
            CheckDlgButton(hDlg, IDD_SUBDEL, bConfirmSubDel);
            CheckDlgButton(hDlg, IDD_REPLACE, bConfirmReplace);
            CheckDlgButton(hDlg, IDD_MOUSE, bConfirmMouse);
            CheckDlgButton(hDlg, IDD_CONFIG, bConfirmFormat);
            CheckDlgButton(hDlg, IDD_READONLY, bConfirmReadOnly);
            break;

        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam)) {
                case IDD_HELP:
                    goto DoHelp;

                case IDCANCEL:
                    EndDialog(hDlg, FALSE);
                    break;

                case IDOK:
                    bConfirmDelete = IsDlgButtonChecked(hDlg, IDD_DELETE);
                    bConfirmSubDel = IsDlgButtonChecked(hDlg, IDD_SUBDEL);
                    bConfirmReplace = IsDlgButtonChecked(hDlg, IDD_REPLACE);
                    bConfirmMouse = IsDlgButtonChecked(hDlg, IDD_MOUSE);
                    bConfirmFormat = IsDlgButtonChecked(hDlg, IDD_CONFIG);

                    bConfirmReadOnly = IsDlgButtonChecked(hDlg, IDD_READONLY);

                    WritePrivateProfileBool(kConfirmDelete, bConfirmDelete);
                    WritePrivateProfileBool(kConfirmSubDel, bConfirmSubDel);
                    WritePrivateProfileBool(kConfirmReplace, bConfirmReplace);
                    WritePrivateProfileBool(kConfirmMouse, bConfirmMouse);
                    WritePrivateProfileBool(kConfirmFormat, bConfirmFormat);

                    WritePrivateProfileBool(kConfirmReadOnly, bConfirmReadOnly);

                    EndDialog(hDlg, TRUE);
                    break;

                default:
                    return (FALSE);
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

INT_PTR CALLBACK PrefDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    /* Editor prefrence variables*/
    WCHAR szTempEditPath[MAXPATHLEN];
    WCHAR szPath[MAXPATHLEN];
    WCHAR szFilter[MAXPATHLEN] = { 0 };

    LoadString(hAppInstance, IDS_EDITFILTER, szFilter, MAXPATHLEN);

    OPENFILENAME ofn;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hDlg;
    ofn.lpstrFile = szPath;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = sizeof(szPath);
    ofn.lpstrFilter = szFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    switch (wMsg) {
        case WM_INITDIALOG:
            GetPrivateProfileString(kSettings, kEditorPath, NULL, szTempEditPath, MAXPATHLEN, szTheINIFile);
            SetDlgItemText(hDlg, IDD_EDITOR, szTempEditPath);

            break;

        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam)) {
                case IDD_HELP:
                    goto DoHelp;

                case IDC_EDITOR:
                    if (GetOpenFileNameW(&ofn)) {
                        wcscpy_s(szPath, MAXPATHLEN, ofn.lpstrFile);
                        SetDlgItemText(hDlg, IDD_EDITOR, szPath);
                    }
                    break;

                case IDOK:
                    GetDlgItemText(hDlg, IDD_EDITOR, szTempEditPath, MAXPATHLEN);
                    WritePrivateProfileString(kSettings, kEditorPath, szTempEditPath, szTheINIFile);

                    EndDialog(hDlg, TRUE);
                    break;

                case IDCANCEL:
                    EndDialog(hDlg, FALSE);
                    break;
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

void KillQuoteTrailSpace(LPWSTR szFile) {
    LPWSTR pc;
    LPWSTR pcNext;
    LPWSTR pcLastSpace = NULL;

    // Could reuse szFile, but that's ok,
    // we use it as a register probably anyway.

    pc = pcNext = szFile;

    while (*pcNext) {
        if (CHAR_DQUOTE == *pcNext) {
            pcNext++;
            continue;
        }

        if (CHAR_SPACE == *pcNext) {
            if (!pcLastSpace) {
                pcLastSpace = pc;
            }
        } else {
            pcLastSpace = NULL;
        }

        *pc++ = *pcNext++;
    }

    // Delimit!
    *pc = CHAR_NULL;

    // Now axe trailing spaces;
    if (pcLastSpace)
        *pcLastSpace = CHAR_NULL;
}

void ActivateCommonContextMenu(HWND hwnd, HWND hwndLB, LPARAM lParam) {
    DWORD cmd, item;
    POINT pt;

    HMENU hMenu = GetSubMenu(LoadMenu(hAppInstance, L"CTXMENU"), 0);

    // Enable or disable the Git Bash shell menu item based on Git Bash availability
    auto gitBashPath = GetGitBashPath();
    EnableMenuItem(hMenu, IDM_STARTBASHSHELL, MF_BYCOMMAND | (gitBashPath.has_value() ? MF_ENABLED : MF_GRAYED));

    if (lParam == -1) {
        RECT rect;

        item = (DWORD)SendMessage(hwndLB, LB_GETCURSEL, 0, 0);
        SendMessage(hwndLB, LB_GETITEMRECT, (WPARAM)LOWORD(item), (LPARAM)&rect);
        pt.x = rect.left;
        pt.y = rect.bottom;
        ClientToScreen(hwnd, &pt);
        lParam = POINTTOPOINTS(pt);
    } else {
        POINTSTOPOINT(pt, lParam);

        ScreenToClient(hwndLB, &pt);
        item = (DWORD)SendMessage(hwndLB, LB_ITEMFROMPOINT, 0, POINTTOPOINTS(pt));

        if (HIWORD(item) == 0) {
            HWND hwndTree, hwndParent;

            SetFocus(hwnd);

            hwndParent = GetParent(hwnd);
            hwndTree = HasTreeWindow(hwndParent);

            // if hwnd is the tree control within the parent window
            if (hwndTree == hwnd) {
                // tree control; do selection differently
                SendMessage(hwndLB, LB_SETCURSEL, (WPARAM)item, 0L);
                SendMessage(hwnd, WM_COMMAND, GET_WM_COMMAND_MPS(0, hwndLB, LBN_SELCHANGE));
            } else {
                // Check if the item under cursor is already selected
                BOOL isAlreadySelected = (BOOL)SendMessage(hwndLB, LB_GETSEL, (WPARAM)item, 0);

                // If item is not selected and no modifier keys are pressed, deselect all and select this item
                if (!isAlreadySelected && !(GetKeyState(VK_CONTROL) < 0) && !(GetKeyState(VK_SHIFT) < 0)) {
                    SendMessage(hwndLB, LB_SETSEL, (WPARAM)FALSE, (LPARAM)-1);
                    SendMessage(hwndLB, LB_SETSEL, (WPARAM)TRUE, (LPARAM)item);
                }
                // If item is not selected but Ctrl is pressed, add it to selection
                else if (!isAlreadySelected && (GetKeyState(VK_CONTROL) < 0)) {
                    SendMessage(hwndLB, LB_SETSEL, (WPARAM)TRUE, (LPARAM)item);
                }
                // Otherwise, preserve the existing selection

                BOOL bDir = FALSE;
                SendMessage(hwnd, FS_GETSELECTION, 5, (LPARAM)&bDir);
                if (bDir) {
                    EnableMenuItem(hMenu, IDM_EDIT, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
                }
            }
        }
    }

    // Enable or disable ZIP archive commands based on file selection
    // This must be done after the selection is updated above
    {
        BOOL bDir = FALSE;
        LPWSTR pSel = (LPWSTR)SendMessage(hwnd, FS_GETSELECTION, 5, (LPARAM)&bDir);

        BOOL bHasSelection = (pSel != NULL);
        BOOL bHasZipFilesOnly = FALSE;

        if (bHasSelection) {
            // Check if all selected files are ZIP files
            bHasZipFilesOnly = TRUE;
            LPWSTR pCurrent = pSel;
            WCHAR szFile[MAXPATHLEN];

            while (pCurrent = GetNextFile(pCurrent, szFile, COUNTOF(szFile))) {
                // Check if this file has a .zip extension
                LPWSTR pExt = wcsrchr(szFile, L'.');
                if (!pExt || _wcsicmp(pExt, L".zip") != 0) {
                    bHasZipFilesOnly = FALSE;
                    break;
                }
            }

            // If there are no files or if not all are ZIP files, disable ZIP-only commands
            if (!bHasZipFilesOnly) {
                bHasZipFilesOnly = FALSE;
            }
        }

        // Enable "Add to Zip" and "Add To..." if there are any selected files
        UINT uMenuFlags = bHasSelection ? MF_BYCOMMAND : MF_BYCOMMAND | MF_GRAYED;
        EnableMenuItem(hMenu, IDM_ZIPARCHIVE_ADDTOZIP, uMenuFlags);
        EnableMenuItem(hMenu, IDM_ZIPARCHIVE_ADDTO, uMenuFlags);

        // Enable extract commands only if all selected files are ZIP files
        uMenuFlags = bHasZipFilesOnly ? MF_BYCOMMAND : MF_BYCOMMAND | MF_GRAYED;
        EnableMenuItem(hMenu, IDM_ZIPARCHIVE_EXTRACTHERE, uMenuFlags);
        EnableMenuItem(hMenu, IDM_ZIPARCHIVE_EXTRACTTONEWFOLDER, uMenuFlags);
        EnableMenuItem(hMenu, IDM_ZIPARCHIVE_EXTRACTTO, uMenuFlags);
    }

    cmd = TrackPopupMenu(
        hMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0, hwnd,
        NULL);
    if (cmd != 0) {
        PostMessage(hwndFrame, WM_COMMAND, GET_WM_COMMAND_MPS(cmd, 0, 0));
    }

    DestroyMenu(hMenu);
}
