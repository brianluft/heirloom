/**************************************************************************

   winfile.h

   Include for WINFILE program

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

**************************************************************************/

#pragma once

#include <string>
#include <optional>

#define TOOLBAR
#define NOCOMM
#define WIN31
#define NTFS

#include <windows.h>
#include <windowsX.h>
#include <setjmp.h>
#include <string.h>
#include <memory.h>
#include "mpr.h"
#include <npapi.h>
#include "wfext.h"
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <ole2.h>  // Add explicit OLE header for drag-drop interfaces
#include "suggest.h"
#include "numfmt.h"

#include "wfexti.h"

#include "wfdocb.h"
#include "wfmem.h"
#include "res.h"

#include <string>
#include <vector>
#include <filesystem>
#include <system_error>
#include "libheirloom/cancel.h"

#define STKCHK()

#ifdef atoi
#undef atoi
#endif

typedef WCHAR TUCHAR, *PTUCHAR;

#define atoi atoiW
int atoiW(LPCWSTR sz);

#define SIZENOMDICRAP 944
#define MAX_TAB_COLUMNS 10

#define MAXDOSFILENAMELEN (12 + 1)              // includes the NULL
#define MAXDOSPATHLEN (68 + MAXDOSFILENAMELEN)  // includes the NULL

#define MAXLFNFILENAMELEN 1024
#define MAXLFNPATHLEN 1024

#define MAXFILENAMELEN MAXLFNFILENAMELEN
#define MAXPATHLEN MAXLFNPATHLEN

#define MAXTITLELEN 128
#define MAXSUGGESTLEN 260  // for non-expanding suggest message
#define MAXERRORLEN (MAXPATHLEN + MAXSUGGESTLEN)
#define MAXMESSAGELEN (MAXPATHLEN * 2 + MAXSUGGESTLEN)

#define MAX_WINDOWS 27
#define MAX_DRIVES 26

// struct for volume info

#define MAX_VOLNAME MAXPATHLEN
#define MAX_FILESYSNAME MAXPATHLEN

// Maximum size of an extension, including NULL
#define EXTSIZ 8

#define TA_LOWERCASE 0x01
#define TA_BOLD 0x02
#define TA_ITALIC 0x04
#define TA_LOWERCASEALL 0x08

#define FF_NULL 0x0
#define FF_ONLYONE 0x1000
#define FF_PRELOAD 0x2000
#define FF_RETRY 0x4000

#define SZ_NTLDR L"NTLDR"

#define SZ_DQUOTE L"\""
#define SZ_DOT L"."
#define SZ_DOTDOT L".."
#define SZ_QUESTION L"?"
#define SZ_ACOLONSLASH L"A:\\"
#define SZ_ACOLON L"A:"

#define SZ_PERCENTD L"%d"
#define SZ_PERCENTFORMAT L"%3d%%"

#define SZ_NTFSNAME L"NTFS"
#define SZ_FATNAME L"FAT"
#define SZ_FILESYSNAMESEP L" - "
#define SZ_CLOSEBRACK L"]"
#define SZ_BACKSLASH L"\\"
#define SZ_COLON L":"
#define SZ_STAR L"*"
#define SZ_DOTSTAR L".*"
#define SZ_COLONONE L":1"
#define SZ_SPACEDASHSPACE L" - "

#define CHAR_DASH TEXT('-')
#define CHAR_CARET TEXT('^')
#define CHAR_UNDERSCORE TEXT('_')
#define CHAR_AND TEXT('&')
#define CHAR_TAB TEXT('\t')
#define CHAR_LESS TEXT('<')
#define CHAR_GREATER TEXT('>')
#define CHAR_EQUAL TEXT('=')
#define CHAR_PLUS TEXT('+')
#define CHAR_SEMICOLON TEXT(';')
#define CHAR_COMMA TEXT(',')
#define CHAR_PIPE TEXT('|')
#define CHAR_BACKSLASH TEXT('\\')
#define CHAR_SLASH TEXT('/')
#define CHAR_OPENBRACK TEXT('[')
#define CHAR_CLOSEBRACK TEXT(']')
#define CHAR_ZERO TEXT('0')
#define CHAR_COLON TEXT(':')
#define CHAR_SPACE TEXT(' ')
#define CHAR_NEWLINE TEXT('\n')

