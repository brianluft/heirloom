/********************************************************************

   lfn.h

   declaration of lfn aware functions

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#pragma once

#define CCHMAXFILE MAXPATHLEN  // max size of a long name
#define CCHMAXPATHCOMP 256
#define LFNCANON_MASK 1
#define PATHLEN1_1 13

#define FILE_83_CI 0
#define FILE_83_CS 1
#define FILE_LONG 2

#define ERROR_OOM 8

// we need to add an extra field to distinguish DOS vs. LFNs

typedef struct {
    HANDLE hFindFile;    // handle returned by FindFirstFile()
    DWORD dwAttrFilter;  // search attribute mask.
    DWORD err;           // error info if failure.
    WIN32_FIND_DATA fd;  // FindFirstFile() data structure;
    int nSpaceLeft;      // Space left for deeper paths
} LFNDTA, *LPLFNDTA, *PLFNDTA;

void LFNInit();
void InvalidateVolTypes();

DWORD GetNameType(LPWSTR);
BOOL IsLFN(LPWSTR pName);
// BOOL  IsLFNDrive(LPWSTR);

BOOL WFFindFirst(LPLFNDTA lpFind, LPWSTR lpName, DWORD dwAttrFilter);
BOOL WFFindNext(LPLFNDTA);
BOOL WFFindClose(LPLFNDTA);

DWORD I_LFNCanon(USHORT CanonType, LPWSTR InFile, LPWSTR OutFile);
DWORD LFNParse(LPWSTR, LPWSTR, LPWSTR);
WORD I_LFNEditName(LPWSTR lpSrc, LPWSTR lpEd, LPWSTR lpRes, int iResBufSize);

BOOL WFIsDir(LPWSTR);
BOOL LFNMergePath(LPWSTR, LPWSTR);

BOOL IsLFNSelected();

DWORD WFCopy(LPWSTR, LPWSTR);
DWORD WFRemove(LPWSTR pszFile);
DWORD WFMove(LPWSTR pszFrom, LPWSTR pszTo, PBOOL pbErrorOnDest, BOOL bSilent);
DWORD WFCopyIfSymlink(LPWSTR pszFrom, LPWSTR pszTo, DWORD dwFlags, DWORD dwNotification);
DWORD DecodeReparsePoint(LPCWSTR szMyFile, LPWSTR szDest, DWORD cwcDest);
DWORD WFHardLink(LPWSTR pszFrom, LPWSTR pszTo);
DWORD WFSymbolicLink(LPWSTR pszFrom, LPWSTR pszTo, DWORD dwFlags);
DWORD WFJunction(LPCWSTR LinkDirectory, LPCWSTR LinkTarget);
