#pragma once

#include "progman/pch.h"
#include "libprogman/ShortcutFolder.h"
#include "libprogman/ShortcutManager.h"

namespace progman {

class ProgramManagerWindow;  // Forward declaration

// Forward declaration of DropTarget class
class DropTarget;

// Forward declaration of DragSource class
class DragSource;

// Forward declaration of DataObject class
class DataObject;

class FolderWindow {
   public:
    FolderWindow(
        HINSTANCE instance,
        HWND mdiClient,
        std::shared_ptr<libprogman::ShortcutFolder> folder,
        libprogman::ShortcutManager* shortcutManager);
    FolderWindow(const FolderWindow&) = delete;
    FolderWindow& operator=(const FolderWindow&) = delete;
    FolderWindow(FolderWindow&&) = delete;
    FolderWindow& operator=(FolderWindow&&) = delete;

    void setFolder(std::shared_ptr<libprogman::ShortcutFolder> folder);
    void show();
    void close();
    void setOnMinimizeCallback(std::function<void(const std::wstring&)> callback);
    void setOnFocusChangeCallback(std::function<void()> callback);
    std::wstring getName() const;

    // New methods for handling item selection
    bool hasSelectedItem() const;
    libprogman::Shortcut* getSelectedShortcut() const;
    void renameSelectedItem();

    // Public method to save window state
    void saveState();

    // Methods to handle minimized state
    void setMinimized(bool minimized);
    bool isMinimized() const;
    bool wasMinimizedOnSave() const;

    // Handles delete command for the selected shortcut or folder
    void handleDeleteCommand();

    // Drag and drop support - called by DropTarget
    void handleFileDrop(const std::vector<std::wstring>& filePaths);

    LRESULT handleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

   private:
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND listView_ = nullptr;
    std::shared_ptr<libprogman::ShortcutFolder> folder_;
    libprogman::ShortcutManager* shortcutManager_ = nullptr;
    std::function<void(const std::wstring&)> onMinimizeCallback_;
    std::function<void()> onFocusChangeCallback_;
    bool isMinimized_ = false;

    // Drag and drop support
    std::shared_ptr<DropTarget> dropTarget_;
    std::shared_ptr<DragSource> dragSource_;

    // Drag source tracking
    bool isDragging_ = false;
    POINT dragStartPoint_ = {};

    void createListView();
    void refreshListView();
    void setupDragAndDrop();
    void cleanupDragAndDrop();

    // Drag source methods
    void startDrag(int itemIndex);
    bool shouldStartDrag(POINT currentPoint) const;

    // Helper functions for window state
    void saveWindowState(HWND hwnd);
    void restoreWindowState(HWND hwnd);
    std::optional<std::filesystem::path> getIniFilePath() const;

    friend LRESULT CALLBACK FolderWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    friend class ProgramManagerWindow;  // Allow ProgramManagerWindow to access window_
    friend class DropTarget;
};

// DropTarget class to handle drag and drop operations
class DropTarget : public IDropTarget {
   public:
    DropTarget(FolderWindow* folderWindow);

    // IUnknown interface
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IDropTarget interface
    STDMETHODIMP DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    STDMETHODIMP DragLeave() override;
    STDMETHODIMP Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;

   private:
    FolderWindow* folderWindow_;
    ULONG refCount_;
    IDataObject* currentDataObject_ = nullptr;  // Store data object from DragEnter for use in DragOver

    std::vector<std::wstring> extractFilePaths(IDataObject* pDataObj);
    bool canAcceptDrop(IDataObject* pDataObj);
    bool isInternalDragSource(IDataObject* pDataObj);
    bool isSameFolderDrop(IDataObject* pDataObj);
};

// DragSource class to handle drag and drop operations
class DragSource : public IDropSource {
   public:
    DragSource();

    // IUnknown interface
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IDropSource interface
    STDMETHODIMP QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override;
    STDMETHODIMP GiveFeedback(DWORD dwEffect) override;

   private:
    ULONG refCount_;
};

// DataObject class to hold the shortcut data during drag operations
class DataObject : public IDataObject {
   public:
    DataObject(const std::wstring& shortcutPath);

    // IUnknown interface
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IDataObject interface
    STDMETHODIMP GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) override;
    STDMETHODIMP GetDataHere(FORMATETC* pformatetc, STGMEDIUM* pmedium) override;
    STDMETHODIMP QueryGetData(FORMATETC* pformatetc) override;
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC* pformatectIn, FORMATETC* pformatetcOut) override;
    STDMETHODIMP SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease) override;
    STDMETHODIMP EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc) override;
    STDMETHODIMP DAdvise(FORMATETC* pformatetc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection) override;
    STDMETHODIMP DUnadvise(DWORD dwConnection) override;
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA** ppenumAdvise) override;

   private:
    ULONG refCount_;
    std::wstring shortcutPath_;
};

}  // namespace progman
