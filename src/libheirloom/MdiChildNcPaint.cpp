#include "libheirloom/pch.h"
#include "libheirloom/MdiChildNcPaint.h"

#ifndef SM_CXPADDEDBORDERWIDTH
#define SM_CXPADDEDBORDERWIDTH 92
#endif

namespace libheirloom {

// --- Per-window state ---

enum class ButtonId { None, Minimize, MaxRestore, Close };

struct MdiChildNcState {
    bool isActive = false;
    ButtonId hoverButton = ButtonId::None;
};

static const WCHAR kPropName[] = L"MdiChildNcState";

static MdiChildNcState* getState(HWND hwnd) {
    return static_cast<MdiChildNcState*>(GetPropW(hwnd, kPropName));
}

static MdiChildNcState* getOrCreateState(HWND hwnd) {
    auto* state = getState(hwnd);
    if (!state) {
        state = new MdiChildNcState();
        state->isActive = (GetForegroundWindow() == GetParent(GetParent(hwnd)));
        SetPropW(hwnd, kPropName, static_cast<HANDLE>(state));
    }
    return state;
}

static void freeState(HWND hwnd) {
    auto* state = getState(hwnd);
    if (state) {
        RemovePropW(hwnd, kPropName);
        delete state;
    }
}

// --- Colors ---

static constexpr COLORREF kCaptionBg        = RGB(255, 255, 255);
static constexpr COLORREF kActiveBorder     = RGB(53, 121, 199);
static constexpr COLORREF kInactiveBorder   = RGB(192, 192, 192);
static constexpr COLORREF kActiveTitleText  = RGB(0, 0, 0);
static constexpr COLORREF kInactiveTitleText = RGB(160, 160, 160);
static constexpr COLORREF kButtonGlyph      = RGB(122, 136, 150);
static constexpr COLORREF kButtonHoverBg    = RGB(229, 241, 251);
static constexpr COLORREF kButtonPressedBg  = RGB(204, 228, 247);

// --- Button visuals ---

enum class ButtonVisual { Normal, Hover, Pressed };

// --- Layout helpers ---

struct NcLayout {
    UINT dpi;
    int borderX;      // SM_CXSIZEFRAME + SM_CXPADDEDBORDERWIDTH
    int borderY;      // SM_CYSIZEFRAME + SM_CXPADDEDBORDERWIDTH
    int captionH;     // SM_CYCAPTION
    int buttonW;      // SM_CXSIZE
    int iconW;        // SM_CXSMICON
    int iconH;        // SM_CYSMICON
    RECT rcWindow;    // window rect in screen coords
    int windowW;      // width of window
    int windowH;      // height of window
};

static NcLayout getNcLayout(HWND hwnd) {
    NcLayout l = {};
    l.dpi = GetDpiForWindow(hwnd);
    l.borderX = GetSystemMetricsForDpi(SM_CXSIZEFRAME, l.dpi)
              + GetSystemMetricsForDpi(SM_CXPADDEDBORDERWIDTH, l.dpi);
    l.borderY = GetSystemMetricsForDpi(SM_CYSIZEFRAME, l.dpi)
              + GetSystemMetricsForDpi(SM_CXPADDEDBORDERWIDTH, l.dpi);
    l.captionH = GetSystemMetricsForDpi(SM_CYCAPTION, l.dpi);
    l.buttonW = GetSystemMetricsForDpi(SM_CXSIZE, l.dpi);
    l.iconW = GetSystemMetricsForDpi(SM_CXSMICON, l.dpi);
    l.iconH = GetSystemMetricsForDpi(SM_CYSMICON, l.dpi);
    GetWindowRect(hwnd, &l.rcWindow);
    l.windowW = l.rcWindow.right - l.rcWindow.left;
    l.windowH = l.rcWindow.bottom - l.rcWindow.top;
    return l;
}

// Button rects in window-relative coordinates (top-right of caption area).
static RECT getButtonRect(const NcLayout& l, int index) {
    // index 0=close (rightmost), 1=max/restore, 2=minimize
    int right = l.windowW - l.borderX - index * l.buttonW;
    int left = right - l.buttonW;
    int top = l.borderY;
    int bottom = top + l.captionH - 1;
    return { left, top, right, bottom };
}

// --- GDI helpers ---

static HFONT createGlyphFont(UINT dpi) {
    int glyphHeight = MulDiv(16, dpi, 96);
    return CreateFontW(
        glyphHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Marlett");
}

static HFONT createCaptionFont(UINT dpi) {
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi);
    return CreateFontIndirectW(&ncm.lfCaptionFont);
}

static void drawButton(HDC hdc, const RECT& rc, WCHAR glyph, HFONT hFont, ButtonVisual visual) {
    HBRUSH hBrush;
    switch (visual) {
        case ButtonVisual::Pressed: hBrush = CreateSolidBrush(kButtonPressedBg); break;
        case ButtonVisual::Hover:   hBrush = CreateSolidBrush(kButtonHoverBg);   break;
        default:                    hBrush = CreateSolidBrush(kCaptionBg);       break;
    }
    FillRect(hdc, &rc, hBrush);
    DeleteObject(hBrush);

    // Draw glyph centered.
    HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, hFont));
    SetTextColor(hdc, kButtonGlyph);
    SetBkMode(hdc, TRANSPARENT);
    RECT rcText = rc;
    DrawTextW(hdc, &glyph, 1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, hOldFont);
}

