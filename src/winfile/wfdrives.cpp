#define PUBLIC  // avoid collision with shell.h
#include "winfile.h"
#include "treectl.h"
#include "lfn.h"
#include "wfcopy.h"
#include "wfpng.h"
#include "wfcomman.h"
#include "wfutil.h"
#include "wfdir.h"
#include "wftree.h"
#include "wfdrives.h"
#include "wfdpi.h"
#include "stringconstants.h"
#include <commctrl.h>
#include <shlobj.h>

// Toolbar PNG indices (order matches IDR_PNG_TOOLBAR_00..07)
#define TBAR_IMG_LIST 0
#define TBAR_IMG_DETAILS 1
#define TBAR_IMG_SORT_NAME 2
#define TBAR_IMG_SORT_TYPE 3
#define TBAR_IMG_SORT_SIZE 4
#define TBAR_IMG_SORT_DATE_NEWEST 5
#define TBAR_IMG_SORT_DATE_OLDEST 6
#define TBAR_IMG_NEW_WINDOW 7
#define TBAR_IMG_COUNT 8

// Control IDs for child windows within the toolbar
#define IDC_TOOLBAR_BUTTONS 3010
#define IDC_LOCATION_COMBO 3011

// Internal toolbar child windows
static HWND hwndToolbarCtrl = NULL;
static HWND hwndLocationCombo = NULL;
static HIMAGELIST himlDriveIcons = NULL;

// Button definitions for the toolbar
static const struct {
    int idm;       // command ID (0 = separator)
    int imgIndex;  // toolbar PNG index (-1 = separator)
    BYTE style;    // TBSTYLE_*
    BYTE group;    // 0=none, 1=view, 2=sort
} toolbarButtons[] = {
    { 0, -1, TBSTYLE_SEP, 0 },
    { IDM_VNAME, TBAR_IMG_LIST, TBSTYLE_CHECKGROUP, 1 },
    { IDM_VDETAILS, TBAR_IMG_DETAILS, TBSTYLE_CHECKGROUP, 1 },
    { 0, -1, TBSTYLE_SEP, 0 },
    { IDM_BYNAME, TBAR_IMG_SORT_NAME, TBSTYLE_CHECKGROUP, 2 },
    { IDM_BYTYPE, TBAR_IMG_SORT_TYPE, TBSTYLE_CHECKGROUP, 2 },
    { IDM_BYSIZE, TBAR_IMG_SORT_SIZE, TBSTYLE_CHECKGROUP, 2 },
    { IDM_BYDATE, TBAR_IMG_SORT_DATE_NEWEST, TBSTYLE_CHECKGROUP, 2 },
    { IDM_BYFDATE, TBAR_IMG_SORT_DATE_OLDEST, TBSTYLE_CHECKGROUP, 2 },
    { 0, -1, TBSTYLE_SEP, 0 },
    { IDM_NEWWINDOW, TBAR_IMG_NEW_WINDOW, TBSTYLE_BUTTON, 0 },
};
#define NUM_TOOLBAR_BUTTONS (sizeof(toolbarButtons) / sizeof(toolbarButtons[0]))

static void PopulateLocationCombo();
static void CreateDriveImageList(UINT dpi);

/////////////////////////////////////////////////////////////////////
//
// Name:     NewTree
//
// Synopsis: Creates new split tree for given drive.  Inherits all
//           properties of current window.
//
// IN        drive    Drive number to create window for
// IN        hwndSrc  Base properties on this window
//
/////////////////////////////////////////////////////////////////////

