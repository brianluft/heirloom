#include "progman/pch.h"
#include "progman/resource.h"
#include "progman/FolderWindow.h"
#include "libprogman/constants.h"
#include "libprogman/string_util.h"
#include "libprogman/window_data.h"
#include "libprogman/window_state.h"
#include "libheirloom/MdiChildNcPaint.h"

namespace progman {

constexpr WCHAR kFolderWindowClass[] = L"ProgmanFolderWindowClass";

// Add a custom clipboard format for internal drag operations
static CLIPFORMAT g_InternalDragFormat = 0;

// Initialize the custom clipboard format
void initializeInternalDragFormat() {
    if (g_InternalDragFormat == 0) {
        g_InternalDragFormat = RegisterClipboardFormatW(L"ProgmanInternalDrag");
    }
}

// Window procedure for the folder window
LRESULT CALLBACK FolderWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    FolderWindow* pThis = nullptr;

    if (message == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        auto* mcs = reinterpret_cast<MDICREATESTRUCT*>(pCreate->lpCreateParams);
        pThis = reinterpret_cast<FolderWindow*>(mcs->lParam);
        if (!pThis) {
            throw std::runtime_error("Failed to get FolderWindow pointer from WM_NCCREATE.");
        }
        pThis->window_ = hwnd;
        libprogman::setWindowData(hwnd, pThis);
    } else {
        pThis = libprogman::getWindowData<FolderWindow>(hwnd);
    }

    if (pThis) {
        return pThis->handleMessage(hwnd, message, wParam, lParam);
    }

    return DefMDIChildProcW(hwnd, message, wParam, lParam);
}

// Helper function to get the ini file path
std::optional<std::filesystem::path> FolderWindow::getIniFilePath() const {
    if (!folder_ || folder_->path().empty()) {
        return std::nullopt;
    }
    return folder_->path() / L"window.ini";
}

// Helper function to save window state
void FolderWindow::saveWindowState(HWND hwnd) {
    auto iniFilePath = getIniFilePath();
    if (iniFilePath) {
        libprogman::saveWindowState(hwnd, *iniFilePath);
        // Also save whether the window is minimized to the list
        libprogman::saveWindowMinimizedState(*iniFilePath, isMinimized_);
    }
}

// Helper function to restore window state
void FolderWindow::restoreWindowState(HWND hwnd) {
    auto iniFilePath = getIniFilePath();
    if (iniFilePath) {
        libprogman::restoreWindowState(hwnd, *iniFilePath);
    }
}

// Public method to save window state
void FolderWindow::saveState() {
    if (window_) {
        saveWindowState(window_);
    }
}

// Set minimized state
void FolderWindow::setMinimized(bool minimized) {
    isMinimized_ = minimized;
}

// Get current minimized state
bool FolderWindow::isMinimized() const {
    return isMinimized_;
}

// Check if the window was minimized on save
bool FolderWindow::wasMinimizedOnSave() const {
    auto iniFilePath = getIniFilePath();
    if (iniFilePath) {
        return libprogman::getWindowMinimizedState(*iniFilePath);
    }
    return false;
}

// Helper function to get the appropriate icon size based on DPI
int getIconSizeForDpi(HWND hwnd) {
    // Get the DPI for the window
    UINT dpi = GetDpiForWindow(hwnd);

    // Calculate scaling factor (96 is the baseline DPI)
    double scalingFactor = static_cast<double>(dpi) / 96.0;

    // Base icon size is 32x32, scale it based on DPI
    return static_cast<int>(32 * scalingFactor);
}

// Comparison function for sorting ListView items by name
static int CALLBACK CompareListViewItems(LPARAM lParam1, LPARAM lParam2, LPARAM /*lParamSort*/) {
    libprogman::Shortcut* shortcut1 = reinterpret_cast<libprogman::Shortcut*>(lParam1);
    libprogman::Shortcut* shortcut2 = reinterpret_cast<libprogman::Shortcut*>(lParam2);

    if (!shortcut1 || !shortcut2) {
        return 0;
    }

    // Compare by name (case-insensitive)
    return _wcsicmp(shortcut1->name().c_str(), shortcut2->name().c_str());
}

