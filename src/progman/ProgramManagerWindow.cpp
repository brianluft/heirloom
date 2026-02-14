#include "progman/pch.h"
#include "progman/resource.h"
#include "progman/ProgramManagerWindow.h"
#include "progman/NewFolderDialog.h"
#include "progman/FindingAppsDialog.h"
#include "progman/NewShortcutDialog.h"
#include "progman/AboutDialog.h"
#include "libprogman/constants.h"
#include "libheirloom/MdiDpiFixup.h"
#include "libprogman/window_data.h"
#include "libprogman/string_util.h"
#include "libprogman/window_state.h"

namespace progman {

constexpr WCHAR kClassName[] = L"ProgmanWindowClass";

// Define a safe range for MDI child window menu IDs
// WM_USER is 0x0400 (1024), we'll use WM_USER+1000 through WM_USER+1999 for MDI child windows
constexpr UINT IDM_MDICHILDFIRST = WM_USER + 1000;
constexpr UINT IDM_MDICHILDLAST = WM_USER + 1999;

// Section and key names for INI file
constexpr WCHAR INI_SPLITTER_SECTION[] = L"MinimizedIconSplitter";
constexpr WCHAR INI_SPLITTER_HEIGHT_KEY[] = L"Height";

// Get the path to the window state INI file
std::filesystem::path getWindowStateFilePath() {
    WCHAR appDataPath[MAX_PATH] = { 0 };
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        throw std::runtime_error("Failed to get APPDATA folder path");
    }

    std::filesystem::path path(appDataPath);
    path /= L"Heirloom Program Manager";

    // Create the directory if it doesn't exist
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        throw std::runtime_error("Failed to create settings directory");
    }

    return path / L"window.ini";
}

ProgramManagerWindow::ProgramManagerWindow(
    HINSTANCE hInstance,
    libprogman::ShortcutManager* shortcutManager,
    libprogman::ShortcutFactory* shortcutFactory,
    libprogman::InstalledAppList* installedAppList)
    : hInstance_(hInstance),
      shortcutManager_(shortcutManager),
      shortcutFactory_(shortcutFactory),
      installedAppList_(installedAppList) {
    registerWindowClass();

    // Create the main frame window with explicit MDI frame styles
    hwnd_ = CreateWindowExW(
        WS_EX_COMPOSITED, kClassName, L"Heirloom Program Manager",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr,
        nullptr, hInstance_, this);

    if (!hwnd_) {
        DWORD error = GetLastError();
        std::string errorMsg = "Failed to create Program Manager window. Error code: " + std::to_string(error);
        throw std::runtime_error(errorMsg);
    }

    createMdiClient();

    // Create the minimized folder list control, passing the MDI client as the parent
    minimizedFolderList_ =
        std::make_unique<MinimizedFolderListControl>(hInstance_, mdiClient_, [this](std::wstring folderName) {
            // Default to no maximization when restored from double-click
            this->restoreMinimizedFolder(folderName, false);
        });

    // Set up rename and delete callbacks
    minimizedFolderList_->setOnRenameCallback([this](const std::wstring& oldName, const std::wstring& newName) {
        this->renameMinimizedFolder(oldName, newName);
    });

    minimizedFolderList_->setOnDeleteCallback(
        [this](const std::wstring& folderName) { this->deleteMinimizedFolder(folderName); });

    minimizedFolderList_->setOnFocusChangeCallback([this]() { updateMenuStates(); });

    // Position the control
    minimizedFolderList_->autoSize(mdiClient_);

    // Load the initial folder windows
    shortcutManager_->refresh();
    syncFolderWindows();

    // Load saved splitter position
    loadSplitterPosition();

    // Set initial menu states
    updateMenuStates();
}

