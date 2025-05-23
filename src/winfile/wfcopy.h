/********************************************************************

   wfcopy.h

   Include for WINFILE's File Copying Routines

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#pragma once

#include "winfile.h"
#include "lfn.h"

#define FIND_DIRS 0x0010

#define CNF_DIR_EXISTS 0x0001
#define CNF_ISDIRECTORY 0x0002

#define BUILD_TOPLEVEL 0
#define BUILD_RECURSING 1
#define BUILD_NORECURSE 2

#define OPER_MASK 0x0F00
#define OPER_MKDIR 0x0100
#define OPER_RMDIR 0x0200
#define OPER_DOFILE 0x0300
#define OPER_ERROR 0x0400

#define CCHPATHMAX 260
#define MAXDIRDEPTH CCHPATHMAX / 2

#define ATTR_ATTRIBS 0x200 /* Flag indicating we have file attributes */
#define ATTR_COPIED 0x400  /* we have copied this file */
#define ATTR_DELSRC 0x800  /* delete the source when done */

typedef struct _copyroot {
    BOOL fRecurse : 1;
    BOOL bFastMove : 1;
    WORD cDepth;
    LPWSTR pSource;
    LPWSTR pRoot;
    WCHAR cIsDiskThereCheck[26];
    WCHAR sz[MAXPATHLEN];
    WCHAR szDest[MAXPATHLEN];
    LFNDTA rgDTA[MAXDIRDEPTH];
} COPYROOT, *PCOPYROOT;

DWORD FileMove(LPWSTR, LPWSTR, PBOOL, BOOL);
DWORD FileRemove(LPWSTR);
DWORD MKDir(LPWSTR, LPWSTR);
DWORD RMDir(LPWSTR);
BOOL WFSetAttr(LPWSTR lpFile, DWORD dwAttr);

void AppendToPath(LPWSTR, LPCWSTR);
UINT RemoveLast(LPWSTR pFile);
void Notify(HWND, WORD, LPCWSTR, LPCWSTR);

LPWSTR FindFileName(LPWSTR pPath);

DWORD DMMoveCopyHelper(LPWSTR pFrom, LPWSTR pTo, int iOperation);
DWORD WFMoveCopyDriver(PCOPYINFO pCopyInfo);
DWORD WINAPI WFMoveCopyDriverThread(LPVOID lpParameter);

BOOL IsDirectory(LPWSTR pPath);
BOOL IsTheDiskReallyThere(HWND hwnd, LPWSTR pPath, DWORD wFunc, BOOL bModal);
BOOL QualifyPath(LPWSTR);
int CheckMultiple(LPWSTR pInput);
void SetDlgItemPath(HWND hDlg, int id, LPCWSTR pszPath);

void DialogEnterFileStuff(HWND hwnd);