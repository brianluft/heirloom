#include "libheirloom/pch.h"
#include "libheirloom/MinimizedWindowListControl.h"
#include "libheirloom/window_data.h"

#include <algorithm>

namespace libheirloom {

// Using system metrics instead of hard-coded pixel values
static int getDpiScaledValue(HWND hwnd, int value) {
    UINT dpi = GetDpiForWindow(hwnd);
    return MulDiv(value, dpi, 96);
}

// Base values at 96 DPI
constexpr int BASE_ICON_SIZE = 32;
constexpr int BASE_SPLITTER_HEIGHT = 8;
constexpr int BASE_MIN_CONTROL_HEIGHT = 64;
constexpr int DEFAULT_HEIGHT = 64;

LRESULT CALLBACK MinimizedWindowListControlProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* control = libheirloom::getWindowData<MinimizedWindowListControl>(hwnd);
    if (!control) {
        return DefWindowProc(hwnd, message, wParam, lParam);
    }

    switch (message) {
        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rect;
            GetClientRect(hwnd, &rect);
            HBRUSH brush = CreateSolidBrush(GetSysColor(COLOR_APPWORKSPACE));
            FillRect(hdc, &rect, brush);
            DeleteObject(brush);
            return TRUE;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            control->paintSplitter(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int y = GET_Y_LPARAM(lParam);

            if (!control->isTrackingMouse_) {
                control->initMouseTracking();
            }

            if (control->isDragging_) {
                HWND mdiClient = GetParent(hwnd);
                if (!mdiClient)
                    break;

                POINT ptCurrent = {0, y};
                ClientToScreen(hwnd, &ptCurrent);

                int deltaY = ptCurrent.y - control->dragStartScreen_.y;
                int newHeight = control->initialHeight_ - deltaY;

                RECT mdiRect;
                GetClientRect(mdiClient, &mdiRect);
                int mdiHeight = mdiRect.bottom - mdiRect.top;

                int minHeight = getDpiScaledValue(hwnd, BASE_MIN_CONTROL_HEIGHT);
                int maxHeight = mdiHeight / 2;

                int constrainedHeight = newHeight;
                if (constrainedHeight < minHeight) {
                    constrainedHeight = minHeight;
                } else if (constrainedHeight > maxHeight) {
                    constrainedHeight = maxHeight;
                }

                if (control->controlHeight_ != constrainedHeight) {
                    control->controlHeight_ = constrainedHeight;

                    RECT mdiRect2;
                    GetClientRect(mdiClient, &mdiRect2);
                    int mdiWidth = mdiRect2.right - mdiRect2.left;
                    int mdiHeight2 = mdiRect2.bottom - mdiRect2.top;

                    int yPos = mdiHeight2 - constrainedHeight;

                    SetWindowPos(
                        hwnd, HWND_BOTTOM, 0, yPos, mdiWidth, constrainedHeight, SWP_SHOWWINDOW | SWP_NOACTIVATE);

                    SetWindowPos(
                        control->listView_, nullptr, 0, control->splitterHeight_, mdiWidth,
                        constrainedHeight - control->splitterHeight_, SWP_NOZORDER);
                }

                return 0;
            } else if (ListView_GetItemCount(control->listView_) > 0) {
                bool inSplitter = control->isPointInSplitter(y);

                if (inSplitter != control->isSplitterHover_) {
                    control->isSplitterHover_ = inSplitter;

                    if (inSplitter) {
                        SetCursor(LoadCursor(nullptr, IDC_SIZENS));
                    } else {
                        SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    }

                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (ListView_GetItemCount(control->listView_) == 0) {
                break;
            }

            int y = GET_Y_LPARAM(lParam);
            if (control->isPointInSplitter(y)) {
                control->isDragging_ = true;
                control->dragStartY_ = y;
                control->initialHeight_ = control->controlHeight_;

                POINT ptStart = {0, y};
                ClientToScreen(hwnd, &ptStart);
                control->dragStartScreen_ = ptStart;

                SetCapture(hwnd);
                SetCursor(LoadCursor(nullptr, IDC_SIZENS));
                InvalidateRect(hwnd, nullptr, FALSE);

                return 0;
            }

            SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;
        }
        case WM_LBUTTONUP: {
            if (control->isDragging_) {
                control->isDragging_ = false;
                ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (control->isSplitterHover_) {
                control->isSplitterHover_ = false;
                control->isTrackingMouse_ = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        }
        case WM_NOTIFY: {
            LPNMHDR nmhdr = reinterpret_cast<LPNMHDR>(lParam);
            if (nmhdr->code == NM_DBLCLK) {
                LPNMITEMACTIVATE lpnmitem = reinterpret_cast<LPNMITEMACTIVATE>(lParam);
                if (lpnmitem->iItem < 0)
                    break;
                wchar_t buffer[MAX_PATH] = {};
                ListView_GetItemText(nmhdr->hwndFrom, lpnmitem->iItem, 0, buffer, MAX_PATH);

                control->onItemDoubleClick(buffer);

                SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                return 0;
            } else if (nmhdr->code == LVN_ENDLABELEDIT) {
                NMLVDISPINFOW* pDispInfo = reinterpret_cast<NMLVDISPINFOW*>(lParam);
                if (pDispInfo->item.pszText) {
                    wchar_t oldName[MAX_PATH] = {};
                    ListView_GetItemText(nmhdr->hwndFrom, pDispInfo->item.iItem, 0, oldName, MAX_PATH);

                    std::wstring newName = pDispInfo->item.pszText;
                    bool result = control->onItemLabelEdit(oldName, newName);
                    return result ? TRUE : FALSE;
                }
                return FALSE;
            } else if (nmhdr->code == LVN_ITEMCHANGED) {
                NMLISTVIEW* pnmlv = reinterpret_cast<NMLISTVIEW*>(lParam);
                if (pnmlv->uChanged & LVIF_STATE) {
                    control->onSelectionChanged();
                }
            }
            break;
        }
        case WM_DPICHANGED: {
            if (control->window_ && control->listView_) {
                int iconSize = getDpiScaledValue(hwnd, BASE_ICON_SIZE);

                HIMAGELIST imageList = ImageList_Create(iconSize, iconSize, ILC_COLOR32 | ILC_MASK, 4, 4);

                // Re-add icons from existing items
                int itemCount = ListView_GetItemCount(control->listView_);
                for (int i = 0; i < itemCount; i++) {
                    LVITEM item = {};
                    item.mask = LVIF_PARAM;
                    item.iItem = i;
                    if (ListView_GetItem(control->listView_, &item) && item.lParam) {
                        // lParam stores the icon handle in the high bits after the text pointer
                        // We need to reload icons - for now just use what we have
                    }
                }

                HIMAGELIST oldImageList = ListView_SetImageList(control->listView_, imageList, LVSIL_NORMAL);
                if (oldImageList) {
                    ImageList_Destroy(oldImageList);
                }

                control->splitterHeight_ = getDpiScaledValue(hwnd, BASE_SPLITTER_HEIGHT);

                if (HWND parent = GetParent(control->window_)) {
                    control->autoSize(parent);
                }
            }
            break;
        }
        case WM_ACTIVATE: {
            if (LOWORD(wParam) != WA_INACTIVE) {
                SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                return 0;
            }
            break;
        }
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK MinimizedWindowListViewSubclassProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR uIdSubclass,
    DWORD_PTR dwRefData) {
    MinimizedWindowListControl* control = reinterpret_cast<MinimizedWindowListControl*>(dwRefData);

    switch (uMsg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            SetFocus(hwnd);

            if (control && control->window_) {
                SetWindowPos(control->window_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            break;

        case WM_RBUTTONDOWN: {
            LVHITTESTINFO hit = {};
            hit.pt.x = GET_X_LPARAM(lParam);
            hit.pt.y = GET_Y_LPARAM(lParam);

            int item = ListView_HitTest(hwnd, &hit);
            if (item != -1) {
                ListView_SetItemState(hwnd, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

                wchar_t buffer[MAX_PATH] = {};
                ListView_GetItemText(hwnd, item, 0, buffer, MAX_PATH);

                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(hwnd, &pt);

                control->onItemRightClick(buffer, pt);
            }

            if (control && control->window_) {
                SetWindowPos(control->window_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            return 0;
        }

        case WM_SETFOCUS:
            if (control) {
                control->onFocusChanged(true);
            }
            break;

        case WM_KILLFOCUS:
            if (control) {
                control->onFocusChanged(false);
            }
            break;

        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, MinimizedWindowListViewSubclassProc, uIdSubclass);
            return 0;
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

MinimizedWindowListControl::MinimizedWindowListControl(HINSTANCE instance, HWND mdiParent) {
    // Register window class
    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = MinimizedWindowListControlProc;
    wcex.hInstance = instance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszClassName = L"HeirloomMinimizedWindowListControl";
    RegisterClassEx(&wcex);

    // Create the container window
    window_ = CreateWindowEx(
        WS_EX_NOACTIVATE, L"HeirloomMinimizedWindowListControl", L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 100,
        mdiParent, nullptr, instance, nullptr);

    if (!window_) {
        throw std::runtime_error("Failed to create MinimizedWindowListControl window");
    }

    libheirloom::setWindowData(window_, this);

    // Initialize common controls
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    // Create the ListView control (no LVS_EDITLABELS in base — derived classes add it if needed)
    listView_ = CreateWindowEx(
        0, WC_LISTVIEW, L"",
        WS_CHILD | WS_VISIBLE | LVS_ICON | LVS_AUTOARRANGE | LVS_SINGLESEL | LVS_NOCOLUMNHEADER, 0, 0, 100, 100,
        window_, nullptr, instance, nullptr);

    if (!listView_) {
        throw std::runtime_error("Failed to create MinimizedWindowListControl ListView");
    }

    // Set up subclass procedure for the ListView
    SetWindowSubclass(listView_, MinimizedWindowListViewSubclassProc, 0, reinterpret_cast<DWORD_PTR>(this));

    // Enable alphabetical sorting
    ListView_SetExtendedListViewStyle(listView_, LVS_EX_AUTOSIZECOLUMNS);

    // Set up the image list with DPI scaling
    int iconSize = getDpiScaledValue(window_, BASE_ICON_SIZE);
    HIMAGELIST imageList = ImageList_Create(iconSize, iconSize, ILC_COLOR32 | ILC_MASK, 4, 4);
    ListView_SetImageList(listView_, imageList, LVSIL_NORMAL);

    // Set background color to match MDI workspace
    ListView_SetBkColor(listView_, GetSysColor(COLOR_APPWORKSPACE));
    ListView_SetTextBkColor(listView_, GetSysColor(COLOR_APPWORKSPACE));

    // Make sure no scrollbars are shown
    ListView_SetExtendedListViewStyle(listView_, LVS_EX_AUTOSIZECOLUMNS);

    // Initialize splitter height with DPI scaling
    splitterHeight_ = getDpiScaledValue(window_, BASE_SPLITTER_HEIGHT);

    // Initialize with default height
    controlHeight_ = getDpiScaledValue(window_, DEFAULT_HEIGHT);
}

MinimizedWindowListControl::~MinimizedWindowListControl() {
    // Free memory allocated for item text
    int itemCount = ListView_GetItemCount(listView_);
    for (int i = 0; i < itemCount; i++) {
        LVITEM item = {};
        item.mask = LVIF_PARAM;
        item.iItem = i;

        if (ListView_GetItem(listView_, &item) && item.lParam) {
            free(reinterpret_cast<void*>(item.lParam));
        }
    }
}

HWND MinimizedWindowListControl::hwnd() const {
    return window_;
}

void MinimizedWindowListControl::addItem(const std::wstring& name, HICON icon) {
    // Add the icon to the image list
    HIMAGELIST imageList = ListView_GetImageList(listView_, LVSIL_NORMAL);
    int imageIndex = ImageList_AddIcon(imageList, icon);

    // Insert item
    LVITEM item = {};
    item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
    item.iItem = 0;
    item.iSubItem = 0;
    item.pszText = const_cast<LPWSTR>(name.c_str());
    item.cchTextMax = static_cast<int>(name.length());
    item.iImage = imageIndex;

    // Store the name string as item data
    wchar_t* itemText = _wcsdup(name.c_str());
    item.lParam = reinterpret_cast<LPARAM>(itemText);

    ListView_InsertItem(listView_, &item);

    // Sort the items alphabetically
    ListView_SortItems(
        listView_,
        [](LPARAM lParam1, LPARAM lParam2, LPARAM /*lParamSort*/) -> int {
            const wchar_t* text1 = reinterpret_cast<const wchar_t*>(lParam1);
            const wchar_t* text2 = reinterpret_cast<const wchar_t*>(lParam2);
            return _wcsicmp(text1, text2);
        },
        0);

    // Force layout update
    RECT clientRect;
    GetClientRect(window_, &clientRect);
    SetWindowPos(listView_, nullptr, 0, 0, clientRect.right, clientRect.bottom, SWP_NOZORDER);

    // Ensure the control stays at the bottom of the z-order
    if (GetParent(window_)) {
        SetWindowPos(window_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void MinimizedWindowListControl::removeItem(const std::wstring& name) {
    if (!listView_) {
        return;
    }

    int itemCount = ListView_GetItemCount(listView_);
    for (int i = 0; i < itemCount; i++) {
        LVITEM item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = i;
        wchar_t buffer[MAX_PATH] = {};
        item.pszText = buffer;
        item.cchTextMax = MAX_PATH;

        if (ListView_GetItem(listView_, &item)) {
            if (name == buffer) {
                if (item.lParam) {
                    free(reinterpret_cast<void*>(item.lParam));
                }

                ListView_DeleteItem(listView_, i);
                autoSize(GetParent(window_));
                break;
            }
        }
    }
}

int MinimizedWindowListControl::autoSize(HWND mdiClient) {
    if (!mdiClient || !window_) {
        return 0;
    }

    RECT mdiRect;
    GetClientRect(mdiClient, &mdiRect);
    int mdiWidth = mdiRect.right - mdiRect.left;
    int mdiHeight = mdiRect.bottom - mdiRect.top;

    int itemCount = ListView_GetItemCount(listView_);
    if (itemCount == 0) {
        ShowWindow(window_, SW_HIDE);
        return 0;
    }

    if (controlHeight_ <= 0) {
        controlHeight_ = getDpiScaledValue(window_, BASE_MIN_CONTROL_HEIGHT);
    }

    int yPos = mdiHeight - controlHeight_;

    SetWindowPos(window_, HWND_BOTTOM, 0, yPos, mdiWidth, controlHeight_, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetWindowPos(listView_, nullptr, 0, splitterHeight_, mdiWidth, controlHeight_ - splitterHeight_, SWP_NOZORDER);

    return controlHeight_;
}

void MinimizedWindowListControl::setSplitterPosition(int height) {
    if (height <= 0) {
        return;
    }

    controlHeight_ = height;

    if (window_) {
        HWND parent = GetParent(window_);
        if (parent) {
            autoSize(parent);
        }
    }
}

void MinimizedWindowListControl::initMouseTracking() {
    if (!isTrackingMouse_ && window_) {
        TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT)};
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = window_;

        if (TrackMouseEvent(&tme)) {
            isTrackingMouse_ = true;
        }
    }
}

bool MinimizedWindowListControl::isPointInSplitter(int y) const {
    return (y >= 0 && y <= splitterHeight_);
}

void MinimizedWindowListControl::paintSplitter(HDC hdc) {
    if (ListView_GetItemCount(listView_) == 0) {
        return;
    }

    RECT rect;
    GetClientRect(window_, &rect);
    rect.bottom = splitterHeight_;

    COLORREF splitterColor;

    if (isDragging_) {
        splitterColor =
            RGB(std::max(0, GetRValue(GetSysColor(COLOR_APPWORKSPACE)) - 30),
                std::max(0, GetGValue(GetSysColor(COLOR_APPWORKSPACE)) - 30),
                std::max(0, GetBValue(GetSysColor(COLOR_APPWORKSPACE)) - 30));
    } else if (isSplitterHover_) {
        splitterColor =
            RGB(std::min(255, GetRValue(GetSysColor(COLOR_APPWORKSPACE)) + 20),
                std::min(255, GetGValue(GetSysColor(COLOR_APPWORKSPACE)) + 20),
                std::min(255, GetBValue(GetSysColor(COLOR_APPWORKSPACE)) + 20));
    } else {
        splitterColor = GetSysColor(COLOR_APPWORKSPACE);
    }

    HBRUSH brush = CreateSolidBrush(splitterColor);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

// Default virtual implementations
void MinimizedWindowListControl::onItemDoubleClick(const std::wstring& /*name*/) {}
void MinimizedWindowListControl::onItemRightClick(const std::wstring& /*name*/, POINT /*screenPos*/) {}
bool MinimizedWindowListControl::onItemLabelEdit(const std::wstring& /*oldName*/, const std::wstring& /*newName*/) {
    return false;
}
void MinimizedWindowListControl::onSelectionChanged() {}
void MinimizedWindowListControl::onFocusChanged(bool /*hasFocus*/) {}

std::wstring MinimizedWindowListControl::getSelectedItemName() const {
    if (!listView_) {
        return L"";
    }

    int selectedIndex = ListView_GetNextItem(listView_, -1, LVNI_SELECTED);
    if (selectedIndex == -1) {
        return L"";
    }

    wchar_t buffer[MAX_PATH] = {};
    ListView_GetItemText(listView_, selectedIndex, 0, buffer, MAX_PATH);
    return std::wstring(buffer);
}

bool MinimizedWindowListControl::hasSelectedItem() const {
    if (!listView_) {
        return false;
    }

    int selectedIndex = ListView_GetNextItem(listView_, -1, LVNI_SELECTED);
    return (selectedIndex != -1);
}

void MinimizedWindowListControl::startEditingSelectedItem() {
    if (!listView_) {
        return;
    }

    int selectedIndex = ListView_GetNextItem(listView_, -1, LVNI_SELECTED);
    if (selectedIndex != -1) {
        ListView_EditLabel(listView_, selectedIndex);
    }
}

}  // namespace libheirloom