// Constructor
FolderWindow::FolderWindow(
    HINSTANCE instance,
    HWND mdiClient,
    std::shared_ptr<libprogman::ShortcutFolder> folder,
    libprogman::ShortcutManager* shortcutManager)
    : instance_(instance), folder_(folder), shortcutManager_(shortcutManager) {
    // Register window class if needed
    WNDCLASSEXW wcex = {};
    if (!GetClassInfoExW(instance_, kFolderWindowClass, &wcex)) {
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wcex.lpfnWndProc = FolderWindowProc;
        wcex.hInstance = instance_;
        wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = kFolderWindowClass;
        wcex.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_FOLDER));

        if (!RegisterClassExW(&wcex)) {
            THROW_LAST_ERROR();
        }
    }

    // Setup MDICREATESTRUCT
    MDICREATESTRUCTW mcs = {};
    mcs.szClass = kFolderWindowClass;
    mcs.szTitle = folder_->name().c_str();
    mcs.hOwner = instance_;
    mcs.x = mcs.y = CW_USEDEFAULT;
    mcs.cx = mcs.cy = CW_USEDEFAULT;
    // Make sure we include the system menu and all window styles for proper MDI behavior
    mcs.style = WS_VISIBLE | WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    mcs.lParam = reinterpret_cast<LPARAM>(this);

    // Send the MDI create message directly
    window_ = reinterpret_cast<HWND>(SendMessageW(mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));

    if (!window_) {
        THROW_LAST_ERROR();
    }

    // Create ListView control
    createListView();

    // Setup drag and drop support
    setupDragAndDrop();

    // Populate the ListView with shortcuts
    refreshListView();

    // Restore window position if available
    restoreWindowState(window_);
}

void FolderWindow::setFolder(std::shared_ptr<libprogman::ShortcutFolder> folder) {
    folder_ = folder;

    // Update window title
    SetWindowTextW(window_, folder_->name().c_str());

    // Refresh the ListView
    refreshListView();
}

void FolderWindow::show() {
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);

    // When showing the window, it's no longer minimized to the list
    isMinimized_ = false;
}

void FolderWindow::close() {
    if (window_) {
        // Clean up drag and drop support
        cleanupDragAndDrop();

        // Save window position before closing
        saveWindowState(window_);

        SendMessageW(GetParent(window_), WM_MDIDESTROY, reinterpret_cast<WPARAM>(window_), 0);
        window_ = nullptr;
        listView_ = nullptr;
    }
}

void FolderWindow::createListView() {
    RECT clientRect;
    GetClientRect(window_, &clientRect);

    // Create ListView control
    listView_ = CreateWindowExW(
        0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_ICON | LVS_AUTOARRANGE | LVS_SINGLESEL | LVS_EDITLABELS, 0, 0,
        clientRect.right, clientRect.bottom, window_, nullptr, instance_, nullptr);

    if (!listView_) {
        THROW_LAST_ERROR();
    }

    // Configure the ListView
    ListView_SetExtendedListViewStyle(listView_, LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);

    // Get the appropriate icon size based on DPI
    int iconSize = getIconSizeForDpi(window_);

    // Create appropriately sized image list for the current DPI scaling
    HIMAGELIST hImageList = ImageList_Create(iconSize, iconSize, ILC_COLOR32 | ILC_MASK, 30, 10);
    if (hImageList) {
        // Set the image list to the ListView
        ListView_SetImageList(listView_, hImageList, LVSIL_NORMAL);
    } else {
        // Fallback to old behavior if image list creation fails
        HIMAGELIST hSystemImageList =
            ImageList_Create(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), ILC_COLOR32 | ILC_MASK, 30, 10);
        ListView_SetImageList(listView_, hSystemImageList, LVSIL_NORMAL);
    }
}

void FolderWindow::refreshListView() {
    if (!listView_ || !folder_) {
        return;
    }

    // Clear existing items
    ListView_DeleteAllItems(listView_);

    // Populate with shortcuts
    int itemIndex = 0;
    for (const auto& [name, shortcut] : folder_->shortcuts()) {
        // Add icon to the image list
        HIMAGELIST hImageList = ListView_GetImageList(listView_, LVSIL_NORMAL);
        int iconIndex = ImageList_AddIcon(hImageList, shortcut->icon().get());

        // Add item to ListView
        LVITEMW lvItem = {};
        lvItem.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        lvItem.iItem = itemIndex++;
        lvItem.iSubItem = 0;
        lvItem.pszText = const_cast<LPWSTR>(name.c_str());
        lvItem.iImage = iconIndex;
        lvItem.lParam = reinterpret_cast<LPARAM>(shortcut.get());

        ListView_InsertItem(listView_, &lvItem);
    }

    // Sort items by name
    ListView_SortItems(listView_, CompareListViewItems, 0);
}

void FolderWindow::setOnMinimizeCallback(std::function<void(const std::wstring&)> callback) {
    onMinimizeCallback_ = std::move(callback);
}

