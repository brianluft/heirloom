#include "libheirloom/pch.h"
#include "libheirloom/window_data.h"

namespace libheirloom {

static LPCWSTR getKeyAtom() {
    static const LPCWSTR atom = MAKEINTATOM(GlobalAddAtomW(L"HEIRLOOM_WINDOW_DATA"));
    return atom;
}

void setWindowDataImpl(HWND hwnd, void* data) {
    SetPropW(hwnd, getKeyAtom(), data);
}

void* getWindowDataImpl(HWND hwnd) {
    return GetPropW(hwnd, getKeyAtom());
}

}  // namespace libheirloom
