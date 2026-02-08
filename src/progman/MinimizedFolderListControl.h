#pragma once

#include "progman/pch.h"
#include "libheirloom/MinimizedWindowListControl.h"

namespace progman {

class MinimizedFolderListControl : public libheirloom::MinimizedWindowListControl {
   public:
    MinimizedFolderListControl(HINSTANCE instance, HWND parent, std::function<void(std::wstring)> onRestore);
    ~MinimizedFolderListControl() override;
    MinimizedFolderListControl(const MinimizedFolderListControl&) = delete;
    MinimizedFolderListControl& operator=(const MinimizedFolderListControl&) = delete;
    MinimizedFolderListControl(MinimizedFolderListControl&&) = delete;
    MinimizedFolderListControl& operator=(MinimizedFolderListControl&&) = delete;

    void addMinimizedFolder(std::wstring name);
    void removeMinimizedFolder(const std::wstring& name);
    bool restoreMinimizedFolder(const std::wstring& folderName, bool maximize = false);

    void setOnRenameCallback(std::function<void(const std::wstring&, const std::wstring&)> callback) {
        onRename_ = std::move(callback);
    }

    void setOnDeleteCallback(std::function<void(const std::wstring&)> callback) { onDelete_ = std::move(callback); }

    void setOnFocusChangeCallback(std::function<void()> callback) { onFocusChange_ = std::move(callback); }

    bool hasSelectedItemAndFocus() const;
    std::wstring getSelectedFolderName() const;
    void startEditingSelectedFolder();

   protected:
    void onItemDoubleClick(const std::wstring& name) override;
    void onItemRightClick(const std::wstring& name, POINT screenPos) override;
    bool onItemLabelEdit(const std::wstring& oldName, const std::wstring& newName) override;
    void onSelectionChanged() override;
    void onFocusChanged(bool hasFocus) override;

   private:
    HINSTANCE instance_ = nullptr;
    std::function<void(std::wstring)> onRestore_;
    std::function<void(const std::wstring&, const std::wstring&)> onRename_;
    std::function<void(const std::wstring&)> onDelete_;
    std::function<void()> onFocusChange_;
    HICON folderIcon_ = nullptr;

    friend LRESULT CALLBACK FolderListViewSubclassProc(
        HWND hwnd,
        UINT uMsg,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR uIdSubclass,
        DWORD_PTR dwRefData);
};

}  // namespace progman
