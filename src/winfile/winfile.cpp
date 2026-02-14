/********************************************************************

   winfile.c

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#define _GLOBALS
#include "winfile.h"
#include "lfn.h"
#include "wfdpi.h"  // Add DPI awareness header
#include "wfrecyclebin.h"
#include "wfchgnot.h"
#include "wfcomman.h"
#include "wfutil.h"
#include "wfdirrd.h"
#include "wfinit.h"
#include "wfsearch.h"
#include "stringconstants.h"
#include "bookmark.h"
#include "wfminbar.h"
#include "libheirloom/MdiDpiFixup.h"
#include <commctrl.h>
#include <shlobj.h>

//
// Overall Window structure
//
// Frame Window (FrameWndProc(), global hwndFrame)
// Drives bar (DrivesWndproc(), global hwndDriveBar)
// MDI Client (n/a, global hwndMDIClient)
//    Tree window (TreeWndProc(), <hwndActive> looked up)
//       Tree control on left (TreeControlWndProc(), hwndTree = HasTreeWindow(hwndActive))
//          Listbox (n/a, GetDlgItem(hwndTree, IDCW_TREELISTBOX))
//       Directory content list on right (DirWndProc(), hwndDir = HasDirWindow(hwndActive), GWL_LISTPARMS -> parent
//       hwndActive)
//          Listbox (n/a, GetDlgItem(hwndDir, IDCW_LISTBOX))
//    Search results window (SearchWndProc(), global hwndSearch, GWL_LISTPARMS -> hwndSearch)
//       Listbox (n/a, GetDlgItem(hwndDir, IDCW_LISTBOX))
// Status window (n/a, hwndStatus)
//

//
// prototypes
//
BOOL EnablePropertiesMenu(HWND hwnd, LPWSTR pszSel);
std::wstring EscapeMenuItemText(const std::wstring& text);

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR pszCmdLineA, int nCmdShow) {
    MSG msg;
    LPWSTR pszCmdLine;

    pszCmdLine = GetCommandLine();

    if (!InitFileManager(hInst, pszNextComponent(pszCmdLine), nCmdShow)) {
        FreeFileManager();
        return FALSE;
    }

    while (TRUE) {
        vWaitMessage();

        while (PeekMessage(&msg, (HWND)NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                FreeFileManager();

                return (int)msg.wParam;
            }

            //
            // since we use RETURN as an accelerator we have to manually
            // restore ourselves when we see VK_RETURN and we are minimized
            //

            if (msg.message == WM_SYSKEYDOWN && msg.wParam == VK_RETURN && IsIconic(hwndFrame)) {
                ShowWindow(hwndFrame, SW_NORMAL);

            } else if (
                !TranslateMDISysAccel(hwndMDIClient, &msg) &&
                (!hwndFrame || !TranslateAccelerator(hwndFrame, hAccel, &msg))) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }
}

void ResizeControls() {
    static int nViews[] = {
        1, 0,                // placeholder for the main menu handle
        1, IDC_STATUS, 0, 0  // signify the end of the list
    };

    RECT rc;
    int cDrivesPerRow;
    int dyDriveBar;

    //
    // These controls move and resize themselves
    //
    if (hwndStatus)
        SendMessage(hwndStatus, WM_SIZE, 0, 0L);

    //
    // This stuff is nec since bRepaint in MoveWindow seems
    // broken.  By invalidating, we don't scroll bad stuff.
    //
    if (bDriveBar) {
        InvalidateRect(hwndDriveBar, NULL, FALSE);
    }
    InvalidateRect(hwndMDIClient, NULL, FALSE);

    GetEffectiveClientRect(hwndFrame, &rc, nViews);
    rc.right -= rc.left;

    cDrivesPerRow = rc.right / dxDrive;
    if (!cDrivesPerRow)
        cDrivesPerRow++;

    dyDriveBar = dyDrive * ((cDrives + cDrivesPerRow - 1) / cDrivesPerRow) + 2 * dyBorder;

    rc.right += 2 * dyBorder;

    MoveWindow(hwndDriveBar, rc.left - dyBorder, rc.top - dyBorder, rc.right, dyDriveBar, FALSE);

    if (bDriveBar)
        rc.top += dyDriveBar - dyBorder;

    MoveWindow(
        hwndMDIClient, rc.left - dyBorder, rc.top - dyBorder, rc.right, rc.bottom - rc.top + 2 * dyBorder - 1, TRUE);
}

BOOL InitPopupMenu(const std::wstring& popupName, HMENU hMenu, HWND hwndActive) {
    DWORD dwSort;
    DWORD dwView;
    UINT uMenuFlags;
    HWND hwndTree, hwndDir;
    BOOL bLFN;

    hwndTree = HasTreeWindow(hwndActive);
    hwndDir = HasDirWindow(hwndActive);
    dwSort = (DWORD)GetWindowLongPtr(hwndActive, GWL_SORT);
    dwView = (DWORD)GetWindowLongPtr(hwndActive, GWL_VIEW);

    uMenuFlags = MF_BYCOMMAND | MF_ENABLED;

    bLFN = FALSE;  // For now, ignore the case.

    if (popupName == L"&File") {
        LPWSTR pSel = NULL;
        BOOL bDir = TRUE;
        IDataObject* pDataObj;

        //
        // In order to avoid deleting the tree control pNodes while
        // a read is in progress, the Move, Copy, Delete, Rename, and
        // and Create Directory menu items will be disabled.
        //
        // The reason this can happen is that the function ReadDirLevel
        // does a wfYield so that messages can be processed.  As a result,
        // if any operation that deletes the tree list occurs during the
        // yield, the ReadDirLevel function will be accessing freed memory
        // when it continues from the yield.
        //
        if ((hwndTree) && GetWindowLongPtr(hwndTree, GWL_READLEVEL)) {
            uMenuFlags = MF_BYCOMMAND | MF_GRAYED;
        } else {
            uMenuFlags = MF_BYCOMMAND;
        }
        EnableMenuItem(hMenu, IDM_MOVE, uMenuFlags);
        EnableMenuItem(hMenu, IDM_COPY, uMenuFlags);
        EnableMenuItem(hMenu, IDM_DELETE, uMenuFlags);
        EnableMenuItem(hMenu, IDM_RENAME, uMenuFlags);
        EnableMenuItem(hMenu, IDM_MAKEDIR, uMenuFlags);

        if (OleGetClipboard(&pDataObj) == S_OK) {
            UINT uPaste = uMenuFlags;
            FORMATETC fmtetcDrop = { CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            FORMATETC fmtetcLFN = { 0, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            unsigned short cp_format_descriptor = RegisterClipboardFormat(CFSTR_FILEDESCRIPTOR);
            unsigned short cp_format_contents = RegisterClipboardFormat(CFSTR_FILECONTENTS);

            // Set up format structure for the descriptor and contents
            FORMATETC descriptor_format = { cp_format_descriptor, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            FORMATETC contents_format = { cp_format_contents, NULL, DVASPECT_CONTENT, -1, TYMED_ISTREAM };

            fmtetcLFN.cfFormat = RegisterClipboardFormat(L"LongFileNameW");

            if (pDataObj->QueryGetData(&fmtetcDrop) != S_OK && pDataObj->QueryGetData(&fmtetcLFN) != S_OK &&
                (pDataObj->QueryGetData(&descriptor_format) != S_OK ||
                 pDataObj->QueryGetData(&contents_format) != S_OK)) {
                uPaste |= MF_GRAYED;
            }

            EnableMenuItem(hMenu, IDM_PASTE, uPaste);

            pDataObj->Release();
        }

        if (!hwndDir)
            uMenuFlags = MF_BYCOMMAND | MF_GRAYED;

        EnableMenuItem(hMenu, IDM_SELALL, uMenuFlags);
        EnableMenuItem(hMenu, IDM_DESELALL, uMenuFlags);

        if (hwndActive == hwndSearch || hwndDir)
            uMenuFlags = MF_BYCOMMAND;
        else
            uMenuFlags = MF_BYCOMMAND | MF_GRAYED;

        EnableMenuItem(hMenu, IDM_SELECT, uMenuFlags);

        pSel = (LPWSTR)SendMessage(hwndActive, FS_GETSELECTION, 5, (LPARAM)&bDir);

        //
        // can't edit a dir
        //
        uMenuFlags = bDir ? MF_BYCOMMAND | MF_DISABLED | MF_GRAYED : MF_BYCOMMAND | MF_ENABLED;

        EnableMenuItem(hMenu, IDM_EDIT, uMenuFlags);

        //
        // See if we can enable the Properties... menu
        //
        if (EnablePropertiesMenu(hwndActive, pSel))
            uMenuFlags = MF_BYCOMMAND;
        else
            uMenuFlags = MF_BYCOMMAND | MF_GRAYED;
        EnableMenuItem(hMenu, IDM_ATTRIBS, uMenuFlags);

        // Set the Empty Recycle Bin menu state based on whether it contains items
        uMenuFlags = IsRecycleBinEmpty() ? MF_BYCOMMAND | MF_GRAYED : MF_BYCOMMAND;
        EnableMenuItem(hMenu, IDM_EMPTYRECYCLE, uMenuFlags);

        // Set the ZIP Archive menu state based on file selection
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
        uMenuFlags = bHasSelection ? MF_BYCOMMAND : MF_BYCOMMAND | MF_GRAYED;
        EnableMenuItem(hMenu, IDM_ZIPARCHIVE_ADDTOZIP, uMenuFlags);
        EnableMenuItem(hMenu, IDM_ZIPARCHIVE_ADDTO, uMenuFlags);

        // Enable extract commands only if all selected files are ZIP files
        uMenuFlags = bHasZipFilesOnly ? MF_BYCOMMAND : MF_BYCOMMAND | MF_GRAYED;
        EnableMenuItem(hMenu, IDM_ZIPARCHIVE_EXTRACTHERE, uMenuFlags);
        EnableMenuItem(hMenu, IDM_ZIPARCHIVE_EXTRACTTONEWFOLDER, uMenuFlags);
        EnableMenuItem(hMenu, IDM_ZIPARCHIVE_EXTRACTTO, uMenuFlags);

        uMenuFlags = MF_BYCOMMAND;
    } else if (popupName == L"&View") {
        if (hwndActive == hwndSearch || IsIconic(hwndActive))
            uMenuFlags = MF_BYCOMMAND | MF_GRAYED;
        else {
            CheckMenuItem(
                hMenu, IDM_BOTH, hwndTree && hwndDir ? MF_CHECKED | MF_BYCOMMAND : MF_UNCHECKED | MF_BYCOMMAND);
            CheckMenuItem(
                hMenu, IDM_DIRONLY, !hwndTree && hwndDir ? MF_CHECKED | MF_BYCOMMAND : MF_UNCHECKED | MF_BYCOMMAND);
            CheckMenuItem(
                hMenu, IDM_TREEONLY, hwndTree && !hwndDir ? MF_CHECKED | MF_BYCOMMAND : MF_UNCHECKED | MF_BYCOMMAND);
        }

        EnableMenuItem(hMenu, IDM_BOTH, uMenuFlags);
        EnableMenuItem(hMenu, IDM_TREEONLY, uMenuFlags);
        EnableMenuItem(hMenu, IDM_DIRONLY, uMenuFlags);
        EnableMenuItem(hMenu, IDM_SPLIT, uMenuFlags);

        dwView &= VIEW_EVERYTHING;

        CheckMenuItem(
            hMenu, IDM_VNAME, (dwView == VIEW_NAMEONLY) ? MF_CHECKED | MF_BYCOMMAND : MF_UNCHECKED | MF_BYCOMMAND);
        CheckMenuItem(
            hMenu, IDM_VDETAILS, (dwView == VIEW_EVERYTHING) ? MF_CHECKED | MF_BYCOMMAND : MF_UNCHECKED | MF_BYCOMMAND);
        CheckMenuItem(
            hMenu, IDM_VOTHER,
            (dwView != VIEW_NAMEONLY && dwView != VIEW_EVERYTHING) ? MF_CHECKED | MF_BYCOMMAND
                                                                   : MF_UNCHECKED | MF_BYCOMMAND);

        CheckMenuItem(
            hMenu, IDM_BYNAME, (dwSort == IDD_NAME) ? MF_CHECKED | MF_BYCOMMAND : MF_UNCHECKED | MF_BYCOMMAND);
        CheckMenuItem(
            hMenu, IDM_BYTYPE, (dwSort == IDD_TYPE) ? MF_CHECKED | MF_BYCOMMAND : MF_UNCHECKED | MF_BYCOMMAND);
        CheckMenuItem(
            hMenu, IDM_BYSIZE, (dwSort == IDD_SIZE) ? MF_CHECKED | MF_BYCOMMAND : MF_UNCHECKED | MF_BYCOMMAND);
        CheckMenuItem(
            hMenu, IDM_BYDATE, (dwSort == IDD_DATE) ? MF_CHECKED | MF_BYCOMMAND : MF_UNCHECKED | MF_BYCOMMAND);
        CheckMenuItem(
            hMenu, IDM_BYFDATE, (dwSort == IDD_FDATE) ? MF_CHECKED | MF_BYCOMMAND : MF_UNCHECKED | MF_BYCOMMAND);

        if (hwndActive == hwndSearch || hwndDir)
            uMenuFlags = MF_BYCOMMAND | MF_ENABLED;
        else
            uMenuFlags = MF_BYCOMMAND | MF_GRAYED;

        EnableMenuItem(hMenu, IDM_VNAME, uMenuFlags);
        EnableMenuItem(hMenu, IDM_VDETAILS, uMenuFlags);
        EnableMenuItem(hMenu, IDM_VOTHER, uMenuFlags);

        if (hwndDir)
            uMenuFlags = MF_BYCOMMAND | MF_ENABLED;
        else
            uMenuFlags = MF_BYCOMMAND | MF_GRAYED;

        EnableMenuItem(hMenu, IDM_BYNAME, uMenuFlags);
        EnableMenuItem(hMenu, IDM_BYTYPE, uMenuFlags);
        EnableMenuItem(hMenu, IDM_BYSIZE, uMenuFlags);
        EnableMenuItem(hMenu, IDM_BYDATE, uMenuFlags);
        EnableMenuItem(hMenu, IDM_BYFDATE, uMenuFlags);

    } else if (popupName == L"&Options") {
        if (iReadLevel)
            uMenuFlags = MF_BYCOMMAND | MF_GRAYED;

        EnableMenuItem(hMenu, IDM_EXPANDTREE, uMenuFlags);

        uMenuFlags = MF_BYCOMMAND | MF_GRAYED;
    } else if (popupName == L"&Bookmarks") {
        // First, remove any existing bookmark entries (keep only "Add Bookmark..." and "Manage Bookmarks..."
        int count = GetMenuItemCount(hMenu);
        for (int i = count - 1; i > 1; i--) {  // Skip 0 and 1 which are "Add Bookmark..." and "Manage Bookmarks..."
            DeleteMenu(hMenu, i, MF_BYPOSITION);
        }

        // Get the list of bookmarks
        auto bookmarks = BookmarkList::instance().read();

        // If there are bookmarks, add a separator after "Manage Bookmarks..."
        if (!bookmarks.empty()) {
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

            // Add each bookmark to the menu
            int id = IDM_BOOKMARK_FIRST;
            for (const auto& bookmark : bookmarks) {
                // Only add if we haven't exceeded the maximum number of bookmarks
                if (id <= IDM_BOOKMARK_LAST) {
                    // Escape any ampersands in the bookmark name
                    std::wstring escapedName = EscapeMenuItemText(bookmark->name());
                    AppendMenuW(hMenu, MF_STRING, id, escapedName.c_str());
                    id++;
                }
            }
        }
    }

    return TRUE;
}

LRESULT
CALLBACK
FrameWndProc(HWND hwnd, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    RECT rc;
    HMENU hMenu2;

    DRIVEIND driveInd;
    BOOL bRedoDriveBar;

    switch (wMsg) {
        case WM_DPICHANGED:
            // Handle DPI change
            HandleDpiChange(hwnd, wParam, lParam);
            break;
        case WM_TIMER:

            //
            // this came from a FSC that wasn't generated by us
            //
            bFSCTimerSet = FALSE;
            KillTimer(hwnd, 1);

            //
            // Fall through to FS_ENABLEFSC
            //

        case FS_ENABLEFSC: {
            HWND hwndTree;

            if (--cDisableFSC)
                break;

            for (hwndTree = GetWindow(hwndMDIClient, GW_CHILD); hwndTree; hwndTree = GetWindow(hwndTree, GW_HWNDNEXT)) {
                //
                // a tree or search window
                //
                if (!GetWindow(hwndTree, GW_OWNER) && GetWindowLongPtr(hwndTree, GWL_FSCFLAG)) {
                    SendMessage(hwndTree, WM_FSC, FSC_REFRESH, 0L);
                }
            }
            break;
        }
        case FS_DISABLEFSC:

            cDisableFSC++;
            break;

        case FS_REBUILDDOCSTRING:

            BuildDocumentStringWorker();
            break;

        case FS_UPDATEDRIVETYPECOMPLETE:
            //
            // wParam = new cDrives
            //

            //
            // See if we need to update the drivebar.
            // If wParam == cDrives and rgiDrive hasn't changed, then
            // we don't need to refresh.
            //

            bRedoDriveBar = TRUE;

            if (cDrives == (int)wParam) {
                for (driveInd = 0; driveInd < cDrives; driveInd++) {
                    if (rgiDriveReal[0][driveInd] != rgiDriveReal[1][driveInd])
                        break;
                }

                bRedoDriveBar = (driveInd != cDrives);
            }

            cDrives = (int)wParam;
            iUpdateReal ^= 1;

            //
            // Update drivelist cb based on new rgiDrive[] if nec.
            //

            if (bRedoDriveBar) {
                RedoDriveWindows(NULL);
            }

            break;

        case FS_UPDATEDRIVELISTCOMPLETE:

            UpdateDriveListComplete();

            break;

        case FS_SEARCHUPDATE:

            // wParam = iDirsRead
            // lParam = iFileCount

            if (SearchInfo.hSearchDlg) {
                WCHAR szTemp[20];

                wsprintf(szTemp, SZ_PERCENTD, wParam);

                SendDlgItemMessage(SearchInfo.hSearchDlg, IDD_TIME, WM_SETTEXT, 0, (LPARAM)szTemp);

                wsprintf(szTemp, SZ_PERCENTD, lParam);

                SendDlgItemMessage(SearchInfo.hSearchDlg, IDD_FOUND, WM_SETTEXT, 0, (LPARAM)szTemp);

                UpdateWindow(SearchInfo.hSearchDlg);
            }

            //
            // If search window is active, update the status bar
            // (since set by same thread, no problem of preemption:
            // preemption only at message start/end)
            //
            if (SearchInfo.bUpdateStatus) {
                UpdateSearchStatus(SearchInfo.hwndLB, (int)lParam);
            }

            break;

        case FS_SEARCHEND:

            //
            // The thread is now dead for our purposes
            //
            SearchInfo.hThread = NULL;

            //
            // Dismiss modless dialog box then inform user if nec.
            //
            if (SearchInfo.hSearchDlg) {
                DestroyWindow(SearchInfo.hSearchDlg);
                SearchInfo.hSearchDlg = NULL;
            }

            SearchEnd();

            return 0L;

        case FS_SEARCHLINEINSERT: {
            int iRetVal;

            // wParam = &iFileCount
            // lParam = lpxdta

            ExtSelItemsInvalidate();

            iRetVal = (int)SendMessage(SearchInfo.hwndLB, LB_ADDSTRING, 0, lParam);

            if (iRetVal >= 0) {
                (*(int*)wParam)++;
            }
        }

        break;

        case WM_CREATE: {
            CLIENTCREATESTRUCT ccs;

            // Store the Frame's hwnd.
            hwndFrame = hwnd;

            hMenu2 = GetMenu(hwnd);

            // the extensions haven't been loaded yet so the window
            // menu is in the position of the first extensions menu

            ccs.hWindowMenu = (HWND)GetSubMenu(hMenu2, IDM_EXTENSIONS);
            ccs.idFirstChild = IDM_CHILDSTART;

            // create the MDI client at approximate size to make sure
            // "run minimized" works

            GetClientRect(hwndFrame, &rc);

            hwndMDIClient = CreateWindow(
                L"MDIClient", NULL, WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL | WS_BORDER, 0, 0, rc.right,
                rc.bottom, hwnd, (HMENU)1, hAppInstance, (LPVOID)&ccs);

            if (!hwndMDIClient) {
                return -1L;
            }

            InitMinimizedWindowBar(hAppInstance, hwndMDIClient);

            // make new drives window

            hwndDriveBar = CreateWindow(
                kDrivesClass, NULL,
                bDriveBar ? WS_CHILD | WS_BORDER | WS_VISIBLE | WS_CLIPSIBLINGS
                          : WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS,
                0, 0, 0, 0, hwndFrame, 0, hAppInstance, NULL);

            if (!hwndDriveBar)
                return -1L;

            hwndStatus = CreateStatusWindow(
                bStatusBar ? WS_CHILD | WS_BORDER | WS_VISIBLE | WS_CLIPSIBLINGS
                           : WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS,
                kEmptyString, hwndFrame, IDC_STATUS);

            if (hwndStatus) {
                HDC hDC;
                int nParts[3];
                int nInch;

                hDC = GetDC(NULL);
                nInch = GetDeviceCaps(hDC, LOGPIXELSX);
                ReleaseDC(NULL, hDC);

                nParts[0] = nInch * 9 / 4 + (nInch * 7 / 8);
                nParts[1] = nParts[0] + nInch * 5 / 2 + nInch * 7 / 8;
                nParts[2] = -1;

                SendMessage(hwndStatus, SB_SETPARTS, 3, (LPARAM)(LPINT)nParts);
            }
            break;
        }

        case WM_INITMENUPOPUP: {
            HMENU hPopup = (HMENU)wParam;  // the popup menu being opened

            // Find the name of the menu so we don't need position-dependent logic.
            std::wstring popupName{};
            {
                HMENU hMainMenu = GetMenu(hwnd);
                int count = GetMenuItemCount(hMainMenu);
                TCHAR buf[128];
                for (int i = 0; i < count; ++i) {
                    if (GetSubMenu(hMainMenu, i) == hPopup) {
                        if (GetMenuString(hMainMenu, i, buf, ARRAYSIZE(buf), MF_BYPOSITION)) {
                            // buf now contains something like "&File"
                            popupName = buf;
                        }
                        break;
                    }
                }
            }

            auto hwndActive = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L);

            InitPopupMenu(popupName, (HMENU)wParam, hwndActive);

            break;
        }

        case WM_DESTROY:

            DestroyMinimizedWindowBar();
            hwndFrame = NULL;
            PostQuitMessage(0);
            DestroyWindow(hwndDriveBar);
            break;

        case WM_SIZE:
            if (wParam != SIZEICONIC) {
                // uses new resize!
                ResizeControls();
                MinBarAutoSize();
            }
            break;

        case FS_FSCREQUEST:

            if (cDisableFSC == 0 || bFSCTimerSet) {
                if (bFSCTimerSet)
                    KillTimer(hwndFrame, 1);  // reset the timer

                if (SetTimer(hwndFrame, 1, uChangeNotifyTime, NULL)) {
                    bFSCTimerSet = TRUE;
                    if (cDisableFSC == 0)  // only disable once
                        SendMessage(hwndFrame, FS_DISABLEFSC, 0, 0L);
                }
            }

            break;

        case WM_FSC:

            ChangeFileSystem((WORD)wParam, (LPWSTR)lParam, NULL);
            break;

        case WM_SYSCOLORCHANGE:
        case WM_WININICHANGE:
            if (!lParam || !lstrcmpi((LPWSTR)lParam, kInternational)) {
                HWND hwnd, hwndT;
                DWORD dwFlags;

                GetInternational();

                for (hwnd = GetWindow(hwndMDIClient, GW_CHILD); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
                    if (!GetWindow(hwnd, GW_OWNER)) {
                        dwFlags = GetWindowLongPtr(hwnd, GWL_VIEW) & VIEW_EVERYTHING;

                        if (hwndT = HasDirWindow(hwnd)) {
                            SendMessage(hwndT, FS_CHANGEDISPLAY, CD_VIEW, MAKELONG(dwFlags, TRUE));
                        } else if (hwnd == hwndSearch) {
                            SetWindowLongPtr(hwnd, GWL_VIEW, dwFlags);
                            SendMessage(hwndSearch, FS_CHANGEDISPLAY, CD_VIEW, 0L);
                        }
                    }
                }
            }

            // win.ini section [colors]
            if (!lParam || !lstrcmpi((LPWSTR)lParam, L"colors")) {
                // we need to recreate the drives windows to change
                // the bitmaps

                RedoDriveWindows(NULL);
            }

            break;

        case FM_GETFOCUS:
        case FM_GETDRIVEINFOA:
        case FM_GETDRIVEINFOW:
        case FM_GETSELCOUNT:
        case FM_GETSELCOUNTLFN:
        case FM_GETFILESELA:
        case FM_GETFILESELW:
        case FM_GETFILESELLFNA:
        case FM_GETFILESELLFNW:
        case FM_REFRESH_WINDOWS:
        case FM_RELOAD_EXTENSIONS:
            return ExtensionMsgProc(wMsg, wParam, lParam);
            break;

        case WM_SETFOCUS:
            UpdateMoveStatus(ReadMoveStatus());
            goto DoDefault;

        case WM_MENUSELECT: {
            if (GET_WM_MENUSELECT_HMENU(wParam, lParam)) {
                // Save the menu the user selected
                uMenuID = GET_WM_MENUSELECT_CMD(wParam, lParam);
                uMenuFlags = GET_WM_MENUSELECT_FLAGS(wParam, lParam);
                hMenu = GET_WM_MENUSELECT_HMENU(wParam, lParam);
                if (uMenuID >= IDM_CHILDSTART && uMenuID < IDM_HELPINDEX)
                    uMenuID = IDM_CHILDSTART;

                // Handle child/frame sys menu decision

                //
                // If maximized, and the 0th menu is set,
                // then it must be the MDI child system menu.
                //
                bMDIFrameSysMenu = (hMenu == GetSystemMenu(hwndFrame, FALSE));
            }
        }

        break;

        case WM_SYSCOMMAND:
            if (GetFocus() == hwndDriveList)
                SendMessage(hwndDriveList, CB_SHOWDROPDOWN, FALSE, 0L);
            return DefFrameProc(hwnd, hwndMDIClient, wMsg, wParam, lParam);
            break;

        case WM_ENDSESSION:

            if (wParam) {
                // Yeah, I know I shouldn't have to save this, but I don't
                // trust anybody

                BOOL bSaveExit = bExitWindows;
                bExitWindows = FALSE;

                // Simulate an exit command to clean up, but don't display
                // the "are you sure you want to exit", since somebody should
                // have already taken care of that, and hitting Cancel has no
                // effect anyway.

                AppCommandProc(IDM_EXIT);
                bExitWindows = bSaveExit;
            }
            break;

        case WM_CLOSE:

            wParam = IDM_EXIT;

            /*** FALL THROUGH to WM_COMMAND ***/

        case WM_COMMAND:
            if (AppCommandProc(GET_WM_COMMAND_ID(wParam, lParam)))
                break;
            if (GET_WM_COMMAND_ID(wParam, lParam) == IDM_EXIT) {
                FreeExtensions();

                DestroyWindow(hwnd);
                break;
            }
            /*** FALL THROUGH ***/

        case WM_NCPAINT:
        case WM_NCACTIVATE: {
            LRESULT result = DefFrameProc(hwnd, hwndMDIClient, wMsg, wParam, lParam);
            libheirloom::redrawMdiMenuBarButtons(hwnd, hwndMDIClient);
            return result;
        }

        case WM_NCLBUTTONDOWN:
            if (libheirloom::handleMdiMenuBarMouseDown(hwnd, hwndMDIClient, lParam))
                return 0;
            goto DoDefault;

        default:
        DoDefault:
            return DefFrameProc(hwnd, hwndMDIClient, wMsg, wParam, lParam);
    }

    return 0L;
}