// --- Painting helpers ---

static void getClientAreaInWindowCoords(HWND hwnd, const NcLayout& l,
                                        int& clientLeft, int& clientTop,
                                        int& clientW, int& clientH) {
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    POINT ptClientOrigin = { 0, 0 };
    ClientToScreen(hwnd, &ptClientOrigin);
    clientLeft = ptClientOrigin.x - l.rcWindow.left;
    clientTop = ptClientOrigin.y - l.rcWindow.top;
    clientW = rcClient.right - rcClient.left;
    clientH = rcClient.bottom - rcClient.top;
}

static void paintNcToDC(HDC hdc, HWND hwnd, const MdiChildNcState* state, const NcLayout& l) {
    // Fill entire NC area with white.
    RECT rcFull = { 0, 0, l.windowW, l.windowH };
    HBRUSH hWhite = CreateSolidBrush(kCaptionBg);
    FillRect(hdc, &rcFull, hWhite);
    DeleteObject(hWhite);

    COLORREF borderColor = state->isActive ? kActiveBorder : kInactiveBorder;
    HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
    HBRUSH hOldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));

    // 1px border at outer edge.
    Rectangle(hdc, 0, 0, l.windowW, l.windowH);

    // 1px border at inner edge (around client area).
    int clientLeft, clientTop, clientW, clientH;
    getClientAreaInWindowCoords(hwnd, l, clientLeft, clientTop, clientW, clientH);
    Rectangle(hdc, clientLeft - 1, clientTop - 1, clientLeft + clientW + 1, clientTop + clientH + 1);

    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBrush);
    DeleteObject(hPen);

    // Icon.
    HICON hIcon = reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0));
    if (!hIcon)
        hIcon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM));
    int iconX = l.borderX + 2;
    int iconY = l.borderY + (l.captionH - l.iconH) / 2;
    if (hIcon)
        DrawIconEx(hdc, iconX, iconY, hIcon, l.iconW, l.iconH, 0, nullptr, DI_NORMAL);

    // Title text.
    WCHAR title[256] = {};
    GetWindowTextW(hwnd, title, 256);
    HFONT hCaptionFont = createCaptionFont(l.dpi);
    if (hCaptionFont) {
        HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, hCaptionFont));
        SetTextColor(hdc, state->isActive ? kActiveTitleText : kInactiveTitleText);
        SetBkMode(hdc, TRANSPARENT);
        int textLeft = iconX + l.iconW + MulDiv(4, l.dpi, 96);
        int textRight = getButtonRect(l, 2).left - MulDiv(4, l.dpi, 96);
        RECT rcText = { textLeft, l.borderY, textRight, l.borderY + l.captionH };
        DrawTextW(hdc, title, -1, &rcText,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(hdc, hOldFont);
        DeleteObject(hCaptionFont);
    }

    // Buttons.
    HFONT hGlyphFont = createGlyphFont(l.dpi);
    if (hGlyphFont) {
        bool isZoomed = IsZoomed(hwnd) != FALSE;

        RECT rcClose = getButtonRect(l, 0);
        RECT rcMaxRestore = getButtonRect(l, 1);
        RECT rcMinimize = getButtonRect(l, 2);

        ButtonVisual closeVis = (state->hoverButton == ButtonId::Close) ? ButtonVisual::Hover : ButtonVisual::Normal;
        ButtonVisual maxVis = (state->hoverButton == ButtonId::MaxRestore) ? ButtonVisual::Hover : ButtonVisual::Normal;
        ButtonVisual minVis = (state->hoverButton == ButtonId::Minimize) ? ButtonVisual::Hover : ButtonVisual::Normal;

        drawButton(hdc, rcClose, L'r', hGlyphFont, closeVis);
        drawButton(hdc, rcMaxRestore, isZoomed ? L'2' : L'1', hGlyphFont, maxVis);
        drawButton(hdc, rcMinimize, L'0', hGlyphFont, minVis);

        DeleteObject(hGlyphFont);
    }
}