void FolderWindow::setOnFocusChangeCallback(std::function<void()> callback) {
    onFocusChangeCallback_ = std::move(callback);
}

std::wstring FolderWindow::getName() const {
    return folder_ ? folder_->name() : L"";
}

bool FolderWindow::hasSelectedItem() const {
    if (!listView_) {
        return false;
    }

    // Get the selected item
    return ListView_GetSelectedCount(listView_) > 0;
}

libprogman::Shortcut* FolderWindow::getSelectedShortcut() const {
    if (!listView_) {
        return nullptr;
    }

    // Get the selected item
    int selectedIndex = ListView_GetNextItem(listView_, -1, LVNI_SELECTED);
    if (selectedIndex == -1) {
        return nullptr;
    }

    // Get the shortcut associated with the selected item
    LVITEMW lvItem = {};
    lvItem.mask = LVIF_PARAM;
    lvItem.iItem = selectedIndex;
    ListView_GetItem(listView_, &lvItem);

    return reinterpret_cast<libprogman::Shortcut*>(lvItem.lParam);
}

void FolderWindow::renameSelectedItem() {
    if (!listView_) {
        return;
    }

    int selectedIndex = ListView_GetNextItem(listView_, -1, LVNI_SELECTED);
    if (selectedIndex != -1) {
        // Start editing the label
        ListView_EditLabel(listView_, selectedIndex);
    }
}

