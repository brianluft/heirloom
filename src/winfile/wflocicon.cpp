#include "winfile.h"
#include "wflocicon.h"
#include "wfpng.h"
#include "wfdrop.h"
#include "wfdragsrc.h"
#include "wfcopy.h"
#include "wfutil.h"
#include "wfdpi.h"
#include "stringconstants.h"
#include <commctrl.h>
#include <ole2.h>
#include <shlobj.h>

const WCHAR kLocationIconClass[] = L"WFLocationIcon";

#define IDC_LOCATION_ICON 3012

// Per-window state
struct LocationIconState {
    WCHAR currentPath[MAXPATHLEN];
    PNG_TYPE iconType;
    int iconIndex;
    BOOL tracking;
    POINT trackStart;
    HWND hwndTooltip;
};

// Forward declarations
static BOOL IsLocationRootDirectory(LPCWSTR pPath);
static BOOL QueryDataOk(IDataObject* pDataObject);
static DWORD ComputeDropEffect(DWORD grfKeyState, DWORD dwAllowed);

//--------------------------------------------------------------------------
// LocationIconDropTarget - IDropTarget for the icon window
//--------------------------------------------------------------------------

class LocationIconDropTarget : public IDropTarget {
   public:
    LocationIconDropTarget(HWND hwnd);
    virtual ~LocationIconDropTarget() = default;

    void SetCurrentPath(LPCWSTR pszPath);

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pdo, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* pdo, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;

   private:
    LONG m_refCount;
    HWND m_hwnd;
    BOOL m_allowDrop;
    BOOL m_highlighted;
    WCHAR m_destPath[MAXPATHLEN];

    void PaintHighlight(BOOL show);
    void ExecuteDrop(IDataObject* pdo, DWORD effect);
};

LocationIconDropTarget::LocationIconDropTarget(HWND hwnd) {
    m_refCount = 1;
    m_hwnd = hwnd;
    m_allowDrop = FALSE;
    m_highlighted = FALSE;
    m_destPath[0] = L'\0';
}

void LocationIconDropTarget::SetCurrentPath(LPCWSTR pszPath) {
    lstrcpyn(m_destPath, pszPath, COUNTOF(m_destPath));
}

