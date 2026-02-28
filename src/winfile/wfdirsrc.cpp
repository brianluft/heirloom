/********************************************************************

   wfDirSrc.c

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#include "winfile.h"
#include "wfdrop.h"
#include "wfdragsrc.h"
#include "wfutil.h"
#include "wfdir.h"
#include "stringconstants.h"
#include <commctrl.h>

#define DO_DROPFILE 0x454C4946L

#define DO_DROPONDESKTOP 0x504D42L

HWND hwndGlobalSink = NULL;

// Forward declare functions
DWORD PerformDragOperation(HWND hwnd, LPWSTR pFiles, UINT iSel);
void SelectItem(HWND hwndLB, WPARAM wParam, BOOL bSel);
void ShowItemBitmaps(HWND hwndLB, int iShow);
int GetDragStatusText(int iOperation);

/////////////////////////////////////////////////////////////////////
//
// Name:     MatchFile
//
// Synopsis: Match dos wildcard spec vs. dos filename
//           Both strings in uppercase
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

BOOL MatchFile(LPWSTR szFile, LPWSTR szSpec) {
#define IS_DOTEND(ch) ((ch) == CHAR_DOT || (ch) == CHAR_NULL)

    if (!lstrcmp(szSpec, SZ_STAR) ||     // "*" matches everything
        !lstrcmp(szSpec, kStarDotStar))  // so does "*.*"
        return TRUE;

    while (*szFile && *szSpec) {
        switch (*szSpec) {
            case CHAR_QUESTION:
                szFile++;
                szSpec++;
                break;

            case CHAR_STAR:

                while (!IS_DOTEND(*szSpec))  // got till a terminator
                    szSpec = CharNext(szSpec);

                if (*szSpec == CHAR_DOT)
                    szSpec++;

                while (!IS_DOTEND(*szFile))  // got till a terminator
                    szFile = CharNext(szFile);

                if (*szFile == CHAR_DOT)
                    szFile++;

                break;

            default:
                if (*szSpec == *szFile) {
                    szFile++;
                    szSpec++;
                } else
                    return FALSE;
        }
    }
    return !*szFile && !*szSpec;
}

void DSSetSelection(HWND hwndLB, BOOL bSelect, LPWSTR szSpec, BOOL bSearch) {
    int i;
    int iMac;
    LPXDTA lpxdta;
    LPXDTALINK lpStart;
    WCHAR szTemp[MAXPATHLEN];

    CharUpper(szSpec);

    lpStart = (LPXDTALINK)GetWindowLongPtr(GetParent(hwndLB), GWL_HDTA);

    if (!lpStart)
        return;

    iMac = (int)MemLinkToHead(lpStart)->dwEntries;

    for (i = 0; i < iMac; i++) {
        if (SendMessage(hwndLB, LB_GETTEXT, i, (LPARAM)&lpxdta) == LB_ERR)
            return;

        if (!lpxdta || lpxdta->dwAttrs & ATTR_PARENT)
            continue;

        lstrcpy(szTemp, MemGetFileName(lpxdta));

        if (bSearch) {
            StripPath(szTemp);
        }

        CharUpper(szTemp);

        if (MatchFile(szTemp, szSpec))
            SendMessage(hwndLB, LB_SETSEL, bSelect, i);
    }
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ShowItemBitmaps() -                                                     */
/*                                                                          */
/*--------------------------------------------------------------------------*/

void ShowItemBitmaps(HWND hwndLB, int iShow) {
    int i;
    int iMac;
    int iFirstSel;
    RECT rc;
    int dx;
    LPINT lpSelItems;

    if (iShow == iShowSourceBitmaps)
        return;

    iShowSourceBitmaps = iShow;

    dx = dxFolder + dyBorderx2 + dyBorder;

    //
    // Invalidate the bitmap parts of all visible, selected items.
    //
    iFirstSel = (int)SendMessage(hwndLB, LB_GETTOPINDEX, 0, 0L);
    iMac = (int)SendMessage(hwndLB, LB_GETSELCOUNT, 0, 0L);

    if (iMac == LB_ERR)
        return;

    lpSelItems = (LPINT)LocalAlloc(LMEM_FIXED, sizeof(int) * iMac);

    if (lpSelItems == NULL)
        return;

    iMac = (int)SendMessage(hwndLB, LB_GETSELITEMS, (WPARAM)iMac, (LPARAM)lpSelItems);

    for (i = 0; i < iMac; i++) {
        if (lpSelItems[i] < iFirstSel)
            continue;

        if (SendMessage(hwndLB, LB_GETITEMRECT, lpSelItems[i], (LPARAM)&rc) == LB_ERR)
            break;

        //
        // Invalidate the bitmap area.
        //
        rc.right = rc.left + dx;
        InvalidateRect(hwndLB, &rc, FALSE);
    }
    UpdateWindow(hwndLB);

    LocalFree((HLOCAL)lpSelItems);
}

