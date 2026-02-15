#pragma once

#include <Windows.h>

namespace libheirloom {

struct NcMessageResult {
    bool handled;
    LRESULT lResult;
};

// Call from every MDI child WndProc before the switch statement.
// Returns handled=true if the message was consumed (caller returns lResult).
// Returns handled=false if the caller should fall through to DefMDIChildProc.
NcMessageResult handleMdiChildNcMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

}
