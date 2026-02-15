#include "wfpng.h"
#include "winfile.h"
#include <assert.h>
#include <wincodec.h>

typedef struct {
    HBITMAP hbmOrig;
    UINT origCX, origCY;  // physical pixels of hbmOrig

    // Cached scaled image for the target DPI
    HBITMAP hbmScaled;  // scaled copy (may be NULL)
    UINT scaledCX, scaledCY;
    UINT dpiCached;  // DPI it was built for (0 = none yet)
} PNG_BITMAP;

#define NUM_DRIVE_PNGS 6
#define NUM_ICON_PNGS 18
#define NUM_TOOLBAR_PNGS 8

static BOOL s_initialized = FALSE;
static PNG_BITMAP s_drivePNGs[NUM_DRIVE_PNGS];
static PNG_BITMAP s_iconPNGs[NUM_ICON_PNGS];
static PNG_BITMAP s_toolbarPNGs[NUM_TOOLBAR_PNGS];

static HGLOBAL LoadPNGResource(HINSTANCE hInst, WORD id, DWORD* pcb) {
    HRSRC hrsrc = FindResourceW(hInst, MAKEINTRESOURCEW(id),
                                RT_RCDATA);  // or L"PNG"
    if (!hrsrc) {
        return NULL;
    }

    DWORD cb = SizeofResource(hInst, hrsrc);
    HGLOBAL hmem = LoadResource(hInst, hrsrc);

    *pcb = cb;
    return hmem;  // pointer = LockResource(hmem);
}

static BOOL CreateBitmapFromPNGRes(HINSTANCE hInst, WORD id, PNG_BITMAP* out) {
    *out = {};

    DWORD cb;
    HGLOBAL hmem = LoadPNGResource(hInst, id, &cb);
    if (!hmem)
        return FALSE;

    // copy bytes into a sharable HGLOBAL that IStream can own
    HGLOBAL hdup = GlobalAlloc(GMEM_MOVEABLE, cb);
    assert(hdup);
    {
        LPVOID hdupPtr = GlobalLock(hdup);
        assert(hdupPtr);
        memcpy(hdupPtr, LockResource(hmem), cb);
        GlobalUnlock(hdup);
    }

    IStream* pStream = NULL;
    if (FAILED(CreateStreamOnHGlobal(hdup, TRUE, &pStream)))
        return FALSE;  // stream will now own hdup if TRUE

    IWICImagingFactory* wic = NULL;
    IWICBitmapDecoder* dec = NULL;
    IWICBitmapFrameDecode* frm = NULL;
    IWICFormatConverter* conv = NULL;
    BOOL ok = FALSE;

    if (SUCCEEDED(CoCreateInstance(
            CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&wic)) &&
        SUCCEEDED(wic->CreateDecoderFromStream(pStream, NULL, WICDecodeMetadataCacheOnLoad, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frm)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(
            (IWICBitmapSource*)frm, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.0f,
            WICBitmapPaletteTypeCustom))) {
        UINT w, h;
        conv->GetSize(&w, &h);
        BITMAPINFO bi = { 0 };
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -((LONG)h);  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* pvBits;
        HBITMAP hbm = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &pvBits, NULL, 0);
        if (hbm && SUCCEEDED(conv->CopyPixels(NULL, w * 4, h * w * 4, (LPBYTE)pvBits))) {
            *out = { hbm, w, h, NULL, 0, 0, 0 };
            ok = TRUE;
        }
    }

    if (conv)
        conv->Release();
    if (frm)
        frm->Release();
    if (dec)
        dec->Release();
    if (wic)
        wic->Release();
    if (pStream)
        pStream->Release();
    return ok;
}

static BOOL EnsureScaledForDpi(PNG_BITMAP* png, UINT dpiTarget) {
    if (png->dpiCached == dpiTarget && png->hbmScaled)
        return TRUE;  // already ready

    // dispose of any previous scaled copy
    if (png->hbmScaled) {
        DeleteObject(png->hbmScaled);
        png->hbmScaled = NULL;
    }

    // ----- compute new size -----
    //  16x master  ->  (dpi / 96)x baseline
    //  baseline = master / 16
    UINT cxNew = (png->origCX * dpiTarget) / (96 * 16);
    UINT cyNew = (png->origCY * dpiTarget) / (96 * 16);
    if (cxNew == 0 || cyNew == 0)  // never shrink below 1 px
        cxNew = cyNew = 1;

    // ----- create destination DIB -----
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = cxNew;
    bi.bmiHeader.biHeight = -((LONG)cyNew);  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pvBitsDest;
    HBITMAP hbmDest = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &pvBitsDest, NULL, 0);
    if (!hbmDest)
        return FALSE;

    // ----- high quality WIC resample -----
    IWICImagingFactory* wic = NULL;
    IWICBitmap* pBitmap = NULL;
    IWICBitmapScaler* pScaler = NULL;
    if (SUCCEEDED(CoCreateInstance(
            CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&wic)) &&
        SUCCEEDED(wic->CreateBitmapFromHBITMAP(png->hbmOrig, NULL, WICBitmapUsePremultipliedAlpha, &pBitmap)) &&
        SUCCEEDED(wic->CreateBitmapScaler(&pScaler)) &&
        SUCCEEDED(pScaler->Initialize(pBitmap, cxNew, cyNew, WICBitmapInterpolationModeFant))) {
        pScaler->CopyPixels(NULL, cxNew * 4, cyNew * cxNew * 4, (LPBYTE)pvBitsDest);
    }
    if (pScaler)
        pScaler->Release();
    if (pBitmap)
        pBitmap->Release();
    if (wic)
        wic->Release();

    // commit
    png->hbmScaled = hbmDest;
    png->scaledCX = cxNew;
    png->scaledCY = cyNew;
    png->dpiCached = dpiTarget;
    return TRUE;
}

