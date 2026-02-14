#include "libheirloom/pch.h"
#include "libheirloom/MdiDpiFixup.h"

namespace libheirloom {

enum class ButtonVisual { Normal, Pressed };

struct ButtonLayout {
    bool valid = false;
    RECT rcClose = {};     // screen coords
    RECT rcRestore = {};   // screen coords
    RECT rcMinimize = {};  // screen coords
};

static ButtonLayout getButtonLayout(HWND hwndFrame, HWND hwndMdiClient) {
    ButtonLayout layout;

    BOOL isMaximized = FALSE;
    HWND hwndActive = reinterpret_cast<HWND>(
        SendMessageW(hwndMdiClient, WM_MDIGETACTIVE, 0, reinterpret_cast<LPARAM>(&isMaximized)));
    if (!hwndActive || !isMaximized)
        return layout;

    MENUBARINFO mbi = {};
    mbi.cbSize = sizeof(mbi);
    if (!GetMenuBarInfo(hwndFrame, OBJID_MENU, 0, &mbi))
        return layout;

    UINT dpi = GetDpiForWindow(hwndFrame);
    int buttonWidth = GetSystemMetricsForDpi(SM_CXMENUSIZE, dpi);
    int menuBarHeight = mbi.rcBar.bottom - mbi.rcBar.top;

    layout.rcClose = { mbi.rcBar.right - buttonWidth, mbi.rcBar.top,
                       mbi.rcBar.right, mbi.rcBar.top + menuBarHeight };
    layout.rcRestore = { layout.rcClose.left - buttonWidth, mbi.rcBar.top,
                         layout.rcClose.left, mbi.rcBar.top + menuBarHeight };
    layout.rcMinimize = { layout.rcRestore.left - buttonWidth, mbi.rcBar.top,
                          layout.rcRestore.left, mbi.rcBar.top + menuBarHeight };
    layout.valid = true;
    return layout;
}

static RECT screenToWindow(const RECT& rc, const RECT& rcWindow) {
    return {
        rc.left - rcWindow.left, rc.top - rcWindow.top,
        rc.right - rcWindow.left, rc.bottom - rcWindow.top
    };
}

static void drawButton(HDC hdc, const RECT& rc, WCHAR glyph, HFONT hFont,
                        ButtonVisual visual, bool isActive) {
    // 1px border at rgb(229,229,229), always 1px regardless of DPI.
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(229, 229, 229));
    HBRUSH hBrush;
    if (visual == ButtonVisual::Pressed) {
        hBrush = CreateSolidBrush(RGB(204, 228, 247));
    } else {
        hBrush = CreateSolidBrush(RGB(255, 255, 255));
    }

    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
    HBRUSH hOldBrush = static_cast<HBRUSH>(SelectObject(hdc, hBrush));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBrush);
    DeleteObject(hPen);
    DeleteObject(hBrush);

    // Draw glyph centered.
    HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, hFont));
    SetTextColor(hdc, RGB(122, 136, 150));
    SetBkMode(hdc, TRANSPARENT);
    RECT rcText = rc;
    DrawTextW(hdc, &glyph, 1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, hOldFont);
}