#define CHAR_DOT TEXT('.')
#define CHAR_OPENPAREN TEXT('(')
#define CHAR_CLOSEPAREN TEXT(')')
#define CHAR_HASH TEXT('#')
#define CHAR_DQUOTE TEXT('"')

#define CHAR_NULL TEXT('\0')
#define CHAR_QUESTION TEXT('?')
#define CHAR_STAR TEXT('*')
#define CHAR_PERCENT TEXT('%')

#define CHAR_A TEXT('A')
#define CHAR_a TEXT('a')
#define CHAR_Z TEXT('Z')

// Default char for untranslatable unicode
// MUST NOT BE an acceptable char for file systems!!
// (GetNextPair scans for this and uses altname)
#define CHAR_DEFAULT CHAR_QUESTION

#define FM_EXT_PROC_ENTRYA "FMExtensionProc"
#define FM_EXT_PROC_ENTRYW "FMExtensionProcW"

#define FILE_NOTIFY_CHANGE_FLAGS (FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE)

#define DwordAlign(cb) (((cb) + 3) & ~3)
#define ISDOTDIR(x) (x[0] == CHAR_DOT && (!x[1] || (x[1] == CHAR_DOT && !x[2])))
#define ISUNCPATH(x) (CHAR_BACKSLASH == x[0] && CHAR_BACKSLASH == x[1])
#define DRIVESET(str, drive) (str[0] = CHAR_A + (drive))
#define COUNTOF(x) (sizeof(x) / sizeof(*x))
#define ByteCountOf(x) ((x) * sizeof(WCHAR))

#define DRIVEID(path) ((path[0] - CHAR_A) & 31)

#define IsDocument(lpszPath) IsBucketFile(lpszPath, ppDocBucket)
#define IsProgramFile(lpszPath) IsBucketFile(lpszPath, ppProgBucket)
#define IsProgramIconFile(lpszPath) IsBucketFile(lpszPath, ppProgIconBucket)

//
// Some typedefs
//

typedef HWND* PHWND;
typedef int DRIVE;
typedef int DRIVEIND;

#include "wfinfo.h"

typedef struct _COPYINFO {
    LPWSTR pFrom;
    LPWSTR pTo;
    DWORD dwFunc;
    BOOL bUserAbort;
} COPYINFO, *PCOPYINFO;

typedef enum eISELTYPE {
    SELTYPE_ALL = 0,
    SELTYPE_FIRST = 1,
    SELTYPE_TESTLFN = 2,
    SELTYPE_QUALIFIED = 4,
    SELTYPE_FILESPEC = 8,
    SELTYPE_NOCHECKESC = 16,
    SELTYPE_SHORTNAME = 32
} ISELTYPE;

// struct for save and restore of window positions

typedef struct {
    //
    // *2 since may have huge filter
    //
    WCHAR szDir[2 * MAXPATHLEN];

    //
    // Next block of fields must be together (11 DWORDS)
    //
    RECT rc;
    POINT pt;
    int sw;
    DWORD dwView;
    DWORD dwSort;
    int nSplit;
} WINDOW, *PWINDOW;

typedef struct _SELINFO* PSELINFO;

//--------------------------------------------------------------------------
//
//  Function Templates (winfile.cpp)
//
//--------------------------------------------------------------------------

