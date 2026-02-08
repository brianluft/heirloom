#pragma once

#include <windows.h>

void InitMinimizedWindowBar(HINSTANCE hInstance, HWND hwndMDIClient);
void DestroyMinimizedWindowBar();
void MinBarAddWindow(HWND hwndChild);
void MinBarRemoveWindow(HWND hwndChild);
void MinBarAutoSize();
int MinBarGetHeight();
void MinBarSetHeight(int height);