/////////////////////////////////////////////////////////////////////
//
// Name:     SelectItem
//
// Synopsis:
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

void SelectItem(HWND hwndLB, WPARAM wParam, BOOL bSel) {
    //
    // Add the current item to the selection.
    //
    SendMessage(hwndLB, LB_SETSEL, bSel, (DWORD)wParam);

    //
    // Give the selected item the focus rect and anchor pt.
    //
    SendMessage(hwndLB, LB_SETCARETINDEX, wParam, MAKELONG(TRUE, 0));
    SendMessage(hwndLB, LB_SETANCHORINDEX, wParam, 0L);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  DropFilesOnApplication() -                                              */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/* this function will determine whether the application we are currently
 * over is a valid drop point and drop the files
 */

WORD DropFilesOnApplication(LPWSTR pszFiles) {
    POINT pt;
    HWND hwnd;
    RECT rc;
    HANDLE hDrop;

    if (!(hwnd = hwndGlobalSink))
        return 0;

    hwndGlobalSink = NULL;

    GetCursorPos(&pt);
    GetClientRect(hwnd, &rc);
    ScreenToClient(hwnd, &pt);

    hDrop = CreateDropFiles(pt, !PtInRect(&rc, pt), pszFiles);

    if (!hDrop)
        return 0;

    PostMessage(hwnd, WM_DROPFILES, (WPARAM)hDrop, 0L);

    return 1;
}

/////////////////////////////////////////////////////////////////////
//
// Name:     DSTrackPoint
//
// Synopsis:
//
// Return:   0 for normal mouse tracking
//           1 for no mouse single click processing
//           2 for no mouse single- or double-click tracking
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

int DSTrackPoint(HWND hwnd, HWND hwndLB, WPARAM wParam, LPARAM lParam, BOOL bSearch) {
    UINT iSel;
    MSG msg;
    RECT rc;
    DWORD dwAnchor;
    DWORD dwTemp;
    LPWSTR pch;
    BOOL bDir;
    BOOL bSelected;
    BOOL bSelectOneItem;
    BOOL bUnselectIfNoDrag;
    LPWSTR pszFile;
    POINT pt;
    int iSelCount;

    bSelectOneItem = FALSE;
    bUnselectIfNoDrag = FALSE;

    bSelected = (BOOL)SendMessage(hwndLB, LB_GETSEL, wParam, 0L);
    iSelCount = (int)SendMessage(hwndLB, LB_GETSELCOUNT, 0, 0L);

    if (GetKeyState(VK_SHIFT) < 0) {
        // What is the state of the Anchor point?
        dwAnchor = (DWORD)SendMessage(hwndLB, LB_GETANCHORINDEX, 0, 0L);
        bSelected = (BOOL)SendMessage(hwndLB, LB_GETSEL, dwAnchor, 0L);

        // If Control is up, turn everything off.
        if (!(GetKeyState(VK_CONTROL) < 0))
            SendMessage(hwndLB, LB_SETSEL, FALSE, -1L);

        // Select everything between the Anchor point and the item.
        SendMessage(hwndLB, LB_SELITEMRANGE, bSelected, MAKELONG(wParam, dwAnchor));

        // Give the selected item the focus rect.
        SendMessage(hwndLB, LB_SETCARETINDEX, wParam, 0L);

    } else if (GetKeyState(VK_CONTROL) < 0) {
        if (bSelected)
            bUnselectIfNoDrag = TRUE;
        else
            SelectItem(hwndLB, wParam, TRUE);

    } else {
        if (bSelected && iSelCount > 1) {
            // If clicking on an already selected item in a multi-selection,
            // preserve the selection for potential drag operation
            bSelectOneItem = TRUE;
        } else if (!bSelected) {
            // Deselect everything.
            SendMessage(hwndLB, LB_SETSEL, FALSE, -1L);

            // Select the current item.
            SelectItem(hwndLB, wParam, TRUE);
        }
    }

    if (!bSearch)
        UpdateStatus(GetParent(hwnd));

    POINTSTOPOINT(pt, lParam);
    ClientToScreen(hwndLB, (LPPOINT)&pt);
    ScreenToClient(hwnd, (LPPOINT)&pt);

    // See if the user moves a certain number of pixels in any direction
    SetRect(&rc, pt.x - dxClickRect, pt.y - dyClickRect, pt.x + dxClickRect, pt.y + dyClickRect);

    SetCapture(hwnd);

    for (;;) {
        if (GetCapture() != hwnd) {
            msg.message = WM_LBUTTONUP;  // don't proceed below
            break;
        }

        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            DispatchMessage(&msg);

            // WM_CANCELMODE messages will unset the capture, in that
            // case I want to exit this loop
            if (msg.message == WM_CANCELMODE || GetCapture() != hwnd) {
                msg.message = WM_LBUTTONUP;  // don't proceed below
                break;
            }

            if (msg.message == WM_LBUTTONUP)
                break;

            POINTSTOPOINT(pt, msg.lParam);
            if ((msg.message == WM_MOUSEMOVE) && !(PtInRect(&rc, pt)))
                break;
        }
    }
    ReleaseCapture();

    // Did the guy NOT drag anything?
    if (msg.message == WM_LBUTTONUP) {
        if (bSelectOneItem) {
            /* Deselect everything. */
            SendMessage(hwndLB, LB_SETSEL, FALSE, -1L);

            /* Select the current item. */
            SelectItem(hwndLB, wParam, TRUE);
        }

        if (bUnselectIfNoDrag)
            SelectItem(hwndLB, wParam, FALSE);

        // notify the appropriate people
        SendMessage(hwnd, WM_COMMAND, GET_WM_COMMAND_MPS(0, hwndLB, LBN_SELCHANGE));

        return 1;
    }

    // User is starting a drag operation - prepare the cursor
    iSelCount = (int)SendMessage(hwndLB, LB_GETSELCOUNT, 0, 0L);

    if (iSelCount == 1) {
        LPXDTA lpxdta;

        // Only one thing selected - get info about it
        if (SendMessage(hwndLB, LB_GETTEXT, wParam, (LPARAM)&lpxdta) == LB_ERR || !lpxdta) {
            return 1;
        }

        pszFile = MemGetFileName(lpxdta);
        bDir = lpxdta->dwAttrs & ATTR_DIR;

        // Avoid dragging the parent dir
        if (lpxdta->dwAttrs & ATTR_PARENT) {
            return 1;
        }

        // Replacing old DOF_* constants with simple numeric values for drag types
        if (bDir) {
            // 1 = Directory
            iSel = 1;  // Was DOF_DIRECTORY
        } else if (IsProgramFile(pszFile)) {
            // 2 = Executable
            iSel = 2;  // Was DOF_EXECUTABLE
        } else if (IsDocument(pszFile)) {
            // 3 = Document
            iSel = 3;  // Was DOF_DOCUMENT
        } else
            // 3 = Document (default)
            iSel = 3;  // Was DOF_DOCUMENT

    } else {
        // Multiple files are selected - use multiple drag cursor
        // 4 = Multiple files
        iSel = 4;  // Was DOF_MULTIPLE
    }

    // Get the list of selected things
    pch = (LPWSTR)SendMessage(hwnd, FS_GETSELECTION, FALSE, 0L);

    // Start drag operation
    hwndDragging = hwndLB;

    // Use our new OLE-based drag and drop instead of the old DragObject
    dwTemp = PerformDragOperation(hwnd, pch, iSel);

    SetWindowDirectory();

    if (dwTemp == DO_DROPFILE) {
        // try and drop them on an application
        DropFilesOnApplication(pch);
    }

    LocalFree((HANDLE)pch);

    if (IsWindow(hwnd))
        ShowItemBitmaps(hwndLB, TRUE);

    hwndDragging = NULL;

    if (!bSearch && IsWindow(hwnd))
        UpdateStatus(GetParent(hwnd));

    return 2;
}