BOOL InitPopupMenu(const std::wstring& popupName, HMENU hMenu, HWND hwndActive);
LRESULT CALLBACK MessageFilter(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK FrameWndProc(HWND hWnd, UINT wMsg, WPARAM wParam, LPARAM lParam);

//--------------------------------------------------------------------------
//
//  Defines
//
//--------------------------------------------------------------------------

#define SST_RESOURCE 0X1
#define SST_FORMAT 0x2

#define DRIVE_INFO_NAME_HEADER 4

#define WS_DIRSTYLE                                                                                   \
    (WS_CHILD | LBS_SORT | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_EXTENDEDSEL | LBS_NOINTEGRALHEIGHT | \
     LBS_WANTKEYBOARDINPUT)

//
// Extra Window Word Offsets
//

// NOTE: see winfile.c for a description of the overall window structure.

//
// Idx  Tree         Search         Dir
// 0    SPLIT        HDTA           HDTA
// 1    PATHLEN      TABARRAY       TABARRAY
// 2    VOLNAME      LISTPARMS      LISTPARMS
// 3    NOTIFYPAUSE  IERROR         IERROR
// 4    TYPE         TYPE           HDTAABORT
// 5    VIEW         VIEW           INITIALDIRSEL
// 6    SORT         SORT           NEXTHWND
// 7    OLEDROP      n/a            OLEDROP
// 8    ATTRIBS      ATTRIBS
// 9    FCSFLAG      FSCFLAG
// 10   LASTFOCUS    LASTFOCUS
//

#define GWL_SPLIT (0 * sizeof(LONG_PTR))
#define GWL_HDTA (0 * sizeof(LONG_PTR))

#define GWL_PATHLEN (1 * sizeof(LONG_PTR))
#define GWL_TABARRAY (1 * sizeof(LONG_PTR))

#define GWL_VOLNAME (2 * sizeof(LONG_PTR))
#define GWL_LISTPARMS (2 * sizeof(LONG_PTR))

#define GWL_NOTIFYPAUSE (3 * sizeof(LONG_PTR))
#define GWL_IERROR (3 * sizeof(LONG_PTR))

#define GWL_TYPE (4 * sizeof(LONG_PTR))  // > 0 Tree, -1 = search
#define GWL_HDTAABORT (4 * sizeof(LONG_PTR))

#define GWL_VIEW (5 * sizeof(LONG_PTR))
#define GWL_SELINFO (5 * sizeof(LONG_PTR))

#define GWL_SORT (6 * sizeof(LONG_PTR))
#define GWL_NEXTHWND (6 * sizeof(LONG_PTR))

#define GWL_OLEDROP (7 * sizeof(LONG_PTR))

#define GWL_FSCFLAG (8 * sizeof(LONG_PTR))

#define GWL_LASTFOCUS (9 * sizeof(LONG_PTR))

// szDrivesClass...

#define GWL_CURDRIVEIND (0 * sizeof(LONG_PTR))    // current selection in drives window
#define GWL_CURDRIVEFOCUS (1 * sizeof(LONG_PTR))  // current focus in drives window
#define GWL_LPTSTRVOLUME (2 * sizeof(LONG_PTR))   // LPWSTR to Volume/Share string

// szTreeControlClass

#define GWL_READLEVEL (0 * sizeof(LONG_PTR))  // iReadLevel for each tree control window
#define GWL_XTREEMAX (1 * sizeof(LONG_PTR))   // max text extent for each tree control window

// GWL_TYPE numbers

#define TYPE_TREE 0  // and all positive numbers (drive number)
#define TYPE_SEARCH -1

/* WM_FILESYSCHANGE (WM_FSC) message wParam value */
#define FSC_CREATE 0x0000
#define FSC_DELETE 0x0001
#define FSC_RENAME 0x0002
#define FSC_ATTRIBUTES 0x0003
#define FSC_NETCONNECT 0x0004
#define FSC_NETDISCONNECT 0x0005
#define FSC_REFRESH 0x0006
#define FSC_MKDIR 0x0007
#define FSC_RMDIR 0x0008
#define FSC_JUNCTION 0x0009
#define FSC_SYMLINKD 0x000A

#define FSC_QUIET 0x8000
#define FSC_OPERATIONMASK 0x00ff

#define FSC_Operation(FSC) ((FSC) & FSC_OPERATIONMASK)

#define WM_LBTRACKPT 0x131

#define TC_SETDRIVE 0x944
#define TC_GETCURDIR 0x945
#define TC_EXPANDLEVEL 0x946
#define TC_COLLAPSELEVEL 0x947
#define TC_GETDIR 0x948
#define TC_SETDIRECTORY 0x949
#define TC_TOGGLELEVEL 0x950
#define TC_RECALC_EXTENT 0x951

#define FS_CHANGEDISPLAY (WM_USER + 0x100)
#define FS_CHANGEDRIVES (WM_USER + 0x101)
#define FS_GETSELECTION (WM_USER + 0x102)
#define FS_GETDIRECTORY (WM_USER + 0x103)
#define FS_GETDRIVE (WM_USER + 0x104)
#define FS_SETDRIVE (WM_USER + 0x107)
#define FS_GETFILESPEC (WM_USER + 0x108)
#define FS_SETSELECTION (WM_USER + 0x109)

#define FS_SEARCHEND (WM_USER + 0x10C)
#define FS_SEARCHLINEINSERT (WM_USER + 0x10D)

#define FS_SEARCHUPDATE (WM_USER + 0x10E)

#define FS_UPDATEDRIVETYPECOMPLETE (WM_USER + 0x112)
#define FS_UPDATEDRIVELISTCOMPLETE (WM_USER + 0x113)
#define FS_FSCREQUEST (WM_USER + 0x114)
#define FS_NOTIFYRESUME (WM_USER + 0x115)
#define FS_COPYDONE (WM_USER + 0x116)
#define FS_DIRREADDONE (WM_USER + 0x117)
#define FS_REBUILDDOCSTRING (WM_USER + 0x118)

#define FS_TESTEMPTY (WM_USER + 0x119)

#define WM_FSC (WM_USER + 0x120)

#define FS_ENABLEFSC (WM_USER + 0x121)
#define FS_DISABLEFSC (WM_USER + 0x122)

#define ATTR_READWRITE 0x0000
#define ATTR_READONLY FILE_ATTRIBUTE_READONLY                // == 0x0001
#define ATTR_HIDDEN FILE_ATTRIBUTE_HIDDEN                    // == 0x0002
#define ATTR_SYSTEM FILE_ATTRIBUTE_SYSTEM                    // == 0x0004
#define ATTR_VOLUME 0x0008                                   // == 0x0008
#define ATTR_DIR FILE_ATTRIBUTE_DIRECTORY                    // == 0x0010
#define ATTR_ARCHIVE FILE_ATTRIBUTE_ARCHIVE                  // == 0x0020
#define ATTR_NORMAL FILE_ATTRIBUTE_NORMAL                    // == 0x0080
#define ATTR_TEMPORARY FILE_ATTRIBUTE_TEMPORARY              // == 0x0100
#define ATTR_REPARSE_POINT FILE_ATTRIBUTE_REPARSE_POINT      // == 0x0400
#define ATTR_COMPRESSED FILE_ATTRIBUTE_COMPRESSED            // == 0x0800
#define ATTR_NOT_INDEXED FILE_ATTRIBUTE_NOT_CONTENT_INDEXED  // == 0x2000
#define ATTR_ENCRYPTED FILE_ATTRIBUTE_ENCRYPTED              // == 0x4000
#define ATTR_USED 0x6DBF                                     // ATTR we use that are returned from FindFirst/NextFile

#define ATTR_PARENT 0x0040  // my hack DTA bits
#define ATTR_LFN 0x10000    // my hack DTA bits
#define ATTR_JUNCTION 0x20000
#define ATTR_SYMBOLIC 0x40000
#define ATTR_LOWERCASE 0x80000

#define ATTR_RWA (ATTR_READWRITE | ATTR_ARCHIVE)
#define ATTR_ALL                                                                                           \
    (ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIR | ATTR_ARCHIVE | ATTR_NORMAL | ATTR_COMPRESSED | \
     ATTR_ENCRYPTED | ATTR_REPARSE_POINT)
#define ATTR_HS (ATTR_HIDDEN | ATTR_SYSTEM)

#define ATTR_RETURNED 0x8000 /* used in DTA's by copy */

#define CD_PATH 0x0001
#define CD_VIEW 0x0002
#define CD_SORT 0x0003
#define CD_PATH_FORCE 0x0004
#define CD_SEARCHUPDATE 0x0005
#define CD_SEARCHFONT 0x0006

#define CD_DONTSTEAL 0x4000
#define CD_ALLOWABORT 0x8000

#define VIEW_NAMEONLY 0x0000
#define VIEW_UPPERCASE 0x0001
#define VIEW_SIZE 0x0002
#define VIEW_DATE 0x0004
#define VIEW_TIME 0x0008
#define VIEW_FLAGS 0x0010
// VIEW_PLUSES removed, was 0x0020
#define VIEW_DOSNAMES 0x0040

#define VIEW_EVERYTHING (VIEW_SIZE | VIEW_TIME | VIEW_DATE | VIEW_FLAGS)

#define CBSECTORSIZE 512

#define INT13_READ 2
#define INT13_WRITE 3

#define ERR_USER 0xF000

/* Child Window IDs */
#define IDCW_DIR 2
#define IDCW_TREELISTBOX 3  // list in tree control
#define IDCW_TREECONTROL 5
#define IDCW_LISTBOX 6  // list in directory and search

#define HasDirWindow(hwnd) GetDlgItem(hwnd, IDCW_DIR)
#define HasTreeWindow(hwnd) GetDlgItem(hwnd, IDCW_TREECONTROL)
#define GetSplit(hwnd) ((int)GetWindowLongPtr(hwnd, GWL_SPLIT))

/* Indexes into the mondo bitmap */
#define BM_IND_APP 0
#define BM_IND_DOC 1
#define BM_IND_FIL 2
#define BM_IND_RO 3
#define BM_IND_DIRUP 4
#define BM_IND_CLOSE 5
#define BM_IND_OPEN 7
#define BM_IND_CLOSEDFS 11
#define BM_IND_OPENDFS 12
#define BM_IND_CLOSEREPARSE 15
#define BM_IND_OPENREPARSE 16
#define BM_IND_FILREPARSE 17

typedef struct _DRIVE_INFO {
    int iBusy;
    BOOL bRemembered : 1;
    BOOL bUpdating : 1;

    //-----------------------------------
    STATUSNAME(Type);
    UINT uType;

    //-----------------------------------
    int iOffset;

    //-----------------------------------
    STATUSNAME(NetCon);
    LPWNET_CONNECTIONINFO lpConnectInfo;
    DWORD dwConnectInfoMax;

    DWORD dwAltNameError;
    LPWSTR lpszRemoteNameMinusFour[MAX_ALTNAME];
    DWORD dwRemoteNameMax[MAX_ALTNAME];
    DWORD dwLines[MAX_ALTNAME];

    //-----------------------------------
    STATUSNAME(VolInfo);
    DWORD dwVolumeSerialNumber;
    DWORD dwMaximumComponentLength;
    DWORD dwFileSystemFlags;
    DWORD dwDriveType;
    DWORD dwVolNameMax;
    WCHAR szVolNameMinusFour[MAX_VOLNAME + DRIVE_INFO_NAME_HEADER];
    // there is no easy way (+4hdr)
    WCHAR szFileSysName[MAX_FILESYSNAME];  // to predetermine length

    //-----------------------------------
    BOOL bShareChkTried : 1;
    BOOL bShareChkFail : 1;

    STATUSNAME(Space);
    LARGE_INTEGER qFreeSpace;
    LARGE_INTEGER qTotalSpace;
} DRIVEINFO, *PDRIVEINFO;

#define SC_SPLIT 100

#define DRIVEBAR_FLAG 2

#ifdef _GLOBALS
#define Extern
#define EQ(x) = x
#else
#define Extern extern
#define EQ(x)
#endif

//----------------------------
//
//  Lazy load network support
//
//----------------------------

#define ACLEDIT_DLL L"acledit.dll"
#define MPR_DLL L"mpr.dll"
#define NTDLL_DLL L"ntdll.dll"
#define NTSHRUI_DLL L"Ntshrui.dll"

#define WAITNET() WaitLoadEvent(TRUE)
#define WAITACLEDIT() WaitLoadEvent(FALSE)

#define WAITNET_DONE bNetDone
#define WAITNET_ACLEDITDONE bNetAcleditDone

#define WAITNET_LOADED bNetLoad
#define WAITNET_TYPELOADED bNetTypeLoad
#define WAITNET_SHARELOADED bNetShareLoad

// Start mpr.dll functions
typedef DWORD(CALLBACK* PFNWNETGETDIRECTORYTYPEW)(LPWSTR, LPDWORD, BOOL);
Extern PFNWNETGETDIRECTORYTYPEW lpfnWNetGetDirectoryTypeW;
#define WNetGetDirectoryType (*lpfnWNetGetDirectoryTypeW)

typedef DWORD(CALLBACK* PFNWNETGETPROPERTYTEXTW)(WORD, WORD, LPWSTR, LPWSTR, WORD, WORD);
Extern PFNWNETGETPROPERTYTEXTW lpfnWNetGetPropertyTextW;
#define WNetGetPropertyText (*lpfnWNetGetPropertyTextW)

typedef DWORD(CALLBACK* PFNWNETRESTORESINGLECONNECTIONW)(HWND, LPWSTR, BOOL);
Extern PFNWNETRESTORESINGLECONNECTIONW lpfnWNetRestoreSingleConnectionW;
#define WNetRestoreSingleConnection (*lpfnWNetRestoreSingleConnectionW)

typedef DWORD(CALLBACK* PFNWNETPROPERTYDIALOGW)(HWND, WORD, WORD, LPWSTR, WORD);
Extern PFNWNETPROPERTYDIALOGW lpfnWNetPropertyDialogW;
#define WNetPropertyDialog (*lpfnWNetPropertyDialogW)

typedef DWORD(CALLBACK* PFNWNETGETCONNECTION2W)(LPWSTR, LPVOID, LPDWORD);
Extern PFNWNETGETCONNECTION2W lpfnWNetGetConnection2W;
#define WNetGetConnection2 (*lpfnWNetGetConnection2W)

typedef DWORD(CALLBACK* PFNWNETFORMATNETWORKNAMEW)(
    LPCWSTR lpProvider,
    LPCWSTR lpRemoteName,
    LPWSTR lpFormattedName,
    LPDWORD lpnLength,
    DWORD dwFlags,
    DWORD dwAveCharPerLine);
Extern PFNWNETFORMATNETWORKNAMEW lpfnWNetFormatNetworkNameW;
#define WNetFormatNetworkName (*lpfnWNetFormatNetworkNameW)
// End mpr.dll

Extern FM_EXT_PROC lpfnAcledit;
Extern BOOL bSecMenuDeleted;

Extern HMODULE hVersion EQ(NULL);
Extern HMODULE hMPR EQ(NULL);
Extern HMODULE hNtshrui EQ(NULL);
Extern HMODULE hAcledit EQ(NULL);
Extern HMODULE hNtdll EQ(NULL);

//--------------------------------------------------------------------------
//
//  Global Externs
//
//--------------------------------------------------------------------------

Extern HANDLE hEventNetLoad EQ(NULL);
Extern HANDLE hEventAcledit EQ(NULL);
Extern BOOL bNetLoad EQ(FALSE);
Extern BOOL bNetTypeLoad EQ(FALSE);
Extern BOOL bNetShareLoad EQ(FALSE);
Extern BOOL bNetDone EQ(FALSE);
Extern BOOL bNetAcleditDone EQ(FALSE);

//----------------------------
//
//  aDriveInfo support
//
//----------------------------

#define rgiDrive rgiDriveReal[iUpdateReal]

Extern int iUpdateReal EQ(0);
Extern DRIVE rgiDriveReal[2][26];
Extern DRIVEINFO aDriveInfo[26];

Extern UINT uMenuID;
Extern HMENU hMenu;
Extern UINT uMenuFlags;
Extern BOOL bMDIFrameSysMenu;

Extern PPDOCBUCKET ppDocBucket;
Extern PPDOCBUCKET ppProgBucket;

Extern CRITICAL_SECTION CriticalSectionPath;

Extern LCID lcid;

Extern BOOL bMinOnRun EQ(FALSE);
Extern BOOL bStatusBar EQ(TRUE);

Extern BOOL bDriveBar EQ(TRUE);
Extern BOOL bNewWinOnConnect EQ(TRUE);
Extern BOOL bDisableVisualStyles EQ(FALSE);
Extern BOOL bMirrorContent EQ(FALSE);

Extern BOOL bExitWindows EQ(FALSE);
Extern BOOL bConfirmDelete EQ(TRUE);
Extern BOOL bConfirmSubDel EQ(TRUE);
Extern BOOL bConfirmReplace EQ(TRUE);
Extern BOOL bConfirmMouse EQ(TRUE);
Extern BOOL bConfirmFormat EQ(TRUE);
Extern BOOL bConfirmReadOnly EQ(TRUE);

Extern BOOL bSaveSettings EQ(TRUE);
Extern BOOL bScrollOnExpand EQ(TRUE);

Extern BOOL bConnectable EQ(FALSE);
Extern int iShowSourceBitmaps EQ(1);
Extern BOOL bFSCTimerSet EQ(FALSE);

Extern WCHAR chFirstDrive;  // 'A' or 'a'

Extern UINT uChangeNotifyTime EQ(3000);

Extern WCHAR szDirsRead[32];
Extern WCHAR szCurrentFileSpec[14] EQ(L"*.*");

Extern WCHAR szComma[4] EQ(L",");
Extern WCHAR szDecimal[4] EQ(L".");

Extern WCHAR szTitle[128];

Extern WCHAR szMessage[MAXMESSAGELEN];

Extern WCHAR szStatusTree[80];
Extern WCHAR szStatusDir[80];

Extern WCHAR szOriginalDirPath[MAXPATHLEN];  // was OEM string!!!!!!

Extern WCHAR szTheINIFile[MAXPATHLEN];  // ini file location in %APPDATA%

Extern WCHAR szBytes[20];
Extern WCHAR szSBytes[10];

Extern int cDrives;
Extern int dxDrive;
Extern int dyDrive;
Extern int dxDriveBitmap;
Extern int dyDriveBitmap;
Extern int dxEllipses;
Extern int dxFolder;
Extern int dyFolder;
Extern int dyBorder;         // System Border Width/Height
Extern int dyBorderx2;       // System Border Width/Height * 2
Extern int dxText;           // System Font Width 'M'
Extern int dyText;           // System Font Height
Extern int cchDriveListMax;  // ave # chars in drive list
Extern int dyIcon EQ(32);
Extern int dxIcon EQ(32);

Extern int dyFileName;
Extern int nFloppies;  // Number of Removable Drives

Extern int iSelHighlight EQ(-1);

Extern int cDisableFSC EQ(0);  // has fsc been disabled?
Extern int iReadLevel EQ(0);   // global.  if !0 someone is reading a tree
Extern int dxFrame;
Extern int dxClickRect;
Extern int dyClickRect;
Extern int iNumWindows EQ(0);

Extern int dxButtonSep EQ(8);
Extern int dxButton EQ(24);
Extern int dyButton EQ(22);
Extern int dxDriveList EQ(205);
Extern int dyDriveItem EQ(17);
Extern int xFirstButton;
Extern HFONT hfontDriveList;
Extern HFONT hFont;
Extern HFONT hFontStatus;

Extern HACCEL hAccel EQ(NULL);
Extern HINSTANCE hAppInstance;

Extern HBITMAP hbmBitmaps EQ(NULL);
Extern HDC hdcMem EQ(NULL);

Extern int iCurDrag EQ(0);

Extern HICON hicoTree EQ(NULL);
Extern HICON hicoTreeDir EQ(NULL);
Extern HICON hicoDir EQ(NULL);

Extern HWND hdlgProgress;
Extern HWND hwndFrame EQ(NULL);
Extern HWND hwndMDIClient EQ(NULL);
Extern HWND hwndSearch EQ(NULL);
Extern HWND hwndDragging EQ(NULL);

Extern HWND hwndDriveBar EQ(NULL);
Extern HWND hwndDriveList EQ(NULL);
Extern HWND hwndDropChild EQ(NULL);  // for tree windows forwarding to drivebar
Extern HWND hwndFormatSelect EQ(NULL);

Extern BOOL bCancelTree;

Extern WORD wTextAttribs EQ(0);
Extern DWORD dwSuperDlgMode;

Extern UINT wHelpMessage;
Extern UINT wBrowseMessage;

//
// Warning: When this is set, creating a directory window
// will cause this file spec to be selected.  This must be
// alloc'd and freed by the callee.  It then must be set
// to null before the dir window is called again.
//
Extern LPWSTR pszInitialDirSel;
Extern DWORD dwNewView EQ(VIEW_NAMEONLY);
Extern DWORD dwNewSort EQ(IDD_NAME);

Extern LARGE_INTEGER qFreeSpace;
Extern LARGE_INTEGER qTotalSpace;

Extern HWND hwndStatus EQ(NULL);

Extern int iNumExtensions EQ(0);
Extern EXTENSION extensions[MAX_EXTENSIONS];

Extern HHOOK hhkMsgFilter EQ(NULL);

// this value is an index into dwMenuIDs and used to workaround a bug
#define MHPOP_CURRENT 2

Extern CHAR PHCM_EXPOSE_PLACEHOLDERS EQ(2);
typedef NTSYSAPI CHAR (*RtlSetProcessPlaceholderCompatibilityMode_t)(CHAR aMode);
Extern RtlSetProcessPlaceholderCompatibilityMode_t pfnRtlSetProcessPlaceholderCompatibilityMode;

#ifdef _GLOBALS
UINT dwMenuIDs[] = {
    // three distinct cases: 1: popups (search), 2: popups (position), 3: non-popups

    MH_MYITEMS,  // case 3: used for all non-popups; IDM from WM_MENUSELECT (loword of wParam) is added to this
    MH_POPUP,    // case 2: used for all popups; position value of top level menu is added to this.
    // NOTE: the check in MenuHelp to determine if the MDI child is maximized doesn't work and the code display the
    // WRONG help in that case

    // case 1: these are searched in pairs only for popups;
    // the second value of which is the position of the menu in question (not the menu handle)
    MH_POPUP, 0,  // always setup explicitly for popups due to the bug related to maximization
    0, 0          // We need to NULL terminate this list
};
#else
Extern UINT dwMenuIDs[];
#endif

#undef Extern
#undef EQ