void PngStartup() {
    if (s_initialized) {
        return;
    }

    HINSTANCE hInst = GetModuleHandle(NULL);

    for (int i = 0; i < NUM_DRIVE_PNGS; i++) {
        CreateBitmapFromPNGRes(hInst, IDR_PNG_DRIVE_00 + i, &s_drivePNGs[i]);
    }

    for (int i = 0; i < NUM_ICON_PNGS; i++) {
        CreateBitmapFromPNGRes(hInst, IDR_PNG_ICON_00 + i, &s_iconPNGs[i]);
    }

    for (int i = 0; i < NUM_TOOLBAR_PNGS; i++) {
        CreateBitmapFromPNGRes(hInst, IDR_PNG_TOOLBAR_00 + i, &s_toolbarPNGs[i]);
    }

    s_initialized = TRUE;
}

void PngShutdown() {
    if (!s_initialized) {
        return;
    }

    for (int i = 0; i < NUM_DRIVE_PNGS; i++) {
        DeleteObject(s_drivePNGs[i].hbmOrig);
        if (s_drivePNGs[i].hbmScaled) {
            DeleteObject(s_drivePNGs[i].hbmScaled);
        }
    }

    for (int i = 0; i < NUM_ICON_PNGS; i++) {
        DeleteObject(s_iconPNGs[i].hbmOrig);
        if (s_iconPNGs[i].hbmScaled) {
            DeleteObject(s_iconPNGs[i].hbmScaled);
        }
    }

    for (int i = 0; i < NUM_TOOLBAR_PNGS; i++) {
        DeleteObject(s_toolbarPNGs[i].hbmOrig);
        if (s_toolbarPNGs[i].hbmScaled) {
            DeleteObject(s_toolbarPNGs[i].hbmScaled);
        }
    }

    s_initialized = FALSE;
}

void PngDraw(HDC hdc, UINT dpi, int x, int y, PNG_TYPE type, int index) {
    if (!s_initialized) {
        return;
    }

    PNG_BITMAP* png = NULL;
    switch (type) {
        case PNG_TYPE_DRIVE:
            png = &s_drivePNGs[index];
            break;
        case PNG_TYPE_ICON:
            png = &s_iconPNGs[index];
            break;
        case PNG_TYPE_TOOLBAR:
            png = &s_toolbarPNGs[index];
            break;
        default:
            return;
    }

    if (!EnsureScaledForDpi((PNG_BITMAP*)png, dpi)) {
        return;
    }

    // Draw the scaled bitmap with alpha blending
    HDC hdcMemScaled = CreateCompatibleDC(hdc);
    if (!hdcMemScaled) {
        return;
    }

    HGDIOBJ hOld = SelectObject(hdcMemScaled, png->hbmScaled);
    if (!hOld) {
        DeleteDC(hdcMemScaled);
        return;
    }

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (!AlphaBlend(hdc, x, y, png->scaledCX, png->scaledCY, hdcMemScaled, 0, 0, png->scaledCX, png->scaledCY, bf)) {
        SelectObject(hdcMemScaled, hOld);
        DeleteDC(hdcMemScaled);
        return;
    }

    if (!SelectObject(hdcMemScaled, hOld)) {
        DeleteDC(hdcMemScaled);
        return;
    }

    DeleteDC(hdcMemScaled);
}

void PngGetScaledSize(UINT dpi, PNG_TYPE type, int index, UINT* cx, UINT* cy) {
    if (!s_initialized) {
        *cx = *cy = 0;
        return;
    }

    PNG_BITMAP* png = NULL;
    switch (type) {
        case PNG_TYPE_DRIVE:
            png = &s_drivePNGs[index];
            break;
        case PNG_TYPE_ICON:
            png = &s_iconPNGs[index];
            break;
        case PNG_TYPE_TOOLBAR:
            png = &s_toolbarPNGs[index];
            break;
        default:
            *cx = *cy = 0;
            return;
    }

    if (!EnsureScaledForDpi(png, dpi)) {
        *cx = *cy = 0;
        return;
    }

    *cx = png->scaledCX;
    *cy = png->scaledCY;
}