// Load the splitter position from INI file
void ProgramManagerWindow::loadSplitterPosition() {
    try {
        auto iniPath = getWindowStateFilePath();
        auto iniPathStr = iniPath.wstring();

        // Read the height from INI file
        WCHAR buffer[16] = { 0 };
        GetPrivateProfileStringW(
            INI_SPLITTER_SECTION, INI_SPLITTER_HEIGHT_KEY, L"", buffer, _countof(buffer), iniPathStr.c_str());

        // Parse the height value
        if (buffer[0] != L'\0') {
            int height = _wtoi(buffer);
            if (height > 0 && minimizedFolderList_) {
                minimizedFolderList_->setSplitterPosition(height);
            }
        }
    } catch (const std::exception&) {
        // Silently ignore errors - this is not critical
    }
}

// Save the splitter position to INI file
void ProgramManagerWindow::saveSplitterPosition() const {
    if (!minimizedFolderList_) {
        return;
    }

    try {
        int height = minimizedFolderList_->getSplitterPosition();
        if (height <= 0) {
            return;
        }

        auto iniPath = getWindowStateFilePath();
        auto iniPathStr = iniPath.wstring();

        // Save the height to INI file
        WritePrivateProfileStringW(
            INI_SPLITTER_SECTION, INI_SPLITTER_HEIGHT_KEY, std::to_wstring(height).c_str(), iniPathStr.c_str());
    } catch (const std::exception&) {
        // Silently ignore errors - this is not critical
    }
}

void ProgramManagerWindow::registerWindowClass() {
    // Check if class already exists
    WNDCLASSW wndClass;
    if (GetClassInfoW(hInstance_, kClassName, &wndClass)) {
        return;
    }

    windowClass_.cbSize = sizeof(WNDCLASSEX);
    windowClass_.style = CS_HREDRAW | CS_VREDRAW;
    windowClass_.lpfnWndProc = windowProc;
    windowClass_.cbClsExtra = 0;
    windowClass_.cbWndExtra = 0;
    windowClass_.hInstance = hInstance_;

    // Try loading the application icon
    HICON hIcon = LoadIcon(hInstance_, MAKEINTRESOURCE(IDI_PROGMAN));
    if (hIcon) {
        windowClass_.hIcon = hIcon;
        windowClass_.hIconSm = hIcon;
    } else {
        // Use system icon if application icon can't be loaded
        windowClass_.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        windowClass_.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    }

    windowClass_.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass_.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);

    windowClass_.lpszMenuName = MAKEINTRESOURCEW(IDR_MAINMENU);
    windowClass_.lpszClassName = kClassName;

    if (!RegisterClassExW(&windowClass_)) {
        DWORD error = GetLastError();
        std::string errorMsg = "Failed to register window class. Error code: " + std::to_string(error);
        throw std::runtime_error(errorMsg);
    }
}

void ProgramManagerWindow::createMdiClient() {
    CLIENTCREATESTRUCT ccs{};

    // Get the main window's menu
    HMENU hMainMenu = GetMenu(hwnd_);

    // Find the Window menu directly by position (usually after File menu)
    int windowMenuPos = -1;
    int menuCount = GetMenuItemCount(hMainMenu);
    for (int i = 0; i < menuCount; i++) {
        WCHAR buffer[256] = { 0 };
        GetMenuStringW(hMainMenu, i, buffer, _countof(buffer), MF_BYPOSITION);

        if (wcscmp(buffer, L"&Window") == 0) {
            windowMenuPos = i;
            break;
        }
    }

    if (windowMenuPos >= 0) {
        ccs.hWindowMenu = GetSubMenu(hMainMenu, windowMenuPos);
    }

    // Set the first child ID to our safe range
    ccs.idFirstChild = IDM_MDICHILDFIRST;

    // Create a standard MDI client
    mdiClient_ = CreateWindowW(
        L"MDICLIENT", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, hwnd_, nullptr, hInstance_, &ccs);

    if (!mdiClient_) {
        DWORD error = GetLastError();
        std::string errorMsg = "Failed to create MDI client window. Error code: " + std::to_string(error);
        throw std::runtime_error(errorMsg);
    }

    // Resize the MDI client to fill the main window
    RECT rcClient;
    GetClientRect(hwnd_, &rcClient);
    MoveWindow(mdiClient_, 0, 0, rcClient.right, rcClient.bottom, TRUE);
}