void NewTree(DRIVE drive, HWND hwndSrc) {
    HWND hwnd, hwndTree, hwndDir;
    WCHAR szDir[MAXPATHLEN * 2];
    int dxSplit;
    BOOL bDir;
    LPWSTR pszSearchDir = NULL;
    LPWSTR psz;

    //
    // make sure the floppy/net drive is still valid
    //
    if (!CheckDrive(hwndSrc, drive, FUNC_SETDRIVE))
        return;

    if (hwndSrc) {
        pszSearchDir = (LPWSTR)SendMessage(hwndSrc, FS_GETSELECTION, 1 | 4 | 16, (LPARAM)&bDir);
    }

    //
    // If no selection
    //
    if (!pszSearchDir || !pszSearchDir[0] || DRIVEID(pszSearchDir) != drive) {
        //
        // Update net con in case remote drive was swapped from console
        //
        if (IsRemoteDrive(drive)) {
            R_NetCon(drive);
        }

        //
        // Update volume label here too if removable
        //
        if (IsRemovableDrive(drive)) {
            R_VolInfo(drive);
        }

        if (hwndSrc) {
            GetSelectedDirectory(drive + 1, szDir);
            AddBackslash(szDir);
            SendMessage(hwndSrc, FS_GETFILESPEC, MAXPATHLEN, (LPARAM)(szDir + lstrlen(szDir)));
        } else {
            szDir[0] = CHAR_A + drive;
            szDir[1] = CHAR_COLON;
            szDir[2] = CHAR_BACKSLASH;
            szDir[3] = CHAR_NULL;
            lstrcat(szDir, kStarDotStar);
        }
    } else {
        lstrcpy(szDir, pszSearchDir);

        if (!bDir) {
            RemoveLast(szDir);

            psz = pszSearchDir + lstrlen(szDir) + 1;

            pszInitialDirSel = (LPWSTR)LocalAlloc(LMEM_FIXED, ByteCountOf(lstrlen(psz) + 1));
            if (pszInitialDirSel) {
                lstrcpy(pszInitialDirSel, psz);
            }
        }

        AddBackslash(szDir);
        lstrcat(szDir, kStarDotStar);
    }

    if (!hwndSrc || hwndSrc == hwndSearch) {
        dxSplit = -1;
    } else {
        hwndTree = HasTreeWindow(hwndSrc);
        hwndDir = HasDirWindow(hwndSrc);

        if (hwndTree && hwndDir) {
            dxSplit = GetSplit(hwndSrc);
        } else if (hwndDir) {
            dxSplit = 0;
        } else {
            dxSplit = 10000;
        }
    }

    //
    // take all the attributes from the current window
    //
    if (hwndSrc) {
        dwNewSort = (DWORD)GetWindowLongPtr(hwndSrc, GWL_SORT);
        dwNewView = (DWORD)GetWindowLongPtr(hwndSrc, GWL_VIEW);
    } else {
        dwNewSort = IDD_NAME;
        dwNewView = VIEW_NAMEONLY;
    }

    hwnd = CreateTreeWindow(szDir, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, dxSplit);

    if (hwnd && (hwndTree = HasTreeWindow(hwnd)))
        SendMessage(hwndTree, TC_SETDRIVE, MAKELONG(MAKEWORD(FALSE, 0), TRUE), 0L);

    if (pszSearchDir)
        LocalFree((HLOCAL)pszSearchDir);
}

// check net/floppy drives for validity, sets the net drive bitmap
// when the thing is not available

BOOL CheckDrive(HWND hwnd, DRIVE drive, DWORD dwFunc) {
    DWORD err;
    DRIVEIND driveInd;
    HCURSOR hCursor;
    WCHAR szDrive[] = SZ_ACOLON;

    hCursor = LoadCursor(NULL, IDC_WAIT);

    if (hCursor)
        hCursor = SetCursor(hCursor);
    ShowCursor(TRUE);

    DRIVESET(szDrive, drive);

    // find index for this drive
    driveInd = 0;
    while ((driveInd < cDrives) && (rgiDrive[driveInd] != drive))
        driveInd++;

    switch (IsNetDrive(drive)) {
        case 2:

            R_Type(drive);

            if (IsValidDisk(drive)) {
                R_NetCon(drive);

            } else {
                aDriveInfo[drive].uType = DRIVE_REMOTE;

                WAITNET();

                err = WNetRestoreSingleConnection(hwnd, szDrive, TRUE);

                if (err != WN_SUCCESS) {
                    aDriveInfo[drive].iOffset = 5;

                    if (hCursor)
                        SetCursor(hCursor);
                    ShowCursor(FALSE);

                    return FALSE;
                }

                C_NetCon(drive, ERROR_SUCCESS);
            }

            aDriveInfo[drive].bRemembered = FALSE;

            // fall through...

        case 1:

            aDriveInfo[drive].iOffset = 4;
            break;

        default:
            break;
    }

    if (hCursor)
        SetCursor(hCursor);
    ShowCursor(FALSE);

    return IsTheDiskReallyThere(hwnd, szDrive, dwFunc, FALSE);
}

//
// set the current window to a new drive
//

