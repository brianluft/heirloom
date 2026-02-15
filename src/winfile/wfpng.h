#pragma once

#include <windows.h>

typedef enum { PNG_TYPE_DRIVE, PNG_TYPE_ICON, PNG_TYPE_TOOLBAR } PNG_TYPE;

void PngStartup();
void PngShutdown();
void PngDraw(HDC hdc, UINT dpi, int x, int y, PNG_TYPE type, int index);
void PngGetScaledSize(UINT dpi, PNG_TYPE type, int index, UINT* cx, UINT* cy);