/////////////////////////////////////////////////////////////////////
//
// Name:     SkipPathHead
//
// Synopsis: Skips "C:\" and "\\foo\bar\"
//
// INC       lpszPath -- path to check
//
// Return:   LPWSTR   pointer to first filespec
//
// Assumes:
//
// Effects:
//
//
// Notes:    If not fully qualified, returns NULL
//
/////////////////////////////////////////////////////////////////////

LPWSTR
SkipPathHead(LPWSTR lpszPath) {
    LPWSTR p = lpszPath;
    int i;

    if (ISUNCPATH(p)) {
        for (i = 0, p += 2; *p && i < 2; p++) {
            if (CHAR_BACKSLASH == *p)
                i++;
        }

        //
        // If we ran out of string, punt.
        //
        if (!*p)
            return NULL;
        else
            return p;

    } else if (CHAR_COLON == lpszPath[1] && CHAR_BACKSLASH == lpszPath[2]) {
        //
        // Regular pathname
        //

        return lpszPath + 3;
    }

    return NULL;
}

// Perform a drag operation using the OLE drag-drop system
// This replaces the old Windows 3.x DragObject function
DWORD PerformDragOperation(HWND hwnd, LPWSTR pFiles, UINT iSel) {
    DWORD dwReturn = 0;
    POINT ptCursor;
    DWORD dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE;

    // Get the current cursor position for the drag
    GetCursorPos(&ptCursor);

    // Use our OLE drag drop implementation
    HRESULT hr = WFDoDragDrop(hwnd, pFiles, ptCursor, &dwEffect);

    // Return appropriate values for the old system to maintain compatibility
    if (SUCCEEDED(hr) && dwEffect != DROPEFFECT_NONE) {
        if (dwEffect & DROPEFFECT_MOVE)
            return 1;  // Equivalent to a move operation
        else if (dwEffect & DROPEFFECT_COPY)
            return 2;  // Equivalent to a copy operation
    }

    return 0;
}