LRESULT
CALLBACK
MessageFilter(int nCode, WPARAM wParam, LPARAM lParam) {
    LPMSG lpMsg = (LPMSG)lParam;

    if (nCode < 0)
        goto DefHook;

    if (nCode == MSGF_MENU) {
        if (lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_F1) {
            // Window of menu we want help for is in loword of lParam.

            PostMessage(hwndFrame, wHelpMessage, MSGF_MENU, (LPARAM)lpMsg->hwnd);
            return 1;
        }

    } else if (nCode == MSGF_DIALOGBOX) {
        if (lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_F1) {
            // Dialog box we want help for is in loword of lParam

            PostMessage(hwndFrame, wHelpMessage, MSGF_DIALOGBOX, (LPARAM)lpMsg->hwnd);
            return 1;
        }

    } else

    DefHook:
        return (int)DefHookProc(nCode, wParam, (LPARAM)lpMsg, &hhkMsgFilter);

    return 0;
}

/////////////////////////////////////////////////////////////////////
//
// Name:     EnablePropertiesMenu
//
// Synopsis: Check if we enable the Properties... menu item
//           Disable if:
//
//           1. _ONLY_ the .. dir is sel
//           2. Nothing is selected in the window with focus
//
// IN    hwndActive   Current active window, has listbox in LASTFOCUS
// IN    pSel         Current sel
//
// Return:   TRUE if Properties... should be enabled.
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