void FolderWindow::handleDeleteCommand() {
    if (hasSelectedItem()) {
        // There's a selected shortcut to delete
        libprogman::Shortcut* shortcut = getSelectedShortcut();
        if (!shortcut) {
            return;
        }

        // Confirm with the user
        std::wstring message = L"Are you sure you want to delete the shortcut \"" + shortcut->name() + L"\"?";
        if (MessageBoxW(window_, message.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            // Delete the shortcut
            shortcut->deleteFile();
            // The filesystem watcher will pick up the change and update the UI
        }
    } else {
        // No item selected, delete the folder itself
        std::wstring folderName = getName();

        // Get the folder object
        try {
            auto folderPtr = shortcutManager_->folder(folderName);
            if (!folderPtr) {
                return;
            }

            // Confirm with the user
            std::wstring message = L"Are you sure you want to delete the folder \"" + folderName + L"\"?";
            if (MessageBoxW(window_, message.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                // Delete the folder
                shortcutManager_->deleteFolder(folderPtr.get());
                // The filesystem watcher will pick up the change and update the UI
            }
        } catch (const std::exception& e) {
            // Show error message if the folder couldn't be found
            std::wstring errorMsg = L"Error deleting folder: " + libprogman::utf8ToWide(e.what());
            MessageBoxW(window_, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
        }
    }
}

LRESULT FolderWindow::handleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    {
        auto ncResult = libheirloom::handleMdiChildNcMessage(hwnd, message, wParam, lParam);
        if (ncResult.handled)
            return ncResult.lResult;
    }

    switch (message) {
        case WM_SIZE: {
            // Resize the ListView to fill the client area
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            if (listView_) {
                MoveWindow(listView_, 0, 0, clientRect.right, clientRect.bottom, TRUE);
            }
            // Continue with default handling for MDI child window
            break;
        }

        case WM_SYSCOMMAND:
            // Completely intercept the minimize command
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                // Save window position before minimizing
                saveWindowState(hwnd);

                // Mark as minimized to the list
                isMinimized_ = true;

                // Call our minimize callback if set
                if (onMinimizeCallback_ && folder_) {
                    onMinimizeCallback_(folder_->name());
                }
                // Don't let the window actually minimize, just hide it
                ShowWindow(hwnd, SW_HIDE);
                return 0;  // Handled, don't pass to default proc
            }
            break;

        case WM_CLOSE:
            // Save window position before closing
            saveWindowState(hwnd);

            // Mark as minimized to the list since we're hiding it
            isMinimized_ = true;

            // Intercept close command and treat it like minimize
            // As with the old school Program Manager, the user can't destroy a group simply by closing it
            if (onMinimizeCallback_ && folder_) {
                onMinimizeCallback_(folder_->name());
            }
            // Don't let the window actually close, just hide it
            ShowWindow(hwnd, SW_HIDE);
            return 0;  // Handled, don't pass to default proc

        case libprogman::WM_FOLDERWINDOW_DELETE:
            handleDeleteCommand();
            return 0;

        case libprogman::WM_FOLDERWINDOW_RENAME:
            renameSelectedItem();
            return 0;

        case WM_NOTIFY: {
            NMHDR* nmhdr = reinterpret_cast<NMHDR*>(lParam);
            if (nmhdr->hwndFrom == listView_) {
                switch (nmhdr->code) {
                    case LVN_BEGINDRAG: {
                        // Handle the start of a drag operation
                        NMLISTVIEW* pnmlv = reinterpret_cast<NMLISTVIEW*>(lParam);
                        if (pnmlv->iItem != -1) {
                            startDrag(pnmlv->iItem);
                        }
                        return 0;
                    }

                    case NM_DBLCLK: {
                        // Handle double-click on a shortcut
                        NMITEMACTIVATE* nmia = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                        if (nmia->iItem != -1) {
                            LVITEMW lvItem = {};
                            lvItem.mask = LVIF_PARAM;
                            lvItem.iItem = nmia->iItem;
                            ListView_GetItem(listView_, &lvItem);

                            libprogman::Shortcut* shortcut = reinterpret_cast<libprogman::Shortcut*>(lvItem.lParam);
                            if (shortcut) {
                                try {
                                    shortcut->launch();
                                } catch (const std::exception&) {
                                    MessageBoxW(
                                        hwnd, L"Failed to open this shortcut.", L"Program Manager",
                                        MB_OK | MB_ICONERROR);
                                }
                            }
                        }
                        return 0;
                    }

                    case NM_RETURN: {
                        // Handle Enter key press on a shortcut
                        libprogman::Shortcut* shortcut = getSelectedShortcut();
                        if (shortcut) {
                            try {
                                shortcut->launch();
                            } catch (const std::exception&) {
                                MessageBoxW(
                                    hwnd, L"Failed to open this shortcut.", L"Program Manager", MB_OK | MB_ICONERROR);
                            }
                        }
                        return 0;
                    }

                    case NM_RCLICK: {
                        // Handle right-click on a shortcut (for context menu)
                        NMITEMACTIVATE* nmia = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                        if (nmia->iItem != -1) {
                            LVITEMW lvItem = {};
                            lvItem.mask = LVIF_PARAM;
                            lvItem.iItem = nmia->iItem;
                            ListView_GetItem(listView_, &lvItem);

                            libprogman::Shortcut* shortcut = reinterpret_cast<libprogman::Shortcut*>(lvItem.lParam);
                            if (shortcut) {
                                POINT pt = nmia->ptAction;
                                ClientToScreen(listView_, &pt);

                                // Load menu from resources
                                HMENU hMenuResource = LoadMenuW(instance_, MAKEINTRESOURCEW(IDR_SHORTCUT_MENU));
                                HMENU hMenu = GetSubMenu(hMenuResource, 0);

                                // Show the context menu
                                int cmd = TrackPopupMenu(
                                    hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);

                                DestroyMenu(hMenuResource);

                                // Handle the selected command
                                switch (cmd) {
                                    case IDM_OPEN:
                                        try {
                                            shortcut->launch();
                                        } catch (const std::exception&) {
                                            MessageBoxW(
                                                hwnd, L"Failed to open this shortcut.", L"Program Manager",
                                                MB_OK | MB_ICONERROR);
                                        }
                                        break;
                                    case IDM_RENAME:
                                        renameSelectedItem();
                                        break;
                                    case IDM_PROPERTIES:
                                        try {
                                            shortcut->showPropertiesWindow();
                                        } catch (const std::exception&) {
                                            MessageBoxW(
                                                hwnd, L"Failed to show shortcut properties.", L"Program Manager",
                                                MB_OK | MB_ICONERROR);
                                        }
                                        break;
                                    case IDM_DELETE:
                                        handleDeleteCommand();
                                        break;
                                }
                            }
                        }
                        return 0;
                    }

                    case LVN_ENDLABELEDIT: {
                        // Handle end of label editing
                        NMLVDISPINFOW* pDispInfo = reinterpret_cast<NMLVDISPINFOW*>(lParam);
                        if (pDispInfo->item.pszText) {  // If not null, user confirmed the edit
                            // Get the shortcut associated with the edited item
                            libprogman::Shortcut* shortcut =
                                reinterpret_cast<libprogman::Shortcut*>(pDispInfo->item.lParam);
                            if (shortcut) {
                                // Get the old path
                                std::filesystem::path oldPath = shortcut->path();
                                // Get the directory path
                                std::filesystem::path dirPath = oldPath.parent_path();
                                // Get the file extension
                                std::wstring extension = oldPath.extension().wstring();

                                // Create new path with new filename (preserve original extension)
                                std::filesystem::path newPath =
                                    dirPath / (std::wstring(pDispInfo->item.pszText) + extension);

                                try {
                                    // Rename the file
                                    std::filesystem::rename(oldPath, newPath);
                                    return TRUE;  // Accept the new name
                                } catch (const std::exception&) {
                                    // Renaming failed
                                    return FALSE;  // Reject the new name
                                }
                            }
                        }
                        return FALSE;  // Reject the new name if no text provided
                    }

                    case LVN_KEYDOWN: {
                        // Handle keyboard events from the list view
                        NMLVKEYDOWN* pnkd = reinterpret_cast<NMLVKEYDOWN*>(lParam);
                        if (pnkd->wVKey == VK_F2) {
                            renameSelectedItem();
                            return 0;
                        }
                        break;
                    }

                    case LVN_ITEMCHANGED: {
                        // Handle selection changes in the ListView
                        NMLISTVIEW* pnmlv = reinterpret_cast<NMLISTVIEW*>(lParam);
                        // Only handle state changes for selection
                        if (pnmlv->uChanged & LVIF_STATE) {
                            // Notify the main window of selection changes
                            if (onFocusChangeCallback_) {
                                onFocusChangeCallback_();
                            }
                        }
                        break;
                    }
                }
            }
            break;
        }

        case WM_KEYDOWN:
            // Handle key events for the window
            if (wParam == VK_F2) {
                renameSelectedItem();
                return 0;
            }
            break;

        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            // Notify the main window of focus changes
            if (onFocusChangeCallback_) {
                onFocusChangeCallback_();
            }
            break;

        case WM_DESTROY:
            return 0;

        case WM_COMMAND:
            // Handle commands from menus or accelerators
            switch (LOWORD(wParam)) {
                case IDM_RENAME:
                    renameSelectedItem();
                    return 0;
                case IDM_DELETE:
                    handleDeleteCommand();
                    return 0;
            }
            break;
    }

    return DefMDIChildProcW(hwnd, message, wParam, lParam);
}

void FolderWindow::setupDragAndDrop() {
    // Initialize OLE for drag and drop support
    OleInitialize(nullptr);

    // Initialize our custom clipboard format
    initializeInternalDragFormat();

    if (listView_) {
        // Create the drop target
        dropTarget_ = std::make_shared<DropTarget>(this);

        // Register the ListView as a drop target
        RegisterDragDrop(listView_, dropTarget_.get());

        // Create the drag source
        dragSource_ = std::make_shared<DragSource>();
    }
}

void FolderWindow::cleanupDragAndDrop() {
    if (listView_ && dropTarget_) {
        // Unregister the ListView as a drop target
        RevokeDragDrop(listView_);
        dropTarget_.reset();
    }

    // Clean up drag source
    dragSource_.reset();
}

void FolderWindow::handleFileDrop(const std::vector<std::wstring>& filePaths) {
    if (!folder_ || !shortcutManager_) {
        return;
    }

    for (const auto& filePath : filePaths) {
        std::filesystem::path sourcePath(filePath);

        if (!std::filesystem::exists(sourcePath)) {
            continue;
        }

        std::wstring fileName = sourcePath.filename().wstring();
        std::wstring extension = sourcePath.extension().wstring();

        if (_wcsicmp(extension.c_str(), L".lnk") == 0) {
            // This is already a shortcut file - copy it to the folder
            std::filesystem::path targetPath = folder_->path() / fileName;

            try {
                // Make sure we don't overwrite existing files
                int counter = 1;
                std::filesystem::path uniquePath = targetPath;
                while (std::filesystem::exists(uniquePath)) {
                    std::wstring baseName = sourcePath.stem().wstring();
                    std::wstring newName = baseName + L" (" + std::to_wstring(counter) + L").lnk";
                    uniquePath = folder_->path() / newName;
                    counter++;
                }

                std::filesystem::copy_file(sourcePath, uniquePath);
                // The filesystem watcher will pick up the change and update the UI
            } catch (const std::exception&) {
                // Ignore copy errors
            }
        } else {
            // This is a regular file or folder - create a shortcut to it
            std::wstring shortcutName = sourcePath.stem().wstring() + L".lnk";
            std::filesystem::path targetPath = folder_->path() / shortcutName;

            try {
                // Make sure we don't overwrite existing files
                int counter = 1;
                std::filesystem::path uniquePath = targetPath;
                while (std::filesystem::exists(uniquePath)) {
                    std::wstring baseName = sourcePath.stem().wstring();
                    std::wstring newName = baseName + L" (" + std::to_wstring(counter) + L").lnk";
                    uniquePath = folder_->path() / newName;
                    counter++;
                }

                // Create the shortcut using the ShortcutFactory
                shortcutManager_->shortcutFactory()->create(uniquePath, sourcePath);
                // The filesystem watcher will pick up the change and update the UI
            } catch (const std::exception&) {
                // Ignore creation errors
            }
        }
    }
}

// DropTarget implementation
DropTarget::DropTarget(FolderWindow* folderWindow) : folderWindow_(folderWindow), refCount_(1) {}

STDMETHODIMP DropTarget::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }

    *ppvObject = nullptr;

    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppvObject = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DropTarget::AddRef() {
    return InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) DropTarget::Release() {
    ULONG count = InterlockedDecrement(&refCount_);
    if (count == 0) {
        delete this;
    }
    return count;
}