void DrivesSetDrive(HWND hWnd, DRIVEIND driveInd, DRIVEIND driveIndCur, BOOL bDontSteal) {
    WCHAR szPath[MAXPATHLEN * 2];

    HWND hwndChild;
    HWND hwndTree;
    HWND hwndDir;

    DRIVE drive;

    hwndChild = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L);

    //
    // save the current directory on this drive for later
    //
    GetSelectedDirectory(0, szPath);
    SaveDirectory(szPath);

    drive = rgiDrive[driveInd];

    I_NetCon(drive);
    I_VolInfo(drive);

    if (!CheckDrive(hWnd, drive, FUNC_SETDRIVE))
        return;

    hwndTree = HasTreeWindow(hwndChild);

    if (hwndTree && GetWindowLongPtr(hwndTree, GWL_READLEVEL)) {
        if (driveInd != driveIndCur)
            bCancelTree = TRUE;
        return;
    }

    GetSelectedDirectory((drive + 1), szPath);

    SetWindowLongPtr(hWnd, GWL_CURDRIVEIND, driveInd);
    SetWindowLongPtr(hWnd, GWL_CURDRIVEFOCUS, driveInd);

    if (hwndDir = HasDirWindow(hwndChild)) {
        UINT iStrLen;
        AddBackslash(szPath);
        iStrLen = lstrlen(szPath);
        SendMessage(hwndDir, FS_GETFILESPEC, COUNTOF(szPath) - iStrLen, (LPARAM)(szPath + iStrLen));

        SendMessage(
            hwndDir, FS_CHANGEDISPLAY, bDontSteal ? CD_PATH_FORCE | CD_DONTSTEAL : CD_PATH_FORCE, (LPARAM)szPath);

        StripFilespec(szPath);
    }

    SPC_SET_HITDISK(qFreeSpace);

    if (hwndTree) {
        SendMessage(hwndTree, TC_SETDRIVE, MAKEWORD(GetKeyState(VK_SHIFT) < 0, bDontSteal), (LPARAM)(szPath));
    } else {
        RECT rc;
        GetClientRect(hwndChild, &rc);
        ResizeWindows(hwndChild, (WORD)(rc.right + 1), (WORD)(rc.bottom + 1));
    }

    if (bDriveBar) {
        UpdateToolbarState(hwndChild);
    }

    UpdateStatus(hwndChild);
}

//
// Layout metrics for toolbar child controls.
//
struct ToolbarMetrics {
    int padding;
    int leftMargin;
    int rightMargin;
    int sepWidth;
    int comboDropdownHeight;
    int spacing;
};

static ToolbarMetrics GetToolbarMetrics(UINT dpi) {
    ToolbarMetrics m;
    m.padding = ScaleValueForDpi(4, dpi);
    m.leftMargin = ScaleValueForDpi(4, dpi);
    m.rightMargin = ScaleValueForDpi(4, dpi);
    m.sepWidth = ScaleValueForDpi(8, dpi);
    m.comboDropdownHeight = ScaleValueForDpi(200, dpi);
    m.spacing = ScaleValueForDpi(8, dpi);
    return m;
}

//
// Render PNG images into an HIMAGELIST for use in toolbar/combo controls.
//
static HIMAGELIST CreatePngImageList(UINT dpi, PNG_TYPE type, int count, UINT iconCX, UINT iconCY) {
    HIMAGELIST himl = ImageList_Create(iconCX, iconCY, ILC_COLOR32, count, 0);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    for (int i = 0; i < count; i++) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = iconCX;
        bi.bmiHeader.biHeight = -((LONG)iconCY);  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* pvBits;
        HBITMAP hbm = CreateDIBSection(hdcScreen, &bi, DIB_RGB_COLORS, &pvBits, NULL, 0);
        if (hbm) {
            HGDIOBJ hOld = SelectObject(hdcMem, hbm);
            memset(pvBits, 0, iconCX * iconCY * 4);
            PngDraw(hdcMem, dpi, 0, 0, type, i);
            SelectObject(hdcMem, hOld);
            ImageList_Add(himl, hbm, NULL);
            DeleteObject(hbm);
        }
    }

    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return himl;
}

//
// Build the drive icon image list for the location combo box.
//
static void CreateDriveImageList(UINT dpi) {
    UINT iconCX, iconCY;
    PngGetScaledSize(dpi, PNG_TYPE_DRIVE, 0, &iconCX, &iconCY);
    if (iconCX == 0)
        iconCX = iconCY = 16;

    if (himlDriveIcons) {
        ImageList_Destroy(himlDriveIcons);
    }

    himlDriveIcons = CreatePngImageList(dpi, PNG_TYPE_DRIVE, 6, iconCX, iconCY);
}