BOOL EnablePropertiesMenu(HWND hwndActive, LPWSTR pSel) {
    LPXDTALINK lpStart;
    LRESULT dwHighlight;  // Number of highlighted entries in listbox
    LPXDTA lpxdta;        // Pointer to listbox DTA data
    BOOL bRet;            // Return value
    HWND hwndLB;
    HWND hwndDir;
    HWND hwndTree;
    HWND hwndParent;

    bRet = FALSE;

    //
    // Quit if pSel == NULL (File selected before any window created)
    //
    if (!pSel)
        return (FALSE);

    hwndLB = (HWND)GetWindowLongPtr(hwndActive, GWL_LASTFOCUS);

    if (!hwndLB)
        return (FALSE);

    dwHighlight = SendMessage(hwndLB, LB_GETSELCOUNT, 0, 0L);

    //
    // This is OK since the search window can never contain the root
    //
    if (hwndActive == hwndSearch)
        return (dwHighlight >= 1);

    hwndTree = HasTreeWindow(hwndActive);
    hwndDir = HasDirWindow(hwndActive);
    hwndParent = GetParent(hwndLB);

    if (hwndParent == hwndDir) {
        //
        // Lock down DTA data
        //
        if (!(lpStart = (LPXDTALINK)GetWindowLongPtr(GetParent(hwndLB), GWL_HDTA)))
            return (FALSE);

        if (dwHighlight <= 0)
            goto ReturnFalse;

        if (dwHighlight > 1)
            goto ReturnTrue;

        //
        // If exactly one element is highlighted, make sure it is not ..
        //
        if (!(BOOL)SendMessage(hwndLB, LB_GETSEL, 0, 0L))
            goto ReturnTrue;

        //
        // Get the DTA index.
        //
        if (SendMessage(hwndLB, LB_GETTEXT, 0, (LPARAM)&lpxdta) == LB_ERR || !lpxdta) {
            goto ReturnFalse;
        }

        if ((lpxdta->dwAttrs & ATTR_DIR) && (lpxdta->dwAttrs & ATTR_PARENT))
            goto ReturnFalse;

    ReturnTrue:

        bRet = TRUE;

    ReturnFalse:

        return (bRet);
    }

    //
    // If this is the tree window and we are not in the middle of ReadDirLevel
    // then it is OK to change properties.
    //
    if (hwndParent == hwndTree) {
        if (SendMessage(hwndLB, LB_GETCURSEL, 0, 0L) != LB_ERR && !GetWindowLongPtr(hwndTree, GWL_READLEVEL))

            return (TRUE);
    }

    return (FALSE);
}

// Helper function to escape ampersands in menu item text
std::wstring EscapeMenuItemText(const std::wstring& text) {
    std::wstring escaped;
    escaped.reserve(text.length() * 2);  // Reserve space for potential full doubling

    for (wchar_t c : text) {
        if (c == L'&') {
            escaped += L"&&";  // Double the ampersand to escape it
        } else {
            escaped += c;
        }
    }

    return escaped;
}
