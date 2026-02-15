/********************************************************************

   wfdlgs.h

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#pragma once

#define RUNDLG 10

#define SEARCHDLG 13
#define MOVECOPYDLG 14
#define DELETEDLG 15
#define MAKEDIRDLG 17
#define EXITDLG 18
#define DISKCOPYDLG 20
#define DISKCOPY2DLG 21
#define FORMAT2DLG 25
#define OTHERDLG 30
#define SORTBYDLG 31
#define OPTIONSDLG 55
#define DMSTATUSDLG 35
#define PRTDLG 36
#define CHOOSENETDRIVEDLG 37
#define MULTIPLEATTRIBSDLG 38
#define CONFIRMREPLACE 39
#define CONFIRMDELETE 40
#define CONFIRMRMDIR 41
#define CONFIRMMOVE 42
#define CONFIRMRENAME 43
#define SELECTDLG 44
#define LFNTOFATDLG 46
#define CHOOSESYSDRIVEDLG 47
#define SEARCHPROGDLG 53

// #define COPYTOCLIPBOARDDLG   54

#define ABOUTDLG 61

#define IDD_TEXT -1
#define IDD_TEXT1 100
#define IDD_DIR 101
#define IDD_FROM 102
#define IDD_TO 103
#define IDD_STATUS 104
#define IDD_DATE1 105
#define IDD_DATE2 106
#define IDD_YESALL 107
#define IDD_TOSTATUS 108
#define IDD_TONAME 109
#define IDD_TEXT2 111
#define IDD_IGNOREALL 112
#define IDD_VERTEXT 113

// next five match IDM_BYNAME list
#define IDD_NAME 201
#define IDD_TYPE 202
#define IDD_SIZE 203
#define IDD_DATE 204
#define IDD_FDATE 205

#define IDD_TIME 299
#define IDD_FLAGS 206

#define IDD_DOSNAMES 266

#define IDD_UPPERCASE 207
#define IDD_SETDEFAULT 208
#define IDD_ASSOC 209
#define IDD_PROGRAMS 210
#define IDD_DOCS 211
#define IDD_OTHER 212
#define IDD_FOUND 213
#define IDD_LOAD 214
#define IDD_READONLY 215
#define IDD_HIDDEN 216
#define IDD_ARCHIVE 217
#define IDD_SYSTEM 218
#define IDD_DELETE 219
#define IDD_SUBDEL 220
#define IDD_REPLACE 221
#define IDD_DRIVE 222
#define IDD_PATH 223
#define IDD_PASSWORD 224
#define IDD_ADDPREV 225
#define IDD_PREV 226
#define IDD_NETBROWSE 227
#define IDD_SERVERS 228
#define IDD_SHARES 229
#define IDD_SAVESETTINGS 231
#define IDD_SEARCHALL 232
#define IDD_INCLUDEDIRS 233
#define IDD_HIGHCAP 241
#define IDD_MAKESYS 242
#define IDD_PROGRESS 243
#define IDD_VERIFY 244
#define IDD_DRIVE1 245
#define IDD_DRIVE2 246
#define IDD_DRIVE3 247
#define IDD_DRIVE4 248
#define IDD_MOUSE 249
#define IDD_SHOWHIDDEN 250
#define IDD_CONFIG 251
#define IDD_CLOSE 252
#define IDD_PERM 253
#define IDD_HELP 254
#define IDD_DISCONNECT 255
#define IDD_COPYTOCLIP 256
#define IDD_COPYTOFILE 257

#define IDD_VERLABEL 258
#define IDD_VERSION 259
#define IDD_SIZELABEL 260
#define IDD_NAMELABEL 261
#define IDD_VERSION_FRAME 262
#define IDD_VERSION_KEY 263
#define IDD_VERSION_VALUE 264
#define IDD_COPYRIGHT 265

// Just to remind people...
// #define IDD_DOSNAMES     266

#define IDD_COMPRESSED 267
#define IDD_CSIZELABEL 268
#define IDD_CSIZE 269
#define IDD_CRATIOLABEL 270
#define IDD_CRATIO 271

#define IDD_RUNAS 272

#define IDD_SHOWJUNCTION 273

#define IDD_EDITOR 274
#define IDC_EDITOR 275
#define IDC_VSTYLE 276
#define IDC_LANGCB 277

#define IDD_ENCRYPTED 279

// Options dialog controls
#define IDC_FONT_LABEL 280
#define IDC_FONT_CHANGE 281
#define IDC_MINONRUN 282

#define IDD_NEW 300
#define IDD_DESC 301
#define IDD_DESCTEXT 302
#define IDD_ADD 303
#define IDD_COMMAND 304
#define IDD_ADVANCED 305
#define IDD_ACTION 306
#define IDD_DDE 308
#define IDD_DDEMESG 309
#define IDD_DDEAPP 310
#define IDD_DDENOTRUN 311
#define IDD_DDETOPIC 312

#define IDD_COMMANDTEXT 313

#define IDD_CLASSLIST 314

#define IDD_EXT 315
#define IDD_EXTTEXT 316
#define IDD_EXTLIST 317

#define IDD_BROWSE 318

#define IDD_DDEMESGTEXT 320
#define IDD_DDEAPPTEXT 321
#define IDD_DDENOTRUNTEXT 322
#define IDD_DDETOPICTEXT 323
#define IDD_DDEOPTIONALTEXT 324

#define IDD_SELECTDRIVE 357

#define IDD_ASSOCFIRST 100
#define IDD_ASSOCLAST 109

#define IDD_HIDE 110

#define IDD_NETWORKFIRST 500

// dialog item IDs
#define IDD_MYTEXT 4000
#define IDD_GASGAUGE 4001

#define IDD_KK_TEXTTO 2001
#define IDD_KK_TEXTFROM 2002

#define IDD_DIRS 2003

void UpdateConnections(BOOL bUpdateDriveList);

void ActivateCommonContextMenu(HWND hwnd, HWND hwndLB, LPARAM lParam);
void KillQuoteTrailSpace(LPWSTR szFile);
void SaveWindows(HWND hwndMain);
void NewFont();

INT_PTR CALLBACK SearchDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK RunDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SelectDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SuperDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AttribsDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK MakeDirDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK OtherDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ProgressDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK OptionsDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam);