void ProgramManagerWindow::show(int nCmdShow) {
    // First, try to restore the saved window state
    bool stateRestored = false;
    try {
        stateRestored = libprogman::restoreWindowState(hwnd_, getWindowStateFilePath());
    } catch (const std::exception& e) {
        // Just log the error - this is not critical
        OutputDebugStringA(e.what());
    }

    // Only use the provided nCmdShow if we couldn't restore the saved state
    if (!stateRestored) {
        // Force window to be shown with SW_SHOWNORMAL if nCmdShow is 0
        if (nCmdShow == 0) {
            nCmdShow = SW_SHOWNORMAL;
        }
        ShowWindow(hwnd_, nCmdShow);
    }

    UpdateWindow(hwnd_);

    // Additional call to force visibility
    SetForegroundWindow(hwnd_);
}

LRESULT CALLBACK ProgramManagerWindow::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ProgramManagerWindow* pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<ProgramManagerWindow*>(pCreate->lpCreateParams);
        if (pThis) {
            libprogman::setWindowData(hwnd, pThis);
        }
    } else {
        pThis = libprogman::getWindowData<ProgramManagerWindow>(hwnd);
    }

    if (pThis) {
        return pThis->handleMessage(hwnd, uMsg, wParam, lParam);
    } else {
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

LRESULT ProgramManagerWindow::handleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Additional special handling for window menu activation
    if (uMsg == WM_COMMAND) {
        UINT cmdId = LOWORD(wParam);

        // Check if this is one of our MDI child window menu commands
        if (cmdId >= IDM_MDICHILDFIRST && cmdId <= IDM_MDICHILDLAST) {
            // Find the window name from the menu text
            HMENU mainMenu = GetMenu(hwnd);
            int menuCount = GetMenuItemCount(mainMenu);

            // Find Window menu
            HMENU windowMenu = nullptr;
            for (int i = 0; i < menuCount; i++) {
                WCHAR buffer[256] = { 0 };
                GetMenuStringW(mainMenu, i, buffer, _countof(buffer), MF_BYPOSITION);

                if (wcscmp(buffer, L"&Window") == 0) {
                    windowMenu = GetSubMenu(mainMenu, i);
                    break;
                }
            }

            if (!windowMenu) {
                return DefFrameProcW(hwnd, mdiClient_, uMsg, wParam, lParam);
            }

            // Find the menu item for this command
            int itemCount = GetMenuItemCount(windowMenu);
            std::wstring targetFolderName;

            for (int i = 0; i < itemCount; i++) {
                if (GetMenuItemID(windowMenu, i) == cmdId) {
                    WCHAR buffer[256] = { 0 };
                    GetMenuStringW(windowMenu, i, buffer, _countof(buffer), MF_BYPOSITION);

                    // Extract the window name (remove the &# prefix)
                    std::wstring menuText = buffer;
                    size_t spacePos = menuText.find(L' ');
                    if (spacePos != std::wstring::npos && spacePos + 1 < menuText.length()) {
                        targetFolderName = menuText.substr(spacePos + 1);
                    }

                    break;
                }
            }

            if (targetFolderName.empty()) {
                return DefFrameProcW(hwnd, mdiClient_, uMsg, wParam, lParam);
            }

            // Find the folder window with this name
            auto it = folderWindows_.find(targetFolderName);
            if (it != folderWindows_.end()) {
                auto& folderWindow = it->second;
                HWND targetWindow = folderWindow->window_;

                // Check if the window is hidden (minimized)
                bool isVisible = IsWindowVisible(targetWindow);

                if (!isVisible) {
                    // Check if there's an active window and if it's maximized
                    HWND activeWindow = reinterpret_cast<HWND>(SendMessage(mdiClient_, WM_MDIGETACTIVE, 0, 0));
                    bool isMaximized = false;

                    if (activeWindow) {
                        WINDOWPLACEMENT wp;
                        wp.length = sizeof(WINDOWPLACEMENT);
                        if (GetWindowPlacement(activeWindow, &wp)) {
                            isMaximized = (wp.showCmd == SW_SHOWMAXIMIZED);
                        }
                    }

                    // Try to restore via the minimized folder list
                    if (minimizedFolderList_) {
                        minimizedFolderList_->restoreMinimizedFolder(targetFolderName, isMaximized);
                    }

                    // Always show and activate the window directly too
                    folderWindow->show();
                    BringWindowToTop(targetWindow);
                    SetFocus(targetWindow);
                    SendMessage(mdiClient_, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(targetWindow), 0);

                    if (isMaximized) {
                        SendMessage(mdiClient_, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(targetWindow), 0);
                    }

                    // Don't process the default command - we've handled it
                    return 0;
                } else {
                    // Window is already visible, just activate it
                    BringWindowToTop(targetWindow);
                    SetFocus(targetWindow);
                    SendMessage(mdiClient_, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(targetWindow), 0);
                    return 0;
                }
            }
        }
    }

    switch (uMsg) {
        case WM_SIZE:
            // Resize the MDI client window
            if (mdiClient_) {
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                MoveWindow(mdiClient_, 0, 0, rcClient.right, rcClient.bottom, TRUE);

                // Resize the minimized folder list
                if (minimizedFolderList_) {
                    minimizedFolderList_->autoSize(mdiClient_);
                }
            }
            return 0;

        case libprogman::WM_SYNC_FOLDER_WINDOWS:
            syncFolderWindows();
            return 0;

        case WM_MDIACTIVATE:
            // Update menu states when MDI child activation changes
            updateMenuStates();
            break;

        case WM_COMMAND: {
            // Handle menu commands
            switch (LOWORD(wParam)) {
                case ID_FILE_EXIT:
                    DestroyWindow(hwnd);
                    return 0;

                case ID_FILE_NEWFOLDER: {
                    // Show the new folder dialog
                    NewFolderDialog dialog(hwnd, hInstance_, shortcutManager_);
                    dialog.show();
                    return 0;
                }

                case ID_FILE_NEWSHORTCUT: {
                    // 1. Find the currently focused FolderWindow
                    FolderWindow* activeFolder = getActiveFolderWindow();
                    if (!activeFolder) {
                        return 0;
                    }

                    // 2. Show FindingAppsDialog to load installed apps
                    FindingAppsDialog findingAppsDialog(hInstance_, installedAppList_);
                    int result = findingAppsDialog.showDialog(hwnd);
                    if (result != IDOK) {
                        return 0;
                    }

                    // 3. Get the list of installed apps
                    auto installedApps = findingAppsDialog.apps();

                    // Get the folder path from the folder name
                    std::wstring folderName = activeFolder->getName();
                    try {
                        auto folderPtr = shortcutManager_->folder(folderName);
                        if (!folderPtr) {
                            return 0;
                        }

                        // 4. Show NewShortcutDialog
                        NewShortcutDialog newShortcutDialog(
                            hwnd, hInstance_, folderPtr->path(), installedApps, shortcutFactory_);
                        newShortcutDialog.showDialog();
                    } catch (const std::exception& e) {
                        // Show error message if the folder couldn't be found
                        std::wstring errorMsg = L"Error accessing folder: " + libprogman::utf8ToWide(e.what());
                        MessageBoxW(hwnd, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
                    }

                    return 0;
                }

                case ID_FILE_DELETE:
                    handleDeleteCommand();
                    return 0;

                case IDM_RENAME:
                    handleRenameCommand();
                    return 0;

                case ID_HELP_ABOUT: {
                    AboutDialog aboutDialog(hwnd, hInstance_);
                    aboutDialog.showDialog();
                    return 0;
                }

                case ID_HELP_VISIT_WEBSITE: {
                    // Open the website in the default browser
                    ShellExecuteW(hwnd, L"open", L"https://heirloomapps.com", nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }

                // Handle Window menu commands
                case ID_WINDOW_CASCADE:
                    SendMessageW(mdiClient_, WM_MDICASCADE, 0, 0);
                    return 0;

                case ID_WINDOW_TILE:
                    SendMessageW(mdiClient_, WM_MDITILE, MDITILE_HORIZONTAL, 0);
                    return 0;

                default:
                    // If the ID falls within our MDI child range, it will be handled in the main WM_COMMAND handler
                    break;
            }
            break;
        }

        // Handle menu initialization
        case WM_INITMENUPOPUP: {
            HMENU hMenu = reinterpret_cast<HMENU>(wParam);
            bool isSystemMenu = HIWORD(lParam) != 0;

            // Update menu states when any menu is about to be shown
            if (!isSystemMenu) {  // This is not the system menu
                updateMenuStates();

                // Sort Window menu items alphabetically
                int menuIndex = LOWORD(lParam);
                HMENU mainMenu = GetMenu(hwnd);

                // Check if this is the Window menu
                WCHAR menuName[256] = { 0 };
                if (GetMenuStringW(mainMenu, menuIndex, menuName, _countof(menuName), MF_BYPOSITION) &&
                    wcscmp(menuName, L"&Window") == 0) {
                    sortWindowMenu(hMenu);
                }
            }
            break;
        }

        case WM_DESTROY:
            // Save the window state before destroying
            try {
                // First save all folder window states
                for (const auto& [folderName, folderWindow] : folderWindows_) {
                    folderWindow->saveState();
                }

                // Save the splitter position
                saveSplitterPosition();

                // Then save the main window state
                libprogman::saveWindowState(hwnd, getWindowStateFilePath());
            } catch (const std::exception& e) {
                // Just log or ignore - this is not critical
                OutputDebugStringA(e.what());
            }
            PostQuitMessage(0);
            return 0;

        case WM_NCPAINT:
        case WM_NCACTIVATE: {
            LRESULT result = DefFrameProcW(hwnd, mdiClient_, uMsg, wParam, lParam);
            libheirloom::redrawMdiMenuBarButtons(hwnd, mdiClient_);
            return result;
        }

        case WM_NCLBUTTONDOWN:
            if (libheirloom::handleMdiMenuBarMouseDown(hwnd, mdiClient_, lParam))
                return 0;
            break;
    }

    // Use DefFrameProc instead of DefWindowProc for proper MDI handling
    return DefFrameProcW(hwnd, mdiClient_, uMsg, wParam, lParam);
}

void ProgramManagerWindow::refresh() {
    // Refresh the shortcuts from disk.
    shortcutManager_->refresh();

    // Post a message to sync the folder windows on the UI thread
    PostMessageW(hwnd_, libprogman::WM_SYNC_FOLDER_WINDOWS, 0, 0);
}

void ProgramManagerWindow::syncFolderWindows() {
    auto currentFolders = shortcutManager_->folders();

    // First pass: Update existing windows or mark them for removal
    std::vector<std::wstring> foldersToRemove;

    for (auto& [folderName, folderWindow] : folderWindows_) {
        auto folder = currentFolders.find(folderName);
        if (folder != nullptr) {
            // Folder still exists, update it
            folderWindow->setFolder(*folder);
        } else {
            // Folder no longer exists, mark for removal
            folderWindow->close();
            foldersToRemove.push_back(folderName);
        }
    }

    // Remove windows for folders that no longer exist
    for (const auto& folderName : foldersToRemove) {
        // Also remove from minimized folder list if it's there
        if (minimizedFolderList_) {
            minimizedFolderList_->removeMinimizedFolder(folderName);
        }
        folderWindows_.erase(folderName);
    }

    // Second pass: Create windows for new folders
    for (const auto& [folderName, folder] : currentFolders) {
        if (folderWindows_.find(folderName) == folderWindows_.end()) {
            // New folder, create a window for it
            auto folderWindow = std::make_unique<FolderWindow>(hInstance_, mdiClient_, folder, shortcutManager_);
            // Set the minimize callback
            folderWindow->setOnMinimizeCallback([this](const std::wstring& name) {
                // Add to minimized list
                minimizedFolderList_->addMinimizedFolder(name);
                // Update the layout
                minimizedFolderList_->autoSize(mdiClient_);
                // Update menu states after minimizing a window
                updateMenuStates();
            });

            // Set the focus change callback
            folderWindow->setOnFocusChangeCallback([this]() {
                // Update menu states when focus or selection changes
                updateMenuStates();
            });

            // Check if this window was minimized when last saved
            bool wasMinimized = folderWindow->wasMinimizedOnSave();
            if (wasMinimized) {
                // If it was minimized, don't show it, but add it to the minimized list
                folderWindow->setMinimized(true);
                minimizedFolderList_->addMinimizedFolder(folderName);
                minimizedFolderList_->autoSize(mdiClient_);
                ShowWindow(folderWindow->window_, SW_HIDE);
            } else {
                // Otherwise, show it normally
                folderWindow->show();
            }

            folderWindows_[folderName] = std::move(folderWindow);
        }
    }

    // Update menu states after any window changes
    updateMenuStates();
}

void ProgramManagerWindow::restoreMinimizedFolder(const std::wstring& folderName, bool maximize) {
    auto it = folderWindows_.find(folderName);
    if (it != folderWindows_.end()) {
        // Get window handle
        HWND folderHwnd = it->second->window_;

        // Show the window
        it->second->show();

        // Ensure window is visible
        BringWindowToTop(folderHwnd);

        // Make it the active MDI window
        SendMessage(mdiClient_, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(folderHwnd), 0);

        // Set focus to the window
        SetFocus(folderHwnd);

        // Maximize if requested
        if (maximize) {
            SendMessage(mdiClient_, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(folderHwnd), 0);
        }

        // Update the minimized folder list layout
        minimizedFolderList_->autoSize(mdiClient_);

        // Update menu states after restoring a window
        updateMenuStates();
    }
}

void ProgramManagerWindow::renameMinimizedFolder(const std::wstring& oldName, const std::wstring& newName) {
    try {
        // Get the folder and rename it
        auto folderPtr = shortcutManager_->folder(oldName);
        if (!folderPtr) {
            return;
        }

        // Rename the folder directory
        std::filesystem::path oldPath = folderPtr->path();
        std::filesystem::path newPath = oldPath.parent_path() / newName;

        // Rename the directory
        std::filesystem::rename(oldPath, newPath);

        // The filesystem watcher will pick up the change and update the UI
    } catch (const std::exception& e) {
        // Show error message if the rename failed
        std::wstring errorMsg = L"Error renaming folder: " + libprogman::utf8ToWide(e.what());
        MessageBoxW(hwnd_, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
    }
}

void ProgramManagerWindow::deleteMinimizedFolder(const std::wstring& folderName) {
    try {
        // Get the folder and delete it
        auto folderPtr = shortcutManager_->folder(folderName);
        if (!folderPtr) {
            return;
        }

        // Delete the folder
        shortcutManager_->deleteFolder(folderPtr.get());

        // The filesystem watcher will pick up the change and update the UI
    } catch (const std::exception& e) {
        // Show error message if the deletion failed
        std::wstring errorMsg = L"Error deleting folder: " + libprogman::utf8ToWide(e.what());
        MessageBoxW(hwnd_, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
    }
}

FolderWindow* ProgramManagerWindow::getActiveFolderWindow() const {
    HWND activeChildWindow = reinterpret_cast<HWND>(SendMessageW(mdiClient_, WM_MDIGETACTIVE, 0, 0));
    if (!activeChildWindow) {
        return nullptr;
    }

    // Find the FolderWindow instance corresponding to this HWND
    for (const auto& [folderName, folderWindow] : folderWindows_) {
        if (folderWindow->window_ == activeChildWindow) {
            return folderWindow.get();
        }
    }

    return nullptr;
}

void ProgramManagerWindow::handleDeleteCommand() {
    // First check if a minimized folder is selected and has focus
    if (minimizedFolderList_ && minimizedFolderList_->hasSelectedItemAndFocus()) {
        std::wstring selectedFolderName = minimizedFolderList_->getSelectedFolderName();
        if (!selectedFolderName.empty()) {
            // Confirm and delete the selected minimized folder
            std::wstring message = L"Are you sure you want to delete the folder \"" + selectedFolderName + L"\"?";
            if (MessageBoxW(hwnd_, message.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                deleteMinimizedFolder(selectedFolderName);
                // Remove the item from the minimized list
                // (The filesystem watcher will handle updating the UI)
            }
            return;
        }
    }

    // Fall back to the active folder window behavior
    FolderWindow* activeFolder = getActiveFolderWindow();
    if (!activeFolder) {
        return;
    }

    // Send the delete message to the active folder window
    HWND folderHwnd = activeFolder->window_;
    SendMessageW(folderHwnd, libprogman::WM_FOLDERWINDOW_DELETE, 0, 0);
}

void ProgramManagerWindow::handleRenameCommand() {
    // First check if a minimized folder is selected and has focus
    if (minimizedFolderList_ && minimizedFolderList_->hasSelectedItemAndFocus()) {
        // Start editing the selected minimized folder
        minimizedFolderList_->startEditingSelectedFolder();
        return;
    }

    // Fall back to the active folder window behavior
    FolderWindow* activeFolder = getActiveFolderWindow();
    if (!activeFolder) {
        return;
    }

    // Send the rename message to the active folder window
    HWND folderHwnd = activeFolder->window_;
    SendMessageW(folderHwnd, libprogman::WM_FOLDERWINDOW_RENAME, 0, 0);
}

void ProgramManagerWindow::sortWindowMenu(HMENU windowMenu) {
    // Get the number of items in the Window menu
    int itemCount = GetMenuItemCount(windowMenu);
    if (itemCount <= 0) {
        return;
    }

    // Find the position where the window list starts
    // Typically, the standard Window menu commands (Cascade, Tile, etc.) come first,
    // followed by a separator, then the window list
    int firstWindowPos = -1;
    for (int i = 0; i < itemCount; i++) {
        UINT id = GetMenuItemID(windowMenu, i);
        // Windows lists typically start after a separator
        if (id == 0) {  // separator
            // Make sure there's at least one item after the separator
            if (i + 1 < itemCount) {
                firstWindowPos = i + 1;
                break;
            }
        }
    }

    // If we didn't find a start position, or there are no windows listed, return
    if (firstWindowPos < 0 || firstWindowPos >= itemCount) {
        return;
    }

    // Collect window menu items
    struct WindowMenuItem {
        std::wstring text;
        UINT id;
        UINT state;
        bool isValid;
    };
    std::vector<WindowMenuItem> windowItems;

    // Get all window menu items
    for (int i = firstWindowPos; i < itemCount; i++) {
        WCHAR buffer[256] = { 0 };
        MENUITEMINFOW mii = { 0 };
        mii.cbSize = sizeof(MENUITEMINFOW);
        mii.fMask = MIIM_ID | MIIM_STATE | MIIM_STRING;
        mii.dwTypeData = buffer;
        mii.cch = _countof(buffer);

        if (GetMenuItemInfoW(windowMenu, i, TRUE, &mii)) {
            WindowMenuItem item;
            item.text = buffer;
            item.id = mii.wID;
            item.state = mii.fState;
            // Mark item as valid if it has a non-zero ID and non-empty text
            item.isValid = (mii.wID != 0 && buffer[0] != L'\0');

            // Only add valid window items
            if (item.isValid) {
                windowItems.push_back(item);
            }
        }
    }

    // If no valid items found, return
    if (windowItems.empty()) {
        return;
    }

    // Sort window items alphabetically
    std::sort(windowItems.begin(), windowItems.end(), [](const WindowMenuItem& a, const WindowMenuItem& b) {
        // Extract the folder name (after the space if present)
        std::wstring nameA = a.text;
        std::wstring nameB = b.text;

        // Remove the numeric prefix (like "&1 ")
        size_t spaceA = nameA.find(L' ');
        size_t spaceB = nameB.find(L' ');

        if (spaceA != std::wstring::npos && spaceA + 1 < nameA.length()) {
            nameA = nameA.substr(spaceA + 1);
        }

        if (spaceB != std::wstring::npos && spaceB + 1 < nameB.length()) {
            nameB = nameB.substr(spaceB + 1);
        }

        return nameA < nameB;
    });

    // Remove all window menu items
    for (int i = itemCount - 1; i >= firstWindowPos; i--) {
        RemoveMenu(windowMenu, i, MF_BYPOSITION);
    }

    // Add items back in sorted order
    for (const auto& item : windowItems) {
        AppendMenuW(windowMenu, MF_STRING | item.state, item.id, item.text.c_str());
    }
}

void ProgramManagerWindow::updateMenuStates() {
    HMENU hMenu = GetMenu(hwnd_);
    if (!hMenu) {
        return;
    }

    // Get the current application state
    FolderWindow* activeFolder = getActiveFolderWindow();
    bool hasActiveFolderWindow = (activeFolder != nullptr);
    bool hasSelectedShortcut = hasActiveFolderWindow && activeFolder->hasSelectedItem();
    bool minimizedBarHasFocus = minimizedFolderList_ && minimizedFolderList_->hasSelectedItemAndFocus();
    bool hasSelectedMinimizedFolder = minimizedBarHasFocus && !minimizedFolderList_->getSelectedFolderName().empty();

    // Count restored windows (visible MDI child windows)
    int restoredWindowCount = 0;
    for (const auto& [folderName, folderWindow] : folderWindows_) {
        if (IsWindowVisible(folderWindow->window_)) {
            restoredWindowCount++;
        }
    }

    // Check if we have a visible, focused folder window
    bool hasVisibleFocusedFolderWindow = false;
    if (hasActiveFolderWindow && IsWindowVisible(activeFolder->window_)) {
        // Check if the active folder window or its child controls have focus
        HWND focusedWindow = GetFocus();
        if (focusedWindow) {
            // Check if the focused window is the folder window itself or one of its children
            HWND parent = focusedWindow;
            while (parent) {
                if (parent == activeFolder->window_) {
                    hasVisibleFocusedFolderWindow = true;
                    break;
                }
                parent = GetParent(parent);
            }
        }
    }

    // Update "New shortcut" command (ID_FILE_NEWSHORTCUT)
    // Enable only when there is a visible, focused folder window
    UINT newShortcutState = hasVisibleFocusedFolderWindow ? MF_ENABLED : MF_GRAYED;
    EnableMenuItem(hMenu, ID_FILE_NEWSHORTCUT, newShortcutState);

    // Update "Rename" command (IDM_RENAME)
    // Enable only when there is a shortcut icon or a minimized folder icon currently selected
    UINT renameState = (hasSelectedShortcut || hasSelectedMinimizedFolder) ? MF_ENABLED : MF_GRAYED;
    EnableMenuItem(hMenu, IDM_RENAME, renameState);

    // Update "Delete" command (ID_FILE_DELETE)
    // Enable only when there is a visible, focused folder window or a focused, selected minimized folder icon
    UINT deleteState = (hasVisibleFocusedFolderWindow || hasSelectedMinimizedFolder) ? MF_ENABLED : MF_GRAYED;
    EnableMenuItem(hMenu, ID_FILE_DELETE, deleteState);

    // Update "Cascade" and "Tile" commands (ID_WINDOW_CASCADE, ID_WINDOW_TILE)
    // Enable only when there is at least one restored window
    UINT windowCommandState = (restoredWindowCount > 0) ? MF_ENABLED : MF_GRAYED;
    EnableMenuItem(hMenu, ID_WINDOW_CASCADE, windowCommandState);
    EnableMenuItem(hMenu, ID_WINDOW_TILE, windowCommandState);

    // Force the menu bar to redraw
    DrawMenuBar(hwnd_);
}

}  // namespace progman
