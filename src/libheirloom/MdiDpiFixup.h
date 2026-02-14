#pragma once

#include <Windows.h>

namespace libheirloom {
    void redrawMdiMenuBarButtons(HWND hwndFrame, HWND hwndMdiClient);
    bool handleMdiMenuBarMouseDown(HWND hwndFrame, HWND hwndMdiClient, LPARAM lParam);
}