STDMETHODIMP DropTarget::DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    if (!pDataObj || !pdwEffect) {
        return E_POINTER;
    }

    if (canAcceptDrop(pDataObj)) {
        // Check if this is an internal drag (from our application)
        bool isInternalDrag = isInternalDragSource(pDataObj);

        if (isInternalDrag && isSameFolderDrop(pDataObj)) {
            // Dragging within the same folder - no-op
            *pdwEffect = DROPEFFECT_NONE;
        } else if (isInternalDrag) {
            // Internal drag to a different folder - allow move operations
            *pdwEffect = DROPEFFECT_MOVE;
        } else {
            // External drag - only allow copy operations
            *pdwEffect = DROPEFFECT_COPY;
        }
    } else {
        *pdwEffect = DROPEFFECT_NONE;
    }

    currentDataObject_ = pDataObj;
    return S_OK;
}

STDMETHODIMP DropTarget::DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    if (!pdwEffect) {
        return E_POINTER;
    }

    // Re-determine the effect based on the data object from DragEnter
    if (currentDataObject_ && canAcceptDrop(currentDataObject_)) {
        // Check if this is an internal drag (from our application)
        bool isInternalDrag = isInternalDragSource(currentDataObject_);

        if (isInternalDrag && isSameFolderDrop(currentDataObject_)) {
            // Dragging within the same folder - no-op
            *pdwEffect = DROPEFFECT_NONE;
        } else if (isInternalDrag) {
            // Internal drag to a different folder - allow move operations
            *pdwEffect = DROPEFFECT_MOVE;
        } else {
            // External drag - only allow copy operations
            *pdwEffect = DROPEFFECT_COPY;
        }
    } else {
        *pdwEffect = DROPEFFECT_NONE;
    }

    return S_OK;
}

