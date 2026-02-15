#include "winfile.h"
#include "wfminbar.h"
#include "wftree.h"
#include "libheirloom/MinimizedWindowListControl.h"

#include <unordered_map>
#include <string>

// Derived class for File Manager's minimized window bar
class WinfileMinimizedWindowList : public libheirloom::MinimizedWindowListControl {
   public:
    WinfileMinimizedWindowList(HINSTANCE instance, HWND mdiParent) : MinimizedWindowListControl(instance, mdiParent) {}

    void addWindow(HWND hwndChild) {
        // Get the window title
        WCHAR title[MAXPATHLEN] = {};
        GetWindowText(hwndChild, title, COUNTOF(title));

        // Create a shortened display name from the path
        std::wstring displayName = shortenPath(title);

        // Get the icon for this window
        HICON icon = getWindowIcon(hwndChild);

        // Store the mapping
        windowMap_[displayName] = hwndChild;

        addItem(displayName, icon);
    }

    void removeWindow(HWND hwndChild) {
        // Find the display name for this window
        for (auto it = windowMap_.begin(); it != windowMap_.end(); ++it) {
            if (it->second == hwndChild) {
                removeItem(it->first);
                windowMap_.erase(it);
                return;
            }
        }
    }

   protected:
    void onItemDoubleClick(const std::wstring& name) override {
        auto it = windowMap_.find(name);
        if (it != windowMap_.end()) {
            HWND hwndChild = it->second;
            windowMap_.erase(it);

            // Remove the icon from the bar
            removeItem(name);

            // Show and restore the MDI child window
            ShowWindow(hwndChild, SW_SHOW);
            SendMessage(hwndMDIClient, WM_MDIRESTORE, (WPARAM)hwndChild, 0);
        }
    }

   private:
    std::unordered_map<std::wstring, HWND> windowMap_;

    static std::wstring shortenPath(const WCHAR* title) {
        std::wstring fullTitle = title;

        if (fullTitle.empty()) {
            return L"(untitled)";
        }

        // Window titles are already just the last path component (e.g. "Documents")
        // or a search title. Strip any ":N" duplicate suffix for a cleaner label.
        size_t colonPos = fullTitle.rfind(L':');
        if (colonPos != std::wstring::npos && colonPos > 0) {
            // Only strip if everything after the colon is digits (duplicate number)
            bool allDigits = true;
            for (size_t i = colonPos + 1; i < fullTitle.size(); i++) {
                if (fullTitle[i] < L'0' || fullTitle[i] > L'9') {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits && colonPos + 1 < fullTitle.size()) {
                return fullTitle.substr(0, colonPos);
            }
        }

        return fullTitle;
    }

    static HICON getWindowIcon(HWND hwndChild) {
        // Try to get the icon set on the window
        HICON icon = reinterpret_cast<HICON>(SendMessage(hwndChild, WM_GETICON, ICON_SMALL, 0));
        if (icon) {
            return icon;
        }

        // Use the tree/dir icon helper for tree windows
        if (HasTreeWindow(hwndChild) || HasDirWindow(hwndChild)) {
            return GetTreeIcon(hwndChild);
        }

        // Search windows get the directory icon
        if (hwndChild == hwndSearch) {
            return hicoDir;
        }

        // Fallback: use application icon
        return LoadIcon(hAppInstance, MAKEINTRESOURCE(APPICON));
    }
};

// Static instance
static WinfileMinimizedWindowList* s_minBar = nullptr;

void InitMinimizedWindowBar(HINSTANCE hInstance, HWND hwndMDIClient) {
    if (!s_minBar) {
        s_minBar = new WinfileMinimizedWindowList(hInstance, hwndMDIClient);
    }
}

void DestroyMinimizedWindowBar() {
    delete s_minBar;
    s_minBar = nullptr;
}

void MinBarAddWindow(HWND hwndChild) {
    if (s_minBar) {
        s_minBar->addWindow(hwndChild);
        // Trigger a layout update
        MinBarAutoSize();
    }
}

void MinBarRemoveWindow(HWND hwndChild) {
    if (s_minBar) {
        s_minBar->removeWindow(hwndChild);
    }
}

void MinBarAutoSize() {
    if (s_minBar) {
        s_minBar->autoSize(hwndMDIClient);
    }
}

int MinBarGetHeight() {
    if (s_minBar) {
        return s_minBar->getSplitterPosition();
    }
    return 0;
}

void MinBarSetHeight(int height) {
    if (s_minBar) {
        s_minBar->setSplitterPosition(height);
    }
}
