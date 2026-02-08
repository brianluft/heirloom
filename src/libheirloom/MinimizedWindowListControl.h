#pragma once

#include "libheirloom/pch.h"

namespace libheirloom {

class MinimizedWindowListControl {
   public:
    MinimizedWindowListControl(HINSTANCE instance, HWND mdiParent);
    virtual ~MinimizedWindowListControl();
    MinimizedWindowListControl(const MinimizedWindowListControl&) = delete;
    MinimizedWindowListControl& operator=(const MinimizedWindowListControl&) = delete;
    MinimizedWindowListControl(MinimizedWindowListControl&&) = delete;
    MinimizedWindowListControl& operator=(MinimizedWindowListControl&&) = delete;

    HWND hwnd() const;
    void addItem(const std::wstring& name, HICON icon);
    void removeItem(const std::wstring& name);
    int autoSize(HWND mdiClient);

    int getSplitterPosition() const { return controlHeight_; }
    void setSplitterPosition(int height);

   protected:
    virtual void onItemDoubleClick(const std::wstring& name);
    virtual void onItemRightClick(const std::wstring& name, POINT screenPos);
    virtual bool onItemLabelEdit(const std::wstring& oldName, const std::wstring& newName);
    virtual void onSelectionChanged();
    virtual void onFocusChanged(bool hasFocus);

    HWND getListView() const { return listView_; }
    std::wstring getSelectedItemName() const;
    bool hasSelectedItem() const;
    void startEditingSelectedItem();

   private:
    HWND window_ = nullptr;
    HWND listView_ = nullptr;

    // Splitter-related members
    int controlHeight_ = 0;
    int splitterHeight_ = 0;
    bool isSplitterHover_ = false;
    bool isDragging_ = false;
    int dragStartY_ = 0;
    POINT dragStartScreen_ = {0, 0};
    int initialHeight_ = 0;

    // Track if mouse tracking is enabled
    bool isTrackingMouse_ = false;

    // Initialize mouse tracking for hover effects
    void initMouseTracking();

    // Check if a point is within the splitter area
    bool isPointInSplitter(int y) const;

    // Handle splitter painting
    void paintSplitter(HDC hdc);

    friend LRESULT CALLBACK MinimizedWindowListControlProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);
    friend LRESULT CALLBACK MinimizedWindowListViewSubclassProc(
        HWND hwnd,
        UINT uMsg,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR uIdSubclass,
        DWORD_PTR dwRefData);
};

}  // namespace libheirloom