HRESULT STDMETHODCALLTYPE LocationIconDropTarget::QueryInterface(REFIID riid, void** ppv) {
    *ppv = NULL;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDropTarget)) {
        AddRef();
        *ppv = this;
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE LocationIconDropTarget::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE LocationIconDropTarget::Release() {
    LONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) {
        delete this;
        return 0;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE
LocationIconDropTarget::DragEnter(IDataObject* pdo, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    m_allowDrop = (m_destPath[0] != L'\0') && QueryDataOk(pdo);
    if (m_allowDrop) {
        *pdwEffect = ComputeDropEffect(grfKeyState, *pdwEffect);
        PaintHighlight(TRUE);
    } else {
        *pdwEffect = DROPEFFECT_NONE;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE LocationIconDropTarget::DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    if (m_allowDrop) {
        *pdwEffect = ComputeDropEffect(grfKeyState, *pdwEffect);
    } else {
        *pdwEffect = DROPEFFECT_NONE;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE LocationIconDropTarget::DragLeave() {
    PaintHighlight(FALSE);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
LocationIconDropTarget::Drop(IDataObject* pdo, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    PaintHighlight(FALSE);
    if (m_allowDrop) {
        *pdwEffect = ComputeDropEffect(grfKeyState, *pdwEffect);
        ExecuteDrop(pdo, *pdwEffect);
    } else {
        *pdwEffect = DROPEFFECT_NONE;
    }
    return S_OK;
}

void LocationIconDropTarget::PaintHighlight(BOOL show) {
    if (show == m_highlighted)
        return;
    m_highlighted = show;

    if (show) {
        HDC hdc = GetDC(m_hwnd);
        if (hdc) {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            HBRUSH hbr = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
            if (hbr) {
                FrameRect(hdc, &rc, hbr);
                DeleteObject(hbr);
            }
            ReleaseDC(m_hwnd, hdc);
        }
    } else {
        InvalidateRect(m_hwnd, NULL, TRUE);
    }
}

void LocationIconDropTarget::ExecuteDrop(IDataObject* pdo, DWORD effect) {
    LPWSTR szFiles = QuotedDropList(pdo);
    if (!szFiles)
        szFiles = QuotedContentList(pdo);
    if (!szFiles)
        return;

    // Self-drop check: if dragging the current directory onto itself, do nothing.
    // The drag source produces a quoted path like "C:\foo", so extract it and compare.
    WCHAR szFirstFile[MAXPATHLEN];
    LPWSTR pTmp = szFiles;
    if (GetNextFile(pTmp, szFirstFile, COUNTOF(szFirstFile))) {
        QualifyPath(szFirstFile);
        if (lstrcmpi(szFirstFile, m_destPath) == 0) {
            LocalFree((HLOCAL)szFiles);
            return;
        }
    }

    WCHAR szDest[MAXPATHLEN * 2];
    lstrcpyn(szDest, m_destPath, COUNTOF(szDest));
    AddBackslash(szDest);
    lstrcat(szDest, kStarDotStar);
    CheckEsc(szDest);

    DMMoveCopyHelper(szFiles, szDest, effect == DROPEFFECT_COPY);
    LocalFree((HLOCAL)szFiles);
}

//--------------------------------------------------------------------------
// Helper functions
//--------------------------------------------------------------------------

static BOOL IsLocationRootDirectory(LPCWSTR pPath) {
    // "C:" or "C:\" is a root
    if (pPath[0] && pPath[1] == L':') {
        if (pPath[2] == L'\0')
            return TRUE;
        if (pPath[2] == L'\\' && pPath[3] == L'\0')
            return TRUE;
    }
    return FALSE;
}

static BOOL QueryDataOk(IDataObject* pDataObject) {
    FORMATETC fmtetc = { CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (pDataObject->QueryGetData(&fmtetc) == S_OK)
        return TRUE;

    CLIPFORMAT cfDesc = RegisterClipboardFormat(CFSTR_FILEDESCRIPTOR);
    FORMATETC fmtDesc = { cfDesc, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (pDataObject->QueryGetData(&fmtDesc) == S_OK)
        return TRUE;

    CLIPFORMAT cfDescW = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW);
    FORMATETC fmtDescW = { cfDescW, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (pDataObject->QueryGetData(&fmtDescW) == S_OK)
        return TRUE;

    return FALSE;
}

static DWORD ComputeDropEffect(DWORD grfKeyState, DWORD dwAllowed) {
    DWORD dwEffect = 0;

    if (dwAllowed & DROPEFFECT_COPY)
        dwEffect = DROPEFFECT_COPY;
    if (dwAllowed & DROPEFFECT_MOVE)
        dwEffect = DROPEFFECT_MOVE;

    if (grfKeyState & MK_CONTROL)
        dwEffect = dwAllowed & DROPEFFECT_COPY;
    else if (grfKeyState & MK_SHIFT)
        dwEffect = dwAllowed & DROPEFFECT_MOVE;
    else if (grfKeyState & MK_ALT)
        dwEffect = dwAllowed & DROPEFFECT_LINK;

    if (dwEffect == 0)
        dwEffect = DROPEFFECT_NONE;

    return dwEffect;
}

//--------------------------------------------------------------------------
// LocationIconWndProc
//--------------------------------------------------------------------------

static LocationIconDropTarget* GetDropTarget(HWND hwnd) {
    return (LocationIconDropTarget*)GetWindowLongPtr(hwnd, sizeof(LONG_PTR));
}

LRESULT CALLBACK LocationIconWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            LocationIconState* pState =
                (LocationIconState*)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(LocationIconState));
            if (!pState)
                return -1;

            pState->iconType = PNG_TYPE_ICON;
            pState->iconIndex = BM_IND_CLOSE;

            SetWindowLongPtr(hwnd, 0, (LONG_PTR)pState);

            // Create tooltip
            pState->hwndTooltip = CreateWindowExW(
                WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, NULL, hAppInstance, NULL);
            if (pState->hwndTooltip) {
                TOOLINFOW ti = {};
                ti.cbSize = sizeof(ti);
                ti.uFlags = TTF_SUBCLASS;
                ti.hwnd = hwnd;
                ti.uId = (UINT_PTR)hwnd;
                ti.hinst = hAppInstance;
                ti.lpszText = const_cast<LPWSTR>(L"Drag this icon or drop files here");
                GetClientRect(hwnd, &ti.rect);
                SendMessageW(pState->hwndTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            }

            // Create and register drop target
            LocationIconDropTarget* pDropTarget = new LocationIconDropTarget(hwnd);
            SetWindowLongPtr(hwnd, sizeof(LONG_PTR), (LONG_PTR)pDropTarget);
            CoLockObjectExternal(pDropTarget, TRUE, FALSE);
            RegisterDragDrop(hwnd, pDropTarget);

            return 0;
        }

        case WM_DESTROY: {
            // Unregister drop target
            LocationIconDropTarget* pDropTarget = GetDropTarget(hwnd);
            if (pDropTarget) {
                RevokeDragDrop(hwnd);
                CoLockObjectExternal(pDropTarget, FALSE, TRUE);
                pDropTarget->Release();
                SetWindowLongPtr(hwnd, sizeof(LONG_PTR), 0);
            }

            LocationIconState* pState = (LocationIconState*)GetWindowLongPtr(hwnd, 0);
            if (pState) {
                if (pState->hwndTooltip)
                    DestroyWindow(pState->hwndTooltip);
                LocalFree((HLOCAL)pState);
                SetWindowLongPtr(hwnd, 0, 0);
            }
            break;
        }

        case WM_ERASEBKGND:
            return 1;  // we handle painting entirely in WM_PAINT

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

            LocationIconState* pState = (LocationIconState*)GetWindowLongPtr(hwnd, 0);
            if (pState && pState->currentPath[0]) {
                UINT dpi = GetDpiForWindow(hwnd);
                UINT cx, cy;
                PngGetScaledSize(dpi, pState->iconType, pState->iconIndex, &cx, &cy);
                int x = ((rc.right - rc.left) - (int)cx) / 2;
                int y = ((rc.bottom - rc.top) - (int)cy) / 2;
                PngDraw(hdc, dpi, x, y, pState->iconType, pState->iconIndex);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            LocationIconState* pState = (LocationIconState*)GetWindowLongPtr(hwnd, 0);
            if (pState && pState->currentPath[0]) {
                pState->tracking = TRUE;
                pState->trackStart.x = GET_X_LPARAM(lParam);
                pState->trackStart.y = GET_Y_LPARAM(lParam);
                ClientToScreen(hwnd, &pState->trackStart);
                SetCapture(hwnd);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            LocationIconState* pState = (LocationIconState*)GetWindowLongPtr(hwnd, 0);
            if (!pState || !pState->tracking)
                break;

            POINT ptNow = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &ptNow);

            int dx = abs(ptNow.x - pState->trackStart.x);
            int dy = abs(ptNow.y - pState->trackStart.y);
            if (dx <= GetSystemMetrics(SM_CXDRAG) && dy <= GetSystemMetrics(SM_CYDRAG))
                break;

            pState->tracking = FALSE;
            ReleaseCapture();

            // Build quoted path string for drag
            WCHAR szQuoted[MAXPATHLEN + 4];
            StringCchPrintfW(szQuoted, COUNTOF(szQuoted), L"\"%s\"", pState->currentPath);

            DWORD dwEffect = 0;
            WFDoDragDrop(hwnd, szQuoted, pState->trackStart, &dwEffect);
            return 0;
        }

        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED: {
            LocationIconState* pState = (LocationIconState*)GetWindowLongPtr(hwnd, 0);
            if (pState)
                pState->tracking = FALSE;
            if (uMsg == WM_LBUTTONUP)
                ReleaseCapture();
            return 0;
        }

        case WM_SIZE: {
            // Update tooltip rect when the window is resized
            LocationIconState* pState = (LocationIconState*)GetWindowLongPtr(hwnd, 0);
            if (pState && pState->hwndTooltip) {
                TOOLINFOW ti = {};
                ti.cbSize = sizeof(ti);
                ti.hwnd = hwnd;
                ti.uId = (UINT_PTR)hwnd;
                GetClientRect(hwnd, &ti.rect);
                SendMessageW(pState->hwndTooltip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
            }
            break;
        }

        case LIM_SETPATH: {
            LPCWSTR pszPath = (LPCWSTR)lParam;
            LocationIconState* pState = (LocationIconState*)GetWindowLongPtr(hwnd, 0);
            if (!pState)
                return 0;

            lstrcpyn(pState->currentPath, pszPath, COUNTOF(pState->currentPath));

            // Update drop target path
            LocationIconDropTarget* pDropTarget = GetDropTarget(hwnd);
            if (pDropTarget)
                pDropTarget->SetCurrentPath(pszPath);

            // Determine which icon to show
            if (pszPath[0] && IsLocationRootDirectory(pszPath)) {
                DRIVE drive = (DRIVE)(towupper(pszPath[0]) - L'A');
                if (drive >= 0 && drive < 26) {
                    pState->iconType = PNG_TYPE_DRIVE;
                    pState->iconIndex = aDriveInfo[drive].iOffset;
                } else {
                    pState->iconType = PNG_TYPE_ICON;
                    pState->iconIndex = BM_IND_OPEN;
                }
            } else if (pszPath[0]) {
                pState->iconType = PNG_TYPE_ICON;
                pState->iconIndex = BM_IND_OPEN;
            } else {
                pState->iconType = PNG_TYPE_ICON;
                pState->iconIndex = BM_IND_CLOSE;
            }

            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

//--------------------------------------------------------------------------
// Public API
//--------------------------------------------------------------------------

HWND CreateLocationIcon(HWND hParent, HINSTANCE hInst) {
    return CreateWindowExW(
        0, kLocationIconClass, NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hParent, (HMENU)(INT_PTR)IDC_LOCATION_ICON,
        hInst, NULL);
}

void UpdateLocationIcon(HWND hwndIcon, LPCWSTR pszCurrentPath) {
    if (hwndIcon)
        SendMessage(hwndIcon, LIM_SETPATH, 0, (LPARAM)pszCurrentPath);
}