STDMETHODIMP DropTarget::DragLeave() {
    // Clear the stored data object reference
    currentDataObject_ = nullptr;
    return S_OK;
}

STDMETHODIMP DropTarget::Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    if (!pDataObj || !pdwEffect) {
        return E_POINTER;
    }

    *pdwEffect = DROPEFFECT_NONE;

    if (canAcceptDrop(pDataObj)) {
        bool isInternalDrag = isInternalDragSource(pDataObj);

        // Dragging within the same folder is a no-op
        if (isInternalDrag && isSameFolderDrop(pDataObj)) {
            *pdwEffect = DROPEFFECT_NONE;
            return S_OK;
        }

        auto filePaths = extractFilePaths(pDataObj);
        if (!filePaths.empty()) {
            folderWindow_->handleFileDrop(filePaths);

            if (isInternalDrag) {
                // Internal drag - we can safely delete source files (move operation)
                for (const auto& filePath : filePaths) {
                    try {
                        std::filesystem::remove(filePath);
                    } catch (const std::exception&) {
                        // Ignore delete errors - the file might have already been moved
                    }
                }
                *pdwEffect = DROPEFFECT_MOVE;
            } else {
                // External drag - NEVER delete source files (copy operation)
                *pdwEffect = DROPEFFECT_COPY;
            }
        }
    }

    return S_OK;
}

std::vector<std::wstring> DropTarget::extractFilePaths(IDataObject* pDataObj) {
    std::vector<std::wstring> filePaths;

    // Get the file paths from the data object
    FORMATETC formatEtc = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stgMedium = { 0 };

    HRESULT hr = pDataObj->GetData(&formatEtc, &stgMedium);
    if (SUCCEEDED(hr)) {
        HDROP hDrop = static_cast<HDROP>(stgMedium.hGlobal);
        if (hDrop) {
            UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            filePaths.reserve(fileCount);

            for (UINT i = 0; i < fileCount; i++) {
                UINT pathLength = DragQueryFileW(hDrop, i, nullptr, 0);
                if (pathLength > 0) {
                    std::wstring filePath(pathLength, L'\0');
                    DragQueryFileW(hDrop, i, filePath.data(), pathLength + 1);
                    filePaths.push_back(filePath);
                }
            }
        }

        ReleaseStgMedium(&stgMedium);
    }

    return filePaths;
}

