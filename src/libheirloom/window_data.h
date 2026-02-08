#pragma once

#include "libheirloom/pch.h"

namespace libheirloom {

void setWindowDataImpl(HWND hwnd, void* data);
void* getWindowDataImpl(HWND hwnd);

template <typename T>
void setWindowData(HWND hwnd, T* data) {
    setWindowDataImpl(hwnd, data);
}

template <typename T>
T* getWindowData(HWND hwnd) {
    return reinterpret_cast<T*>(getWindowDataImpl(hwnd));
}

}  // namespace libheirloom