// --- Painting ---

// Full NC repaint, double-buffered to prevent flicker.
static void paintNonClientArea(HWND hwnd) {
    auto* state = getState(hwnd);
    if (!state) return;

    NcLayout l = getNcLayout(hwnd);
    HDC hdcWindow = GetWindowDC(hwnd);
    if (!hdcWindow) return;

    // Double-buffer: paint to memory DC, then blit to window DC.
    HDC hdcMem = CreateCompatibleDC(hdcWindow);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcWindow, l.windowW, l.windowH);
    HBITMAP hOldBitmap = static_cast<HBITMAP>(SelectObject(hdcMem, hBitmap));

    paintNcToDC(hdcMem, hwnd, state, l);

    // Exclude client area from the blit target so we don't touch it.
    int clientLeft, clientTop, clientW, clientH;
    getClientAreaInWindowCoords(hwnd, l, clientLeft, clientTop, clientW, clientH);
    ExcludeClipRect(hdcWindow, clientLeft, clientTop, clientLeft + clientW, clientTop + clientH);

    BitBlt(hdcWindow, 0, 0, l.windowW, l.windowH, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWindow);
}

// Repaint only the caption buttons (for hover changes -- avoids redrawing title/icon).
static void paintButtons(HWND hwnd) {
    auto* state = getState(hwnd);
    if (!state) return;

    NcLayout l = getNcLayout(hwnd);
    HDC hdc = GetWindowDC(hwnd);
    if (!hdc) return;

    HFONT hGlyphFont = createGlyphFont(l.dpi);
    if (hGlyphFont) {
        bool isZoomed = IsZoomed(hwnd) != FALSE;

        RECT rcClose = getButtonRect(l, 0);
        RECT rcMaxRestore = getButtonRect(l, 1);
        RECT rcMinimize = getButtonRect(l, 2);

        ButtonVisual closeVis = (state->hoverButton == ButtonId::Close) ? ButtonVisual::Hover : ButtonVisual::Normal;
        ButtonVisual maxVis = (state->hoverButton == ButtonId::MaxRestore) ? ButtonVisual::Hover : ButtonVisual::Normal;
        ButtonVisual minVis = (state->hoverButton == ButtonId::Minimize) ? ButtonVisual::Hover : ButtonVisual::Normal;

        drawButton(hdc, rcClose, L'r', hGlyphFont, closeVis);
        drawButton(hdc, rcMaxRestore, isZoomed ? L'2' : L'1', hGlyphFont, maxVis);
        drawButton(hdc, rcMinimize, L'0', hGlyphFont, minVis);

        DeleteObject(hGlyphFont);
    }

    ReleaseDC(hwnd, hdc);
}

// --- Hit testing ---