bool DropTarget::canAcceptDrop(IDataObject* pDataObj) {
    if (!pDataObj) {
        return false;
    }

    // Check if the data object contains file paths
    FORMATETC formatEtc = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return pDataObj->QueryGetData(&formatEtc) == S_OK;
}

bool DropTarget::isInternalDragSource(IDataObject* pDataObj) {
    if (!pDataObj) {
        return false;
    }

    // Check if the data object contains our custom format
    FORMATETC formatEtc = { g_InternalDragFormat, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return pDataObj->QueryGetData(&formatEtc) == S_OK;
}

bool DropTarget::isSameFolderDrop(IDataObject* pDataObj) {
    auto filePaths = extractFilePaths(pDataObj);
    if (filePaths.empty() || !folderWindow_->folder_) {
        return false;
    }

    const auto& targetFolder = folderWindow_->folder_->path();
    for (const auto& filePath : filePaths) {
        std::filesystem::path sourceFolder = std::filesystem::path(filePath).parent_path();
        try {
            if (!std::filesystem::equivalent(sourceFolder, targetFolder)) {
                return false;
            }
        } catch (const std::exception&) {
            return false;
        }
    }
    return true;
}

// FolderWindow drag source methods
void FolderWindow::startDrag(int itemIndex) {
    if (!listView_ || !dragSource_) {
        return;
    }

    // Get the shortcut associated with the item
    LVITEMW lvItem = {};
    lvItem.mask = LVIF_PARAM;
    lvItem.iItem = itemIndex;
    ListView_GetItem(listView_, &lvItem);

    libprogman::Shortcut* shortcut = reinterpret_cast<libprogman::Shortcut*>(lvItem.lParam);
    if (!shortcut) {
        return;
    }

    // Create a data object with the shortcut path
    auto dataObject = std::make_shared<DataObject>(shortcut->path().wstring());

    // Only allow move operations (only dragging within our application)
    DWORD allowedEffects = DROPEFFECT_MOVE;
    DWORD effect = DROPEFFECT_NONE;

    // Perform the drag operation
    HRESULT hr = DoDragDrop(dataObject.get(), dragSource_.get(), allowedEffects, &effect);

    if (SUCCEEDED(hr)) {
        if (effect == DROPEFFECT_MOVE) {
            // The shortcut was moved to another folder window
            // The target will handle the actual move operation
            // We need to check if the file still exists, and if not, refresh the view
            if (!std::filesystem::exists(shortcut->path())) {
                refreshListView();
            }
        }
    }
}

bool FolderWindow::shouldStartDrag(POINT currentPoint) const {
    int deltaX = abs(currentPoint.x - dragStartPoint_.x);
    int deltaY = abs(currentPoint.y - dragStartPoint_.y);

    return deltaX > GetSystemMetrics(SM_CXDRAG) || deltaY > GetSystemMetrics(SM_CYDRAG);
}

// DragSource implementation
DragSource::DragSource() : refCount_(1) {}

STDMETHODIMP DragSource::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }

    *ppvObject = nullptr;

    if (riid == IID_IUnknown || riid == IID_IDropSource) {
        *ppvObject = static_cast<IDropSource*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DragSource::AddRef() {
    return InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) DragSource::Release() {
    ULONG count = InterlockedDecrement(&refCount_);
    if (count == 0) {
        delete this;
    }
    return count;
}

STDMETHODIMP DragSource::QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) {
    if (fEscapePressed) {
        return DRAGDROP_S_CANCEL;
    }

    // Check if the mouse button is still pressed
    if (!(grfKeyState & MK_LBUTTON)) {
        return DRAGDROP_S_DROP;
    }

    return S_OK;
}

STDMETHODIMP DragSource::GiveFeedback(DWORD dwEffect) {
    // Use default cursor feedback
    return DRAGDROP_S_USEDEFAULTCURSORS;
}

// DataObject implementation
DataObject::DataObject(const std::wstring& shortcutPath) : refCount_(1), shortcutPath_(shortcutPath) {}

