#pragma once

#define DIALOGRESIZECONTROLCLASSW L"DialogResizeControlClassW"
#define DIALOGRESIZECONTROLCLASSA "DialogResizeControlClassA"

#define DIALOGRESIZECONTROLCLASS DIALOGRESIZECONTROLCLASSW

#define DIALOGRESIZEDATACLASSW L"DialogResizeDataClassW"
#define DIALOGRESIZEDATACLASSA "DialogResizeDataClassA"

#define DIALOGRESIZEDATACLASS DIALOGRESIZEDATACLASSW

#ifndef RC_INVOKED

BOOL CALLBACK ResizeDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

BOOL ResizeDialogInitialize(HINSTANCE hInst);

#else

#define DIALOGRESIZE CONTROL "", -1, DIALOGRESIZEDATACLASS, NOT WS_VISIBLE, 0, 0, 0, 0, 0
#define DIALOGRESIZECONTROL CONTROL "", -1, DIALOGRESIZECONTROLCLASS, NOT WS_VISIBLE, 0, 0, 0, 0, 0

#endif