static void PopulateLocationCombo() {
    if (!hwndLocationCombo)
        return;

    SendMessage(hwndLocationCombo, CB_RESETCONTENT, 0, 0);

    COMBOBOXEXITEMW cbei = {};
    cbei.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE;

    WCHAR szDrive[4];

    for (int i = 0; i < cDrives; i++) {
        DRIVE drive = rgiDrive[i];
        szDrive[0] = (WCHAR)(chFirstDrive + drive);
        szDrive[1] = L':';
        szDrive[2] = L'\0';

        cbei.iItem = i;
        cbei.pszText = szDrive;
        cbei.iImage = aDriveInfo[drive].iOffset;
        cbei.iSelectedImage = aDriveInfo[drive].iOffset;

        SendMessage(hwndLocationCombo, CBEM_INSERTITEM, 0, (LPARAM)&cbei);
    }
}

//
// Navigate the active MDI child to an arbitrary path typed in the combobox.
//
void NavigateToPath(LPCWSTR pszPath) {
    HWND hwndActive = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L);

    // Determine the drive from the path
    DRIVE drive = -1;
    if (pszPath[0] && pszPath[1] == L':') {
        WCHAR ch = pszPath[0];
        if (ch >= L'a' && ch <= L'z')
            drive = ch - L'a';
        else if (ch >= L'A' && ch <= L'Z')
            drive = ch - L'A';
    }

    if (drive < 0)
        return;

    // Check if the path exists
    DWORD attrs = GetFileAttributesW(pszPath);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        // Try treating it as a file path - strip filename
        WCHAR szDir[MAXPATHLEN];
        lstrcpyn(szDir, pszPath, COUNTOF(szDir));
        LPWSTR pSlash = wcsrchr(szDir, L'\\');
        if (pSlash) {
            *pSlash = L'\0';
            attrs = GetFileAttributesW(szDir);
        }
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            MessageBeep(MB_ICONEXCLAMATION);
            return;
        }
    }

    // Build a path with filespec for the tree window
    WCHAR szFullPath[MAXPATHLEN * 2];
    lstrcpyn(szFullPath, pszPath, COUNTOF(szFullPath));

    // Ensure trailing backslash + *.*
    int len = lstrlen(szFullPath);
    if (len > 0 && szFullPath[len - 1] != L'\\') {
        szFullPath[len] = L'\\';
        szFullPath[len + 1] = L'\0';
    }
    lstrcat(szFullPath, kStarDotStar);

    if (!hwndActive || hwndActive == hwndSearch) {
        // No active tree window - create a new one
        dwNewSort = IDD_NAME;
        dwNewView = VIEW_NAMEONLY;
        CreateTreeWindow(szFullPath, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, -1);
        return;
    }

    // Navigate active window to the new path
    HWND hwndDir = HasDirWindow(hwndActive);
    HWND hwndTree = HasTreeWindow(hwndActive);

    if (hwndDir) {
        SendMessage(hwndDir, FS_CHANGEDISPLAY, CD_PATH_FORCE, (LPARAM)szFullPath);
    }

    // Strip filespec for tree control
    WCHAR szTreePath[MAXPATHLEN * 2];
    lstrcpyn(szTreePath, pszPath, COUNTOF(szTreePath));

    if (hwndTree) {
        SendMessage(hwndTree, TC_SETDRIVE, MAKEWORD(FALSE, FALSE), (LPARAM)szTreePath);
    }

    SPC_SET_HITDISK(qFreeSpace);
    UpdateStatus(hwndActive);
}

