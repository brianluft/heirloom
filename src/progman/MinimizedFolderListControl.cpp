#include "progman/pch.h"
#include "progman/MinimizedFolderListControl.h"
#include "progman/resource.h"

namespace progman {

// Subclass proc for progman-specific keyboard shortcuts (F2, Ctrl+D)
LRESULT CALLBACK FolderListViewSubclassProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR uIdSubclass,
    DWORD_PTR dwRefData) {
    MinimizedFolderListControl* control = reinterpret_cast<MinimizedFolderListControl*>(dwRefData);

    switch (uMsg) {
        case WM_KEYDOWN: {
            if (wParam == VK_F2) {
                int selectedIndex = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
                if (selectedIndex != -1) {
                    ListView_EditLabel(hwnd, selectedIndex);
                }
                return 0;
            } else if (wParam == 'D' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                int selectedIndex = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
                if (selectedIndex != -1 && control && control->onDelete_) {
                    wchar_t buffer[MAX_PATH] = {};
                    ListView_GetItemText(hwnd, selectedIndex, 0, buffer, MAX_PATH);

                    std::wstring message =
                        L"Are you sure you want to delete the folder \"" + std::wstring(buffer) + L"\"?";
                    if (MessageBoxW(hwnd, message.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        control->onDelete_(buffer);
                        ListView_DeleteItem(hwnd, selectedIndex);
                    }
                }
                return 0;
            }
            break;
        }

        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, FolderListViewSubclassProc, uIdSubclass);
            return 0;
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

MinimizedFolderListControl::MinimizedFolderListControl(
    HINSTANCE instance,
    HWND parent,
    std::function<void(std::wstring)> onRestore)
    : MinimizedWindowListControl(instance, parent), instance_(instance), onRestore_(std::move(onRestore)) {
    // Load folder icon
    folderIcon_ = LoadIcon(instance, MAKEINTRESOURCE(IDI_FOLDER));
    if (!folderIcon_) {
        folderIcon_ = LoadIcon(nullptr, MAKEINTRESOURCE(IDI_FOLDER));
        if (!folderIcon_) {
            folderIcon_ = LoadIcon(nullptr, IDI_APPLICATION);
        }
    }

    // Enable label editing on the ListView (progman-specific)
    HWND lv = getListView();
    LONG_PTR style = GetWindowLongPtr(lv, GWL_STYLE);
    SetWindowLongPtr(lv, GWL_STYLE, style | LVS_EDITLABELS);

    // Add progman-specific subclass for F2/Ctrl+D keyboard shortcuts
    SetWindowSubclass(lv, FolderListViewSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
}

MinimizedFolderListControl::~MinimizedFolderListControl() = default;

void MinimizedFolderListControl::addMinimizedFolder(std::wstring name) {
    addItem(name, folderIcon_);
}

void MinimizedFolderListControl::removeMinimizedFolder(const std::wstring& name) {
    removeItem(name);
}

bool MinimizedFolderListControl::restoreMinimizedFolder(const std::wstring& folderName, bool /*maximize*/) {
    // Check if the folder is in our list
    HWND lv = getListView();
    int itemCount = ListView_GetItemCount(lv);
    int foundIndex = -1;

    for (int i = 0; i < itemCount; i++) {
        wchar_t buffer[MAX_PATH] = {};
        ListView_GetItemText(lv, i, 0, buffer, MAX_PATH);
        if (folderName == buffer) {
            foundIndex = i;
            break;
        }
    }

    if (foundIndex >= 0) {
        // Free the lParam memory before deleting
        LVITEM item = {};
        item.mask = LVIF_PARAM;
        item.iItem = foundIndex;
        if (ListView_GetItem(lv, &item) && item.lParam) {
            free(reinterpret_cast<void*>(item.lParam));
        }

        ListView_DeleteItem(lv, foundIndex);

        if (onRestore_) {
            onRestore_(folderName);
        }

        return true;
    }

    return false;
}

void MinimizedFolderListControl::onItemDoubleClick(const std::wstring& name) {
    // Find and remove the item, then restore
    HWND lv = getListView();
    int itemCount = ListView_GetItemCount(lv);
    for (int i = 0; i < itemCount; i++) {
        wchar_t buffer[MAX_PATH] = {};
        ListView_GetItemText(lv, i, 0, buffer, MAX_PATH);
        if (name == buffer) {
            // Free the lParam memory
            LVITEM item = {};
            item.mask = LVIF_PARAM;
            item.iItem = i;
            if (ListView_GetItem(lv, &item) && item.lParam) {
                free(reinterpret_cast<void*>(item.lParam));
            }

            ListView_DeleteItem(lv, i);
            break;
        }
    }

    if (onRestore_) {
        onRestore_(std::wstring(name));
    }
}

void MinimizedFolderListControl::onItemRightClick(const std::wstring& name, POINT screenPos) {
    HMENU hMenuResource = LoadMenuW(instance_, MAKEINTRESOURCEW(IDR_FOLDER_MENU));
    HMENU hMenu = GetSubMenu(hMenuResource, 0);

    HWND lv = getListView();
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPos.x, screenPos.y, 0, lv, nullptr);

    DestroyMenu(hMenuResource);

    switch (cmd) {
        case IDM_OPEN: {
            // Find and remove item, then restore (same as double-click)
            int itemCount = ListView_GetItemCount(lv);
            for (int i = 0; i < itemCount; i++) {
                wchar_t buffer[MAX_PATH] = {};
                ListView_GetItemText(lv, i, 0, buffer, MAX_PATH);
                if (name == buffer) {
                    LVITEM item = {};
                    item.mask = LVIF_PARAM;
                    item.iItem = i;
                    if (ListView_GetItem(lv, &item) && item.lParam) {
                        free(reinterpret_cast<void*>(item.lParam));
                    }
                    ListView_DeleteItem(lv, i);
                    break;
                }
            }
            if (onRestore_) {
                onRestore_(std::wstring(name));
            }
            break;
        }
        case IDM_RENAME: {
            int selectedIndex = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
            if (selectedIndex != -1) {
                ListView_EditLabel(lv, selectedIndex);
            }
            break;
        }
        case IDM_DELETE: {
            if (onDelete_) {
                std::wstring message = L"Are you sure you want to delete the folder \"" + name + L"\"?";
                if (MessageBoxW(lv, message.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    onDelete_(name);
                    int itemCount = ListView_GetItemCount(lv);
                    for (int i = 0; i < itemCount; i++) {
                        wchar_t buffer[MAX_PATH] = {};
                        ListView_GetItemText(lv, i, 0, buffer, MAX_PATH);
                        if (name == buffer) {
                            LVITEM item = {};
                            item.mask = LVIF_PARAM;
                            item.iItem = i;
                            if (ListView_GetItem(lv, &item) && item.lParam) {
                                free(reinterpret_cast<void*>(item.lParam));
                            }
                            ListView_DeleteItem(lv, i);
                            break;
                        }
                    }
                }
            }
            break;
        }
    }
}

bool MinimizedFolderListControl::onItemLabelEdit(const std::wstring& oldName, const std::wstring& newName) {
    if (onRename_) {
        onRename_(oldName, newName);
    }
    // Return false to reject ListView update — let filesystem watcher handle UI update
    return false;
}

void MinimizedFolderListControl::onSelectionChanged() {
    if (onFocusChange_) {
        onFocusChange_();
    }
}

void MinimizedFolderListControl::onFocusChanged(bool /*hasFocus*/) {
    if (onFocusChange_) {
        onFocusChange_();
    }
}

bool MinimizedFolderListControl::hasSelectedItemAndFocus() const {
    HWND lv = getListView();
    if (!lv) {
        return false;
    }

    HWND focusedWindow = GetFocus();
    if (focusedWindow != lv) {
        return false;
    }

    return hasSelectedItem();
}

std::wstring MinimizedFolderListControl::getSelectedFolderName() const {
    return getSelectedItemName();
}

void MinimizedFolderListControl::startEditingSelectedFolder() {
    startEditingSelectedItem();
}

}  // namespace progman