static LRESULT ncHitTest(HWND hwnd, int x, int y) {
    NcLayout l = getNcLayout(hwnd);

    // Convert screen coords to window-relative.
    int wx = x - l.rcWindow.left;
    int wy = y - l.rcWindow.top;

    // Resize edges/corners (using full border width).
    int cornerSize = l.borderX * 2;

    bool onLeft   = (wx < l.borderX);
    bool onRight  = (wx >= l.windowW - l.borderX);
    bool onTop    = (wy < l.borderY);
    bool onBottom = (wy >= l.windowH - l.borderY);

    if (onTop && wx < cornerSize)                     return HTTOPLEFT;
    if (onTop && wx >= l.windowW - cornerSize)        return HTTOPRIGHT;
    if (onBottom && wx < cornerSize)                  return HTBOTTOMLEFT;
    if (onBottom && wx >= l.windowW - cornerSize)     return HTBOTTOMRIGHT;
    if (onLeft)                                        return HTLEFT;
    if (onRight)                                       return HTRIGHT;
    if (onTop)                                         return HTTOP;
    if (onBottom)                                      return HTBOTTOM;

    // Caption area?
    if (wy < l.borderY + l.captionH) {
        // Check buttons (right side).
        RECT rcClose = getButtonRect(l, 0);
        RECT rcMaxRestore = getButtonRect(l, 1);
        RECT rcMinimize = getButtonRect(l, 2);

        if (wx >= rcClose.left && wx < rcClose.right && wy >= rcClose.top && wy < rcClose.bottom)
            return HTCLOSE;
        if (wx >= rcMaxRestore.left && wx < rcMaxRestore.right && wy >= rcMaxRestore.top && wy < rcMaxRestore.bottom)
            return HTMAXBUTTON;
        if (wx >= rcMinimize.left && wx < rcMinimize.right && wy >= rcMinimize.top && wy < rcMinimize.bottom)
            return HTMINBUTTON;

        // Icon area (left side) = system menu.
        int iconRight = l.borderX + 2 + l.iconW + MulDiv(2, l.dpi, 96);
        if (wx < iconRight)
            return HTSYSMENU;

        return HTCAPTION;
    }

    return HTCLIENT;
}

// --- Button tracking for WM_NCLBUTTONDOWN ---

static void handleButtonDown(HWND hwnd, ButtonId hitButton) {
    NcLayout l = getNcLayout(hwnd);

    RECT rcButton;
    WCHAR glyph;
    UINT syscmd;
    bool isZoomed = IsZoomed(hwnd) != FALSE;
    switch (hitButton) {
        case ButtonId::Close:      rcButton = getButtonRect(l, 0); glyph = L'r';                    syscmd = SC_CLOSE;   break;
        case ButtonId::MaxRestore: rcButton = getButtonRect(l, 1); glyph = isZoomed ? L'2' : L'1';  syscmd = isZoomed ? SC_RESTORE : SC_MAXIMIZE; break;
        case ButtonId::Minimize:   rcButton = getButtonRect(l, 2); glyph = L'0';                    syscmd = SC_MINIMIZE; break;
        default: return;
    }

    // Convert button rect to screen coords for PtInRect.
    RECT rcScreen = {
        rcButton.left + l.rcWindow.left,   rcButton.top + l.rcWindow.top,
        rcButton.right + l.rcWindow.left,  rcButton.bottom + l.rcWindow.top
    };

    HFONT hFont = createGlyphFont(l.dpi);
    if (!hFont) return;

    // Draw pressed state.
    HDC hdc = GetWindowDC(hwnd);
    if (hdc) {
        drawButton(hdc, rcButton, glyph, hFont, ButtonVisual::Pressed);
        ReleaseDC(hwnd, hdc);
    }

    // Enter mouse tracking loop.
    SetCapture(hwnd);
    bool isOver = true;
    bool committed = false;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_MOUSEMOVE) {
            POINT pt = { GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam) };
            ClientToScreen(hwnd, &pt);
            bool nowOver = PtInRect(&rcScreen, pt) != FALSE;
            if (nowOver != isOver) {
                isOver = nowOver;
                hdc = GetWindowDC(hwnd);
                if (hdc) {
                    // Re-fetch layout in case window moved.
                    NcLayout l2 = getNcLayout(hwnd);
                    RECT rcBtn2;
                    switch (hitButton) {
                        case ButtonId::Close:      rcBtn2 = getButtonRect(l2, 0); break;
                        case ButtonId::MaxRestore: rcBtn2 = getButtonRect(l2, 1); break;
                        case ButtonId::Minimize:   rcBtn2 = getButtonRect(l2, 2); break;
                        default: rcBtn2 = {}; break;
                    }
                    drawButton(hdc, rcBtn2, glyph, hFont,
                        isOver ? ButtonVisual::Pressed : ButtonVisual::Normal);
                    ReleaseDC(hwnd, hdc);
                }
            }
        } else if (msg.message == WM_LBUTTONUP) {
            POINT pt = { GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam) };
            ClientToScreen(hwnd, &pt);
            committed = PtInRect(&rcScreen, pt) != FALSE;
            break;
        } else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            break;
        } else if (msg.message == WM_CAPTURECHANGED) {
            break;
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    ReleaseCapture();
    DeleteObject(hFont);

    // Repaint buttons to clear pressed state.
    paintButtons(hwnd);

    if (committed) {
        PostMessageW(hwnd, WM_SYSCOMMAND, syscmd, 0);
    }
}