void UpdateToolbarState(HWND hwndActive) {
    if (!hwndToolbarCtrl)
        return;

    BOOL bEnable = FALSE;
    DWORD dwView = VIEW_NAMEONLY;
    DWORD dwSort = IDD_NAME;

    if (hwndActive && hwndActive != hwndSearch) {
        // It's a tree window - enable toolbar items and read state
        bEnable = TRUE;
        dwView = (DWORD)GetWindowLongPtr(hwndActive, GWL_VIEW) & VIEW_EVERYTHING;
        dwSort = (DWORD)GetWindowLongPtr(hwndActive, GWL_SORT);
    }

    // Update location combobox enable state
    if (hwndLocationCombo) {
        EnableWindow(hwndLocationCombo, bEnable);
    }

    // Update combobox to show the active window's full path
    if (hwndLocationCombo && hwndActive) {
        DRIVE drive = (DRIVE)GetWindowLongPtr(hwndActive, GWL_TYPE);
        if (drive == TYPE_SEARCH) {
            drive = (DRIVE)SendMessage(hwndSearch, FS_GETDRIVE, 0, 0L) - CHAR_A;
        }

        // Select the drive item so the correct drive icon shows
        for (int i = 0; i < cDrives; i++) {
            if (rgiDrive[i] == drive) {
                SendMessage(hwndLocationCombo, CB_SETCURSEL, i, 0);
                break;
            }
        }

        // Override the edit text with the full directory path
        WCHAR szPath[MAXPATHLEN];
        SendMessage(hwndActive, FS_GETDIRECTORY, MAXPATHLEN, (LPARAM)szPath);
        StripBackslash(szPath);
        HWND hwndEdit = (HWND)SendMessage(hwndLocationCombo, CBEM_GETEDITCONTROL, 0, 0);
        if (hwndEdit) {
            SetWindowTextW(hwndEdit, szPath);
        }
    }

    // Update view radio group
    SendMessage(hwndToolbarCtrl, TB_CHECKBUTTON, IDM_VNAME, MAKELONG(dwView == VIEW_NAMEONLY, 0));
    SendMessage(hwndToolbarCtrl, TB_CHECKBUTTON, IDM_VDETAILS, MAKELONG(dwView == VIEW_EVERYTHING, 0));

    // Update sort radio group
    SendMessage(hwndToolbarCtrl, TB_CHECKBUTTON, IDM_BYNAME, MAKELONG(dwSort == IDD_NAME, 0));
    SendMessage(hwndToolbarCtrl, TB_CHECKBUTTON, IDM_BYTYPE, MAKELONG(dwSort == IDD_TYPE, 0));
    SendMessage(hwndToolbarCtrl, TB_CHECKBUTTON, IDM_BYSIZE, MAKELONG(dwSort == IDD_SIZE, 0));
    SendMessage(hwndToolbarCtrl, TB_CHECKBUTTON, IDM_BYDATE, MAKELONG(dwSort == IDD_DATE, 0));
    SendMessage(hwndToolbarCtrl, TB_CHECKBUTTON, IDM_BYFDATE, MAKELONG(dwSort == IDD_FDATE, 0));

    // Enable/disable view and sort buttons
    for (int i = 0; i < (int)NUM_TOOLBAR_BUTTONS; i++) {
        if (toolbarButtons[i].idm && toolbarButtons[i].group > 0) {
            SendMessage(hwndToolbarCtrl, TB_ENABLEBUTTON, toolbarButtons[i].idm, MAKELONG(bEnable, 0));
        }
    }
}

void RefreshToolbarDriveList() {
    if (hwndLocationCombo) {
        UINT dpi = GetDpiForWindow(hwndDriveBar);
        CreateDriveImageList(dpi);
        SendMessage(hwndLocationCombo, CBEM_SETIMAGELIST, 0, (LPARAM)himlDriveIcons);
        PopulateLocationCombo();
    }
}

static LRESULT CALLBACK
ToolbarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, ToolbarSubclassProc, uIdSubclass);
        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    LRESULT result = DefSubclassProc(hWnd, uMsg, wParam, lParam);

    if (uMsg == WM_PAINT) {
        // Paint over separator areas to hide the default vertical line.
        int count = (int)SendMessage(hWnd, TB_BUTTONCOUNT, 0, 0);
        HDC hdc = GetDC(hWnd);
        HBRUSH hbr = GetSysColorBrush(COLOR_BTNFACE);
        for (int i = 0; i < count; i++) {
            TBBUTTON tbb;
            SendMessage(hWnd, TB_GETBUTTON, i, (LPARAM)&tbb);
            if (tbb.fsStyle & TBSTYLE_SEP) {
                RECT rc;
                SendMessage(hWnd, TB_GETITEMRECT, i, (LPARAM)&rc);
                FillRect(hdc, &rc, hbr);
            }
        }
        ReleaseDC(hWnd, hdc);
    }

    return result;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  DrivesWndProc() - Toolbar window procedure                              */