static HFONT createGlyphFont(UINT dpi) {
    int glyphHeight = MulDiv(16, dpi, 96);
    return CreateFontW(
        glyphHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Marlett");
}

void redrawMdiMenuBarButtons(HWND hwndFrame, HWND hwndMdiClient) {
    auto layout = getButtonLayout(hwndFrame, hwndMdiClient);
    if (!layout.valid)
        return;

    RECT rcWindow = {};
    GetWindowRect(hwndFrame, &rcWindow);

    HDC hdc = GetWindowDC(hwndFrame);
    if (!hdc)
        return;

    UINT dpi = GetDpiForWindow(hwndFrame);
    HFONT hFont = createGlyphFont(dpi);
    if (hFont) {
        bool isActive = (hwndFrame == GetForegroundWindow());
        drawButton(hdc, screenToWindow(layout.rcClose, rcWindow), L'r', hFont, ButtonVisual::Normal, isActive);
        drawButton(hdc, screenToWindow(layout.rcRestore, rcWindow), L'2', hFont, ButtonVisual::Normal, isActive);
        drawButton(hdc, screenToWindow(layout.rcMinimize, rcWindow), L'0', hFont, ButtonVisual::Normal, isActive);
        DeleteObject(hFont);
    }

    ReleaseDC(hwndFrame, hdc);
}

enum class ButtonId { None, Minimize, Restore, Close };

static ButtonId hitTestButton(const ButtonLayout& layout, POINT ptScreen) {
    if (PtInRect(&layout.rcClose, ptScreen)) return ButtonId::Close;
    if (PtInRect(&layout.rcRestore, ptScreen)) return ButtonId::Restore;
    if (PtInRect(&layout.rcMinimize, ptScreen)) return ButtonId::Minimize;
    return ButtonId::None;
}

bool handleMdiMenuBarMouseDown(HWND hwndFrame, HWND hwndMdiClient, LPARAM lParam) {
    auto layout = getButtonLayout(hwndFrame, hwndMdiClient);
    if (!layout.valid)
        return false;

    POINT ptScreen = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    ButtonId hitButton = hitTestButton(layout, ptScreen);
    if (hitButton == ButtonId::None)
        return false;

    // Identify the hit button's rect, glyph, and syscommand.
    RECT rcButton;
    WCHAR glyph;
    UINT syscmd;
    switch (hitButton) {
        case ButtonId::Close:    rcButton = layout.rcClose;    glyph = L'r'; syscmd = SC_CLOSE;   break;
        case ButtonId::Restore:  rcButton = layout.rcRestore;  glyph = L'2'; syscmd = SC_RESTORE; break;
        case ButtonId::Minimize: rcButton = layout.rcMinimize; glyph = L'0'; syscmd = SC_MINIMIZE; break;
        default: return false;
    }

    UINT dpi = GetDpiForWindow(hwndFrame);
    HFONT hFont = createGlyphFont(dpi);
    if (!hFont)
        return false;

    // Draw the pressed state.
    RECT rcWindow = {};
    GetWindowRect(hwndFrame, &rcWindow);
    RECT rcButtonWin = screenToWindow(rcButton, rcWindow);

    HDC hdc = GetWindowDC(hwndFrame);
    if (hdc) {
        drawButton(hdc, rcButtonWin, glyph, hFont, ButtonVisual::Pressed, true);
        ReleaseDC(hwndFrame, hdc);
    }

    // Enter mouse tracking loop.
    SetCapture(hwndFrame);
    bool isOver = true;
    bool committed = false;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_MOUSEMOVE) {
            POINT pt = { GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam) };
            ClientToScreen(hwndFrame, &pt);
            bool nowOver = PtInRect(&rcButton, pt) != FALSE;
            if (nowOver != isOver) {
                isOver = nowOver;
                hdc = GetWindowDC(hwndFrame);
                if (hdc) {
                    GetWindowRect(hwndFrame, &rcWindow);
                    rcButtonWin = screenToWindow(rcButton, rcWindow);
                    drawButton(hdc, rcButtonWin, glyph, hFont,
                        isOver ? ButtonVisual::Pressed : ButtonVisual::Normal, true);
                    ReleaseDC(hwndFrame, hdc);
                }
            }
        } else if (msg.message == WM_LBUTTONUP) {
            POINT pt = { GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam) };
            ClientToScreen(hwndFrame, &pt);
            committed = PtInRect(&rcButton, pt) != FALSE;
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

    // Redraw all buttons in normal state.
    redrawMdiMenuBarButtons(hwndFrame, hwndMdiClient);

    if (committed) {
        BOOL isMax = FALSE;
        HWND hwndChild = reinterpret_cast<HWND>(
            SendMessageW(hwndMdiClient, WM_MDIGETACTIVE, 0, reinterpret_cast<LPARAM>(&isMax)));
        if (hwndChild && isMax) {
            PostMessageW(hwndChild, WM_SYSCOMMAND, syscmd, 0);
        }
    }

    return true;
}

} // namespace libheirloom