STDMETHODIMP DataObject::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }

    *ppvObject = nullptr;

    if (riid == IID_IUnknown || riid == IID_IDataObject) {
        *ppvObject = static_cast<IDataObject*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DataObject::AddRef() {
    return InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) DataObject::Release() {
    ULONG count = InterlockedDecrement(&refCount_);
    if (count == 0) {
        delete this;
    }
    return count;
}

STDMETHODIMP DataObject::GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) {
    if (!pformatetcIn || !pmedium) {
        return E_POINTER;
    }

    ZeroMemory(pmedium, sizeof(STGMEDIUM));

    // Handle our custom internal drag format
    if (pformatetcIn->cfFormat == g_InternalDragFormat) {
        // Just provide a simple marker - the presence of this format indicates internal drag
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
        if (!hGlobal) {
            return E_OUTOFMEMORY;
        }

        void* pData = GlobalLock(hGlobal);
        if (!pData) {
            GlobalFree(hGlobal);
            return E_OUTOFMEMORY;
        }

        *static_cast<DWORD*>(pData) = 0x12345678;  // Simple marker value
        GlobalUnlock(hGlobal);

        pmedium->tymed = TYMED_HGLOBAL;
        pmedium->hGlobal = hGlobal;
        pmedium->pUnkForRelease = nullptr;

        return S_OK;
    }

    // Handle CF_HDROP format for file paths
    if (pformatetcIn->cfFormat == CF_HDROP) {
        // Calculate the size needed for the DROPFILES structure
        size_t pathLength = shortcutPath_.length() + 1;                           // +1 for null terminator
        size_t totalSize = sizeof(DROPFILES) + (pathLength + 1) * sizeof(WCHAR);  // +1 for double null terminator

        // Allocate global memory
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, totalSize);
        if (!hGlobal) {
            return E_OUTOFMEMORY;
        }

        // Lock the memory and set up the DROPFILES structure
        void* pData = GlobalLock(hGlobal);
        if (!pData) {
            GlobalFree(hGlobal);
            return E_OUTOFMEMORY;
        }

        DROPFILES* pDropFiles = static_cast<DROPFILES*>(pData);
        pDropFiles->pFiles = sizeof(DROPFILES);
        pDropFiles->fWide = TRUE;
        pDropFiles->pt.x = 0;
        pDropFiles->pt.y = 0;
        pDropFiles->fNC = FALSE;

        // Copy the file path
        WCHAR* pPath = reinterpret_cast<WCHAR*>(static_cast<char*>(pData) + sizeof(DROPFILES));
        wcscpy_s(pPath, pathLength, shortcutPath_.c_str());
        pPath[pathLength] = L'\0';  // Double null terminator

        GlobalUnlock(hGlobal);

        // Set up the storage medium
        pmedium->tymed = TYMED_HGLOBAL;
        pmedium->hGlobal = hGlobal;
        pmedium->pUnkForRelease = nullptr;

        return S_OK;
    }

    return DV_E_FORMATETC;
}

STDMETHODIMP DataObject::GetDataHere(FORMATETC* pformatetc, STGMEDIUM* pmedium) {
    return E_NOTIMPL;
}

STDMETHODIMP DataObject::QueryGetData(FORMATETC* pformatetc) {
    if (!pformatetc) {
        return E_POINTER;
    }

    // We support our custom internal drag format
    if (pformatetc->cfFormat == g_InternalDragFormat && pformatetc->tymed & TYMED_HGLOBAL &&
        pformatetc->dwAspect == DVASPECT_CONTENT) {
        return S_OK;
    }

    // We support CF_HDROP format
    if (pformatetc->cfFormat == CF_HDROP && pformatetc->tymed & TYMED_HGLOBAL &&
        pformatetc->dwAspect == DVASPECT_CONTENT) {
        return S_OK;
    }

    return DV_E_FORMATETC;
}

STDMETHODIMP DataObject::GetCanonicalFormatEtc(FORMATETC* pformatectIn, FORMATETC* pformatetcOut) {
    if (!pformatetcOut) {
        return E_POINTER;
    }

    *pformatetcOut = *pformatectIn;
    pformatetcOut->ptd = nullptr;
    return DATA_S_SAMEFORMATETC;
}

STDMETHODIMP DataObject::SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease) {
    return E_NOTIMPL;
}

STDMETHODIMP DataObject::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc) {
    return E_NOTIMPL;
}

STDMETHODIMP DataObject::DAdvise(FORMATETC* pformatetc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection) {
    return E_NOTIMPL;
}

STDMETHODIMP DataObject::DUnadvise(DWORD dwConnection) {
    return E_NOTIMPL;
}

STDMETHODIMP DataObject::EnumDAdvise(IEnumSTATDATA** ppenumAdvise) {
    return E_NOTIMPL;
}

}  // namespace progman