/*                                                                          */
/*--------------------------------------------------------------------------*/

LRESULT
CALLBACK
DrivesWndProc(HWND hWnd, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    switch (wMsg) {
        case WM_CREATE: {
            UINT dpi = GetDpiForWindow(hWnd);
            ToolbarMetrics m = GetToolbarMetrics(dpi);

            // Compute icon size for toolbar buttons
            UINT iconCX, iconCY;
            PngGetScaledSize(dpi, PNG_TYPE_TOOLBAR, 0, &iconCX, &iconCY);
            if (iconCX == 0)
                iconCX = iconCY = ScaleValueForDpi(16, dpi);

            // Create the location combo box (ComboBoxEx for icon support)
            CreateDriveImageList(dpi);

            hwndLocationCombo = CreateWindowExW(
                0, WC_COMBOBOXEXW, NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL,
                m.leftMargin, 0, 100, m.comboDropdownHeight, hWnd, (HMENU)(INT_PTR)IDC_LOCATION_COMBO, hAppInstance,
                NULL);

            if (hwndLocationCombo) {
                hwndDriveList = hwndLocationCombo;  // keep global in sync for compatibility
                SendMessage(hwndLocationCombo, CBEM_SETIMAGELIST, 0, (LPARAM)himlDriveIcons);
                PopulateLocationCombo();
            }

            // Create the toolbar control for buttons
            hwndToolbarCtrl = CreateWindowExW(
                0, TOOLBARCLASSNAMEW, NULL,
                WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NORESIZE |
                    CCS_NOPARENTALIGN,
                0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)IDC_TOOLBAR_BUTTONS, hAppInstance, NULL);

            if (hwndToolbarCtrl) {
                SendMessage(hwndToolbarCtrl, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

                // Set button size with padding around the icon
                int btnWidth = iconCX + m.padding * 2;
                int btnHeight = iconCY + m.padding * 2;
                SendMessage(hwndToolbarCtrl, TB_SETBUTTONSIZE, 0, MAKELONG(btnWidth, btnHeight));
                SendMessage(hwndToolbarCtrl, TB_SETPADDING, 0, MAKELONG(m.padding, m.padding));

                // Create image list with rendered toolbar PNGs
                HIMAGELIST himlButtons = CreatePngImageList(dpi, PNG_TYPE_TOOLBAR, TBAR_IMG_COUNT, iconCX, iconCY);
                SendMessage(hwndToolbarCtrl, TB_SETIMAGELIST, 0, (LPARAM)himlButtons);

                // Add buttons
                TBBUTTON tbb[NUM_TOOLBAR_BUTTONS] = {};
                for (int i = 0; i < (int)NUM_TOOLBAR_BUTTONS; i++) {
                    if (toolbarButtons[i].style == TBSTYLE_SEP) {
                        tbb[i].iBitmap = m.sepWidth;
                        tbb[i].idCommand = 0;
                        tbb[i].fsState = 0;
                        tbb[i].fsStyle = TBSTYLE_SEP;
                    } else {
                        tbb[i].iBitmap = toolbarButtons[i].imgIndex;
                        tbb[i].idCommand = toolbarButtons[i].idm;
                        tbb[i].fsState = TBSTATE_ENABLED;
                        tbb[i].fsStyle = toolbarButtons[i].style;
                    }
                }
                SendMessage(hwndToolbarCtrl, TB_ADDBUTTONS, NUM_TOOLBAR_BUTTONS, (LPARAM)tbb);
                SendMessage(hwndToolbarCtrl, TB_AUTOSIZE, 0, 0);

                SendMessage(hwndToolbarCtrl, TB_AUTOSIZE, 0, 0);

                SetWindowSubclass(hwndToolbarCtrl, ToolbarSubclassProc, 0, 0);
            }

            break;
        }

        case WM_SIZE: {
            int cx = LOWORD(lParam);
            int cy = HIWORD(lParam);
            ToolbarMetrics m = GetToolbarMetrics(GetDpiForWindow(hWnd));

            // Get toolbar button area width and right-align it
            int toolbarWidth = 0;
            int toolbarHeight = 0;
            if (hwndToolbarCtrl) {
                RECT rcToolbar;
                SendMessage(hwndToolbarCtrl, TB_GETITEMRECT, NUM_TOOLBAR_BUTTONS - 1, (LPARAM)&rcToolbar);
                toolbarWidth = rcToolbar.right;
                toolbarHeight = rcToolbar.bottom;
                int toolbarX = cx - toolbarWidth - m.rightMargin;
                int toolbarY = max(0, (cy - toolbarHeight) / 2);
                SetWindowPos(hwndToolbarCtrl, NULL, toolbarX, toolbarY, toolbarWidth, toolbarHeight, SWP_NOZORDER);
            }

            // Stretch combobox to fill remaining space
            if (hwndLocationCombo) {
                int comboRight = cx - toolbarWidth - m.spacing;
                int comboWidth = max(60, comboRight - m.leftMargin);
                RECT rcCombo;
                GetWindowRect(hwndLocationCombo, &rcCombo);
                int comboH = rcCombo.bottom - rcCombo.top;
                int comboY = max(0, (cy - comboH) / 2);
                SetWindowPos(
                    hwndLocationCombo, NULL, m.leftMargin, comboY, comboWidth, m.comboDropdownHeight, SWP_NOZORDER);
            }
            break;
        }

        case WM_COMMAND: {
            WORD wNotifyCode = HIWORD(wParam);
            WORD wID = LOWORD(wParam);

            if ((HWND)lParam == hwndLocationCombo || wID == IDC_LOCATION_COMBO) {
                if (wNotifyCode == CBN_SELENDOK) {
                    int sel = (int)SendMessage(hwndLocationCombo, CB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel < cDrives) {
                        HWND hwndChild = (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L);
                        if (hwndChild && hwndChild != hwndSearch) {
                            int nDriveCurrent = (int)GetWindowLongPtr(hWnd, GWL_CURDRIVEIND);
                            DrivesSetDrive(hWnd, sel, nDriveCurrent, sel == nDriveCurrent);
                        }
                    }
                    return 0;
                }
            }

            // Forward toolbar button commands to the frame window
            if ((HWND)lParam == hwndToolbarCtrl) {
                switch (wID) {
                    case IDM_VNAME:
                    case IDM_VDETAILS:
                    case IDM_BYNAME:
                    case IDM_BYTYPE:
                    case IDM_BYSIZE:
                    case IDM_BYDATE:
                    case IDM_BYFDATE:
                    case IDM_NEWWINDOW:
                        PostMessage(hwndFrame, WM_COMMAND, wID, 0);
                        return 0;
                }
            }

            return DefWindowProc(hWnd, wMsg, wParam, lParam);
        }

        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;

            if (pnmh->hwndFrom == hwndToolbarCtrl) {
                if (pnmh->code == TBN_GETINFOTIPW) {
                    // Provide tooltips from the same string table as the menu help
                    LPNMTBGETINFOTIPW pTip = (LPNMTBGETINFOTIPW)lParam;
                    LoadStringW(hAppInstance, MH_MYITEMS + pTip->iItem, pTip->pszText, pTip->cchTextMax);
                    return 0;
                }
            }

            return DefWindowProc(hWnd, wMsg, wParam, lParam);
        }

        case FS_SETDRIVE:
            // wParam     the drive index to set
            // lParam if Non-Zero, don't steal on same drive!
            DrivesSetDrive(
                hWnd, (DRIVEIND)wParam, (int)GetWindowLongPtr(hWnd, GWL_CURDRIVEIND),
                lParam && (wParam == (WPARAM)GetWindowLongPtr(hWnd, GWL_CURDRIVEIND)));
            break;

        case FS_GETDRIVE: {
            int nDriveCurrent = (int)GetWindowLongPtr(hWnd, GWL_CURDRIVEIND);
            return rgiDrive[nDriveCurrent] + CHAR_A;
        }

        case WM_DESTROY:
            if (himlDriveIcons) {
                ImageList_Destroy(himlDriveIcons);
                himlDriveIcons = NULL;
            }
            // The toolbar image list is owned by the toolbar and destroyed with it,
            // but we need to be careful. Get it before the toolbar is destroyed.
            if (hwndToolbarCtrl) {
                HIMAGELIST himl = (HIMAGELIST)SendMessage(hwndToolbarCtrl, TB_GETIMAGELIST, 0, 0);
                if (himl)
                    ImageList_Destroy(himl);
            }
            hwndToolbarCtrl = NULL;
            hwndLocationCombo = NULL;
            hwndDriveList = NULL;
            break;

        default:
            return DefWindowProc(hWnd, wMsg, wParam, lParam);
    }

    return 0L;
}