// --- Message dispatcher ---

NcMessageResult handleMdiChildNcMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // When maximized, the child's NC area is hidden. Let DefMDIChildProc handle everything.
    if (IsZoomed(hwnd) && msg != WM_NCDESTROY && msg != WM_MDIACTIVATE)
        return { false, 0 };

    switch (msg) {
        case WM_NCPAINT: {
            getOrCreateState(hwnd);
            paintNonClientArea(hwnd);
            return { true, 0 };
        }

        case WM_NCACTIVATE: {
            auto* state = getOrCreateState(hwnd);
            state->isActive = (wParam != FALSE);
            // Call DefMDIChildProc with lParam=-1 to suppress default paint
            // while preserving MDI state tracking.
            LRESULT result = DefMDIChildProcW(hwnd, msg, wParam, -1);
            paintNonClientArea(hwnd);
            return { true, result };
        }

        case WM_NCHITTEST: {
            getOrCreateState(hwnd);
            LRESULT ht = ncHitTest(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return { true, ht };
        }

        case WM_NCLBUTTONDOWN: {
            getOrCreateState(hwnd);
            ButtonId hitButton = ButtonId::None;
            switch (wParam) {
                case HTCLOSE:     hitButton = ButtonId::Close; break;
                case HTMAXBUTTON: hitButton = ButtonId::MaxRestore; break;
                case HTMINBUTTON: hitButton = ButtonId::Minimize; break;
            }
            if (hitButton != ButtonId::None) {
                handleButtonDown(hwnd, hitButton);
                return { true, 0 };
            }
            // HTCAPTION, HTSYSMENU, resize: let DefMDIChildProc handle.
            return { false, 0 };
        }

        case WM_NCMOUSEMOVE: {
            auto* state = getOrCreateState(hwnd);
            ButtonId newHover = ButtonId::None;
            switch (wParam) {
                case HTCLOSE:     newHover = ButtonId::Close; break;
                case HTMAXBUTTON: newHover = ButtonId::MaxRestore; break;
                case HTMINBUTTON: newHover = ButtonId::Minimize; break;
            }
            if (newHover != state->hoverButton) {
                state->hoverButton = newHover;
                paintButtons(hwnd);
            }
            // Request TME_LEAVE | TME_NONCLIENT to get WM_NCMOUSELEAVE.
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE | TME_NONCLIENT;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            return { true, 0 };
        }

        case WM_NCMOUSELEAVE: {
            auto* state = getState(hwnd);
            if (state && state->hoverButton != ButtonId::None) {
                state->hoverButton = ButtonId::None;
                paintButtons(hwnd);
            }
            return { true, 0 };
        }

        case WM_SETTEXT: {
            // Use DefWindowProc (not DefMDIChildProc) to store text without repainting.
            LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
            paintNonClientArea(hwnd);
            return { true, result };
        }

        case WM_MDIACTIVATE: {
            auto* state = getOrCreateState(hwnd);
            state->isActive = (reinterpret_cast<HWND>(lParam) == hwnd);
            paintNonClientArea(hwnd);
            // Still needs DefMDIChildProc for MDI state.
            return { false, 0 };
        }

        case WM_NCDESTROY: {
            freeState(hwnd);
            return { false, 0 };
        }
    }

    return { false, 0 };
}

} // namespace libheirloom
