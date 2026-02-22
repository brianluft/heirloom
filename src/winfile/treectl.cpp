/********************************************************************

   treectl.c

   Windows Directory Tree Window Proc Routines

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#define PUBLIC  // avoid collision with shell.h
#include "winfile.h"
#include "treectl.h"
#include "lfn.h"
#include "wfcopy.h"
#include "wfdrop.h"
#include "wfdragsrc.h"
#include "wfpng.h"
#include "wfutil.h"
#include "wfdir.h"
#include "wftree.h"
#include "wfcomman.h"
#include "stringconstants.h"
#include <commctrl.h>
#include <winnls.h>

#define WS_TREESTYLE                                                                                            \
    (WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT | \
     LBS_WANTKEYBOARDINPUT | LBS_DISABLENOSCROLL)

#define READDIRLEVEL_UPDATE 7
#define READDIRLEVEL_YIELDBIT 2

#define IS_PARTIALSORT(drive) (aDriveInfo[drive].dwFileSystemFlags & FS_CASE_IS_PRESERVED)

#define CALC_EXTENT(pNode) (pNode->dwExtent + (2 * pNode->nLevels) * dxText + dxFolder + 3 * dyBorderx2)

DWORD cNodes;

void GetTreePathIndirect(PDNODE pNode, LPWSTR szDest);

void ScanDirLevel(PDNODE pParentNode, LPWSTR szPath, DWORD view);

int InsertDirectory(
    HWND hwndTreeCtl,
    PDNODE pParentNode,
    int iParentNode,
    LPWSTR szName,
    PDNODE* ppNode,
    BOOL bCasePreserved,
    BOOL bPartialSort,
    DWORD dwAttribs);

BOOL ReadDirLevel(
    HWND hwndTreeCtl,
    PDNODE pParentNode,
    LPWSTR szPath,
    UINT uLevel,
    int iParentNode,
    DWORD dwAttribs,
    BOOL bFullyExpand,
    LPWSTR szAutoExpand,
    BOOL bPartialSort);

void FillTreeListbox(HWND hwndTreeCtl, LPWSTR szDefaultDir, BOOL bFullyExpand, BOOL bDontSteal);

BOOL FindItemFromPath(HWND hwndLB, LPWSTR lpszPath, BOOL bReturnParent, DWORD* pIndex, PDNODE* ppNode);

int BuildTreeName(LPWSTR lpszPath, int iLen, int iSize);

UINT GetRealExtent(PDNODE pNode, HWND hwndLB, LPWSTR szPath, int* pLen);

void ResetTreeMax(HWND hwndLB, BOOL fReCalcExtent);

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  GetDragStatusText() -                                                   */
/*                                                                          */
/*  return IDS_<id> with respect to iOperation                              */
/*                                                                          */
/*--------------------------------------------------------------------------*/

int GetDragStatusText(int iOperation) {
    int iStatusText = IDS_DRAG_MOVING;
    switch (iOperation) {
        case DROP_COPY:
            iStatusText = IDS_DRAG_COPYING;
            break;

        case DROP_HARD:
        case DROP_LINK:
            iStatusText = IDS_DRAG_LINKING;
            break;
    }
    return iStatusText;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  GetTreePathIndirect() -                                                 */
/*                                                                          */
/*  build a complete path for a given node in the tree by recursively       */
/*  traversing the tree structure                                           */
/*                                                                          */
/*--------------------------------------------------------------------------*/

void GetTreePathIndirect(PDNODE pNode, LPWSTR szDest) {
    PDNODE pParent;

    pParent = pNode->pParent;

    if (pParent)
        GetTreePathIndirect(pParent, szDest);

    lstrcat(szDest, pNode->szName);

    if (pParent)
        lstrcat(szDest, SZ_BACKSLASH);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  GetTreePath() -                                                         */
/*                                                                          */
/*  build a complete path for a given node in the tree                      */
/*                                                                          */
/*--------------------------------------------------------------------------*/

void GetTreePath(PDNODE pNode, LPWSTR szDest) {
    szDest[0] = CHAR_NULL;
    GetTreePathIndirect(pNode, szDest);

    //
    // Remove the last backslash (unless it is the root directory).
    //
    if (pNode->pParent)
        szDest[lstrlen(szDest) - 1] = CHAR_NULL;
}

//  SetNodeAttribs
//
//  Set node attributes for directory/junction/symlink
//
void SetNodeAttribs(PDNODE pNode, LPWSTR szPath) {
    pNode->dwAttribs = GetFileAttributes(szPath);
    if (INVALID_FILE_ATTRIBUTES == pNode->dwAttribs) {
        pNode->dwAttribs = 0;
    }

    //
    // Determine which kind of Reparse Point
    //
    if (pNode->dwAttribs & ATTR_REPARSE_POINT) {
        DWORD tag = DecodeReparsePoint(szPath, NULL, 0);
        switch (tag) {
            case IO_REPARSE_TAG_MOUNT_POINT:
                pNode->dwAttribs |= ATTR_JUNCTION;
                break;
            case IO_REPARSE_TAG_SYMLINK:
                pNode->dwAttribs |= ATTR_SYMBOLIC;
                break;
        }
    }
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ScanDirLevel() -                                                        */
/*                                                                          */
/*  look down to see if this node has any sub directories                   */
/*                                                                          */
/*--------------------------------------------------------------------------*/

void ScanDirLevel(PDNODE pParentNode, LPWSTR szPath, DWORD view) {
    BOOL bFound;
    BOOL bExclude;

    LFNDTA lfndta{};
    lfndta.hFindFile = INVALID_HANDLE_VALUE;

    /* Add '*.*' to the current path. */
    lstrcpy(szMessage, szPath);
    AddBackslash(szMessage);
    lstrcat(szMessage, kStarDotStar);

    /* Search for the first subdirectory on this level. */

    bFound = WFFindFirst(&lfndta, szMessage, ATTR_DIR | view);

    while (bFound) {
        /* Is this a junction and are those displayed? */
        bExclude = FALSE;
        if ((view & ATTR_JUNCTION) == 0 && (lfndta.fd.dwFileAttributes & ATTR_JUNCTION)) {
            bExclude = TRUE;
        }

        /* Is this not a '.' or '..' directory? */
        if (!ISDOTDIR(lfndta.fd.cFileName) && (lfndta.fd.dwFileAttributes & ATTR_DIR) && !bExclude) {
            pParentNode->wFlags |= TF_HASCHILDREN;
            bFound = FALSE;
        } else
            /* Search for the next subdirectory. */
            bFound = WFFindNext(&lfndta);
    }

    WFFindClose(&lfndta);
}

// wizzy cool recursive path compare routine
//
// p1 and p2 must be on the same level (p1->nLevels == p2->nLevels)

int ComparePath(PDNODE p1, PDNODE p2) {
    int ret;

    if ((p1 == p2) || (!p1) || (!p2)) {
        return 0;  // equal (base case)

    } else {
        ret = ComparePath(p1->pParent, p2->pParent);

        if (ret == 0) {
            // parents are equal

            ret = lstrcmpi(p1->szName, p2->szName);
        }

        // not equal parents, propagate up the call tree
        return ret;
    }
}

int CompareNodes(PDNODE p1, PDNODE p2) {
    PDNODE p1save, p2save;
    int ret;

    p1save = p1;
    p2save = p2;

    // get p1 and p2 to the same level

    while (p1->nLevels > p2->nLevels)
        p1 = p1->pParent;

    while (p2->nLevels > p1->nLevels)
        p2 = p2->pParent;

    // compare those paths

    ret = ComparePath(p1, p2);

    if (ret == 0)
        ret = (int)p1save->nLevels - (int)p2save->nLevels;

    return ret;
}

//
// InsertDirectory()
//
// wizzy quick n log n binary insert code!
//
// creates and inserts a new node in the tree, this also sets
// the TF_LASTLEVELENTRY bits to mark a branch as being the last
// for a given level as well as marking parents with
// TF_HASCHILDREN | TF_EXPANDED to indicate they have been expanded
// and have children.
//
// Returns iNode and fills ppNode with pNode.
//

int InsertDirectory(
    HWND hwndTreeCtl,
    PDNODE pParentNode,
    int iParentNode,
    LPWSTR szName,
    PDNODE* ppNode,
    BOOL bCasePreserved,
    BOOL bPartialSort,
    DWORD dwAttribs) {
    int len;
    UINT x, xTreeMax;
    PDNODE pNode, pMid;
    HWND hwndLB;
    int iMin, iMax, iMid;
    WCHAR szPathName[MAXPATHLEN * 2];

    len = lstrlen(szName);

    pNode = (PDNODE)LocalAlloc(LPTR, sizeof(DNODE) + ByteCountOf(len));
    if (!pNode) {
        if (ppNode) {
            *ppNode = NULL;
        }
        return (0);
    }

    pNode->pParent = pParentNode;
    pNode->nLevels = pParentNode ? (pParentNode->nLevels + (BYTE)1) : (BYTE)0;
    pNode->wFlags = 0;
    pNode->dwNetType = (DWORD)-1;

    if (!bCasePreserved)
        pNode->wFlags |= TF_LOWERCASE;

    lstrcpy(pNode->szName, szName);

    if (pParentNode)
        pParentNode->wFlags |= TF_HASCHILDREN | TF_EXPANDED;  // mark the parent

    hwndLB = GetDlgItem(hwndTreeCtl, IDCW_TREELISTBOX);

    /*
     *  Get the real text extent for the current directory and save it
     *  in the pNode.
     */
    x = GetRealExtent(pNode, hwndLB, NULL, &len);
    x = CALC_EXTENT(pNode);

    xTreeMax = (UINT)GetWindowLongPtr(hwndTreeCtl, GWL_XTREEMAX);
    if (x > xTreeMax) {
        SetWindowLongPtr(hwndTreeCtl, GWL_XTREEMAX, x);
        SendMessage(hwndLB, LB_SETHORIZONTALEXTENT, x, 0L);
    }

    iMax = (int)SendMessage(hwndLB, LB_GETCOUNT, 0, 0L);

    if (iMax > 0) {
        // do a binary insert

        iMin = iParentNode + 1;
        iMax--;  // last index

        //
        // Hack speedup: check if goes last.
        //
        SendMessage(hwndLB, LB_GETTEXT, iMax, (LPARAM)&pMid);

        if (bPartialSort && CompareNodes(pNode, pMid) > 0) {
            iMax++;
        } else {
            int iCmp;
            do {
                iMid = (iMax + iMin) / 2;

                SendMessage(hwndLB, LB_GETTEXT, iMid, (LPARAM)&pMid);

                iCmp = CompareNodes(pNode, pMid);
                if (iCmp == 0) {
                    iMax = iMin = iMid;
                } else if (iCmp > 0)
                    iMin = iMid + 1;
                else
                    iMax = iMid - 1;

            } while (iMax > iMin);

            // result is that new node may be:
            // a. inserted before iMax (normal case)
            // b. inserted after iMax (if at end of list)
            // c. same as iMax -- return right away
            SendMessage(hwndLB, LB_GETTEXT, iMax, (LPARAM)&pMid);
            iCmp = CompareNodes(pNode, pMid);
            if (iCmp == 0) {
                if (ppNode) {
                    *ppNode = pMid;
                }
                return iMax;
            }

            if (iCmp > 0) {
                iMax++;  // insert after this one
            }
        }
    }

    // now reset the TF_LASTLEVEL flags as appropriate

    // look for the first guy on our level above us and turn off
    // his TF_LASTLEVELENTRY flag so he draws a line down to us

    iMid = iMax - 1;

    while (iMid >= 0) {
        SendMessage(hwndLB, LB_GETTEXT, iMid--, (LPARAM)&pMid);
        if (pMid->nLevels == pNode->nLevels) {
            pMid->wFlags &= ~TF_LASTLEVELENTRY;
            break;
        } else if (pMid->nLevels < pNode->nLevels) {
            break;
        }
    }

    // if no one below me or the level of the guy below is less, then
    // this is the last entry for this level

    if (((int)SendMessage(hwndLB, LB_GETTEXT, iMax, (LPARAM)&pMid) == LB_ERR) || (pMid->nLevels < pNode->nLevels)) {
        pNode->wFlags |= TF_LASTLEVELENTRY;
    }

    //
    //  Set the attributes for this directory/junction/symlink.
    //
    if (dwAttribs == INVALID_FILE_ATTRIBUTES) {
        GetTreePath(pNode, szPathName);
        SetNodeAttribs(pNode, szPathName);
    } else {
        pNode->dwAttribs = dwAttribs;
    }

    SendMessage(hwndLB, LB_INSERTSTRING, iMax, (LPARAM)pNode);

    if (ppNode) {
        *ppNode = pNode;
    }
    return iMax;
}

/////////////////////////////////////////////////////////////////////
//
// Name:     wfYield
//
/////////////////////////////////////////////////////////////////////

void wfYield() {
    MSG msg;

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (!TranslateMDISysAccel(hwndMDIClient, &msg) &&
            (!hwndFrame || !TranslateAccelerator(hwndFrame, hAccel, &msg))) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

/////////////////////////////////////////////////////////////////////
//
// Name:     WFFindNextNonJunction
//
// Synopsis: Returns the next non-junction entry, which may be the
//           current entry.  Continually calls WFFindNext so long as
//           the current entry is a junction.
//
// lpFind               Pointer to the find context, which may (or may
//                      not) be advanced to a later entry.
//
// Return:              TRUE  = non-junction successfully found
//                      FALSE = no non-junction remaining.
BOOL WFFindNextNonJunction(LPLFNDTA lpFind) {
    BOOL bFound;

    bFound = TRUE;

    while (bFound) {
        // If it's not a junction, return it.
        if (!(lpFind->fd.dwFileAttributes & ATTR_JUNCTION)) {
            return bFound;
        }

        bFound = WFFindNext(lpFind);
    }

    return bFound;
}

/////////////////////////////////////////////////////////////////////
//
// Name:     ReadDirLevel
//
// Synopsis: Does a DFS on dir tree, BFS didn't (?) do any better
//
// szPath               a directory path that MUST EXIST long enough
//                      to hold the full path to the largest directory
//                      that will be found (MAXPATHLEN).  this is an
//                      ANSI string.  (ie C:\ and C:\FOO are valid)
// nLevel               level in the tree
// iParentNode          index of parent node
// dwAttribs            attributes to filter with
// bFullyExpand         TRUE means expand this node fully
// szAutoExpand         CharUpper path to autoexpand
//                      (eg. "C:\FOO\BAR\STUFF")
// bPartialSort         TRUE means partially sorted on disk
//
// Return:              TRUE  = successful tree read
//                      FALSE = user abort or bogus tree read.
//
// Assumes:
//
// Effects:
//
//
// Notes:
//
/////////////////////////////////////////////////////////////////////

BOOL ReadDirLevel(
    HWND hwndTreeCtl,
    PDNODE pParentNode,
    LPWSTR szPath,
    UINT uLevel,
    int iParentNode,
    DWORD dwAttribs,
    BOOL bFullyExpand,
    LPWSTR szAutoExpand,
    BOOL bPartialSort) {
    HWND hwndParent;
    LPWSTR szEndPath;
    int iNode;
    BOOL bFound;
    BOOL bAutoExpand;
    PDNODE pNode;
    LPXDTA* plpxdta = NULL;
    LPXDTA lpxdta = NULL;
    WCHAR szMessage[MAXPATHLEN];
    UINT uYieldCount = 0;
    HWND hwndLB;
    int count = 0;
    BOOL bResult = TRUE;
    LPXDTALINK lpStart = NULL;  // assume none to steal from
    HWND hwndDir;

    LFNDTA lfndta{};
    lfndta.hFindFile = INVALID_HANDLE_VALUE;

    if (bCancelTree)
        return FALSE;

    hwndParent = GetParent(hwndTreeCtl);
    hwndLB = GetDlgItem(hwndTreeCtl, IDCW_TREELISTBOX);

    // We always optimize the tree read by looking through the DTA structure
    // in the dir window (instead of calling FindFirst/FindNext) if one is available.
    // In this case we have to disable yielding since the user could
    // potentially close the dir window that we are reading, or change directory.
    if ((hwndDir = HasDirWindow(hwndParent))) {
        SendMessage(hwndDir, FS_GETDIRECTORY, COUNTOF(szMessage), (LPARAM)szMessage);
        StripBackslash(szMessage);

        if (!lstrcmpi(szMessage, szPath)) {
            SendMessage(hwndDir, FS_GETFILESPEC, COUNTOF(szMessage), (LPARAM)szMessage);

            if (!lstrcmp(szMessage, kStarDotStar)) {
                lpStart = (LPXDTALINK)GetWindowLongPtr(hwndDir, GWL_HDTA);

                if (lpStart) {
                    //
                    // holds number of entries, NOT size.
                    //
                    count = (int)MemLinkToHead(lpStart)->dwEntries;

                    //
                    // We are currently using it, so mark it as in use.
                    // There's a sync problem around lpStart:
                    // Since we do a wfYield, this could be free'd in
                    // the same thread if the user changes directories.
                    // So we must mark it appropriately.
                    //
                    // Don't worry about critical sections, since
                    // we are cooperatively multitasking using the
                    // wfYield.
                    //
                    MemLinkToHead(lpStart)->fdwStatus |= LPXDTA_STATUS_READING;
                }
            }
        }
    }

    //
    //  Disable drive list combo box while we're reading.
    //
    EnableWindow(hwndDriveList, FALSE);

    SetWindowLongPtr(hwndTreeCtl, GWL_READLEVEL, GetWindowLongPtr(hwndTreeCtl, GWL_READLEVEL) + 1);

    iReadLevel++;  // global for menu code

    szEndPath = szPath + lstrlen(szPath);

    //
    // Add '\*.*' to the current path.
    //
    AddBackslash(szPath);
    lstrcat(szPath, kStarDotStar);

    if ((lpStart) && (plpxdta = MemLinkToHead(lpStart)->alpxdtaSorted)) {
        //
        // steal the entry from the dir window
        //

        // TODO: why not set count here?

        // find first directory which isn't the special parent node
        for (; count > 0; count--, plpxdta++) {
            lpxdta = *plpxdta;

            if ((lpxdta->dwAttrs & ATTR_DIR) && !(lpxdta->dwAttrs & ATTR_PARENT)) {
                break;
            }
        }

        if (count > 0) {
            bFound = TRUE;

            //
            // Only need to copy filename and my_dwAttrs
            //
            lfndta.fd.dwFileAttributes = lpxdta->dwAttrs;
            lstrcpy(lfndta.fd.cFileName, MemGetFileName(lpxdta));
        } else {
            bFound = FALSE;
        }
    } else {
        //
        // get first file from DOS
        //
        lstrcpy(szMessage, szPath);

        bFound = WFFindFirst(&lfndta, szMessage, dwAttribs);
    }

    // for net drive case where we can't actually see what is in these
    // directories we will build the tree automatically

    // Note that szAutoExpand is nulled out if we are not building
    // along its path at this time.  (If szAutoExpand[0]=="FOO" "BAR"
    // and we are at "F:\IT\IS" then the original szAutoExpand
    // was for path "F:\IT\IS\FOO\BAR": the second we stop off the
    // path, we null it.)

    // Must now check if szAutoExpand == NULL
    if (!bFound && szAutoExpand && *szAutoExpand) {
        LPWSTR p;

        p = szAutoExpand;
        szAutoExpand += lstrlen(szAutoExpand) + 1;

        iNode = InsertDirectory(
            hwndTreeCtl, pParentNode, iParentNode, p, &pNode, IsCasePreservedDrive(DRIVEID(szPath)), bPartialSort,
            lfndta.fd.dwFileAttributes);

        pParentNode->wFlags |= TF_DISABLED;

        /* Construct the path to this new subdirectory. */
        *szEndPath = CHAR_NULL;  // remove old stuff
        AddBackslash(szPath);
        lstrcat(szPath, p);

        // Recurse!

        ReadDirLevel(
            hwndTreeCtl, pNode, szPath, uLevel + 1, iNode, dwAttribs, bFullyExpand, szAutoExpand, bPartialSort);
    }

    while (bFound) {
        if (uYieldCount & (1 << READDIRLEVEL_YIELDBIT)) {
            wfYield();
        }

        uYieldCount++;

        if (bCancelTree) {
            int iDrive = (int)GetWindowLongPtr(hwndParent, GWL_TYPE);

            if (!IsValidDisk(iDrive))
                PostMessage(hwndParent, WM_SYSCOMMAND, SC_CLOSE, 0L);

            bResult = FALSE;
            if (bCancelTree == 2)
                PostMessage(hwndFrame, WM_COMMAND, IDM_EXIT, 0L);
            goto DONE;
        }

        /* Is this not a '.' or '..' directory? */
        if (!ISDOTDIR(lfndta.fd.cFileName) && (lfndta.fd.dwFileAttributes & ATTR_DIR)) {
            // we will try to auto expand this node if it matches

            // Must check if NULL

            if (szAutoExpand && *szAutoExpand && !lstrcmpi(szAutoExpand, lfndta.fd.cFileName)) {
                bAutoExpand = TRUE;
                szAutoExpand += lstrlen(szAutoExpand) + 1;
            } else {
                bAutoExpand = FALSE;
            }

            iNode = InsertDirectory(
                hwndTreeCtl, pParentNode, iParentNode, lfndta.fd.cFileName, &pNode,
                IsCasePreservedDrive(DRIVEID(szPath)), bPartialSort, lfndta.fd.dwFileAttributes);

            if (hwndStatus && ((cNodes % READDIRLEVEL_UPDATE) == 0)) {
                // make sure we are the active window before we
                // update the status bar

                if (hwndParent == (HWND)SendMessage(hwndMDIClient, WM_MDIGETACTIVE, 0, 0L)) {
                    SetStatusText(0, SST_FORMAT, szDirsRead, cNodes);
                    UpdateWindow(hwndStatus);
                }
            }

            cNodes++;

            //
            // Construct the path to this new subdirectory.
            //
            *szEndPath = CHAR_NULL;
            AddBackslash(szPath);
            lstrcat(szPath, lfndta.fd.cFileName);  // cFileName is ANSI now

            // Now we just recurse if needed
            if (bFullyExpand || bAutoExpand) {
                // If we are recursing due to bAutoExpand
                // then pass it.  Else pass NULL instead.

                if (!ReadDirLevel(
                        hwndTreeCtl, pNode, szPath, uLevel + 1, iNode, dwAttribs, bFullyExpand,
                        bAutoExpand ? szAutoExpand : NULL, bPartialSort)) {
                    bResult = FALSE;
                    goto DONE;
                }
            }
        }

        if (lpStart && plpxdta) {
            //
            // short cut, steal data from dir window
            //
            // Warning: count could be zero now, so may go negative.
            //
            count--;
            ++plpxdta;

            // find next directory which isn't the special parent node
            while (count > 0) {
                lpxdta = *plpxdta;

                if ((lpxdta->dwAttrs & ATTR_DIR) && !(lpxdta->dwAttrs & ATTR_PARENT)) {
                    break;
                }

                //
                // Go to next item.
                //
                count--;
                ++plpxdta;
            }

            if (count > 0) {
                bFound = TRUE;

                //
                // Only need to copy attrs and file name
                //
                lfndta.fd.dwFileAttributes = lpxdta->dwAttrs;
                lstrcpy(lfndta.fd.cFileName, MemGetFileName(lpxdta));
            } else {
                bFound = FALSE;
            }
        } else {
            bFound = WFFindNext(&lfndta);  // get it from dos

            //
            // if junctions are not displayed, continue to the next non-junction
            //
            if (bFound && !(dwAttribs & ATTR_JUNCTION)) {
                bFound = WFFindNextNonJunction(&lfndta);
            }
        }
    }

    WFFindClose(&lfndta);

DONE:

    //
    // Tell dir that we are now done reading.
    //
    if (lpStart)
        MemLinkToHead(lpStart)->fdwStatus &= ~LPXDTA_STATUS_READING;

    pParentNode->wFlags |= TF_HASCHILDREN;

    SetWindowLongPtr(hwndTreeCtl, GWL_READLEVEL, GetWindowLongPtr(hwndTreeCtl, GWL_READLEVEL) - 1);

    iReadLevel--;  // global for menu code

    // Make the volume name real.
    if (uLevel == 0)
        InvalidateRect(hwndDriveBar, NULL, TRUE);

    //
    //  Re-enable the drive list combo box.
    //
    EnableWindow(hwndDriveList, TRUE);

    return (bResult);
}

// this is used by StealTreeData() to avoid alias problems where
// the nodes in one tree point to parents in the other tree.
// basically, as we are duplicating the tree data structure we
// have to find the parent node that corresponds with the parent
// of the tree we are copying from in the tree that we are building.
// since the tree is build in order we run up the listbox, looking
// for the parent (matched by it's level being one smaller than
// the level of the node being inserted).  when we find that we
// return the pointer to that node.

PDNODE
FindParent(int iLevelParent, int iStartInd, HWND hwndLB) {
    PDNODE pNode;

    while (TRUE) {
        if (SendMessage(hwndLB, LB_GETTEXT, iStartInd, (LPARAM)&pNode) == LB_ERR)
            return NULL;

        if (pNode->nLevels == (BYTE)iLevelParent) {
            // NOTE: seems like a duplicate and unnecessary call to the one above
            SendMessage(hwndLB, LB_GETTEXT, iStartInd, (LPARAM)&pNode);
            return pNode;
        }

        iStartInd--;
    }
}

BOOL StealTreeData(HWND hwndTC, HWND hwndLB, LPWSTR szDir) {
    HWND hwndSrc;
    HWND hwndT;
    HWND hwndLBSrc;
    WCHAR szSrc[MAXPATHLEN];

    hwndT = NULL;

    //
    // get the dir of this new window for compare below
    //
    for (hwndSrc = GetWindow(hwndMDIClient, GW_CHILD); hwndSrc; hwndSrc = GetWindow(hwndSrc, GW_HWNDNEXT)) {
        //
        // avoid finding ourselves, make sure has a tree
        //
        if ((hwndT = HasTreeWindow(hwndSrc)) && (hwndT != hwndTC) && !GetWindowLongPtr(hwndT, GWL_READLEVEL)) {
            SendMessage(hwndSrc, FS_GETDIRECTORY, COUNTOF(szSrc), (LPARAM)szSrc);
            StripBackslash(szSrc);

            if (!lstrcmpi(szDir, szSrc))  // are they the same?
                break;                    // yes, do stuff below
        }
    }

    if (hwndSrc) {
        PDNODE pNode, pNewNode, pLastParent;
        int i;

        hwndLBSrc = GetDlgItem(hwndT, IDCW_TREELISTBOX);

        //
        // don't seal from a tree that hasn't been read yet!
        //
        if ((int)SendMessage(hwndLBSrc, LB_GETCOUNT, 0, 0L) == 0) {
            return FALSE;
        }

        pLastParent = NULL;

        for (i = 0; SendMessage(hwndLBSrc, LB_GETTEXT, i, (LPARAM)&pNode) != LB_ERR; i++) {
            if (pNewNode = (PDNODE)LocalAlloc(LPTR, sizeof(DNODE) + ByteCountOf(lstrlen(pNode->szName)))) {
                *pNewNode = *pNode;                        // dup the node
                lstrcpy(pNewNode->szName, pNode->szName);  // and the name

                //
                // accelerate the case where we are on the same level to avoid
                // slow linear search!
                //
                if (pLastParent && pLastParent->nLevels == (BYTE)(pNode->nLevels - (BYTE)1)) {
                    pNewNode->pParent = pLastParent;
                } else {
                    pNewNode->pParent = pLastParent = FindParent(pNode->nLevels - 1, i - 1, hwndLB);
                }

                SendMessage(hwndLB, LB_INSERTSTRING, i, (LPARAM)pNewNode);
            }
        }

        /*
         *  Reset the max text extent value for the new window.
         */
        ResetTreeMax(hwndLB, FALSE);

        return TRUE;  // successful steal
    }

    return FALSE;
}

void FreeAllTreeData(HWND hwndLB) {
    int nIndex;
    PDNODE pNode;

    // Free up the old tree (if any)

    nIndex = (int)SendMessage(hwndLB, LB_GETCOUNT, 0, 0L) - 1;
    while (nIndex >= 0) {
        SendMessage(hwndLB, LB_GETTEXT, nIndex, (LPARAM)&pNode);
        LocalFree((HANDLE)pNode);
        nIndex--;
    }

    SendMessage(hwndLB, LB_RESETCONTENT, 0, 0L);
    SetWindowLongPtr(GetParent(hwndLB), GWL_XTREEMAX, 0);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  FillTreeListbox() -                                                     */
/*                                                                          */
/*--------------------------------------------------------------------------*/

void FillTreeListbox(HWND hwndTC, LPWSTR szDefaultDir, BOOL bFullyExpand, BOOL bDontSteal) {
    PDNODE pNode;
    int iNode;
    DWORD dwAttribs;
    WCHAR szTemp[MAXPATHLEN + 1] = SZ_ACOLONSLASH;
    WCHAR szExpand[MAXPATHLEN + 1];
    LPWSTR p;
    HWND hwndLB;
    BOOL bPartialSort;
    DRIVE drive;

    pNode = NULL;

    hwndLB = GetDlgItem(hwndTC, IDCW_TREELISTBOX);

    FreeAllTreeData(hwndLB);

    SendMessage(hwndLB, WM_SETREDRAW, FALSE, 0L);

    if (bDontSteal || bFullyExpand || !StealTreeData(hwndTC, hwndLB, szDefaultDir)) {
        drive = DRIVEID(szDefaultDir);
        DRIVESET(szTemp, drive);

        //
        // Hack: if NTFS/HPFS are partially sorted already
        // Optimize for this
        //
        U_VolInfo(drive);

        bPartialSort = IS_PARTIALSORT(drive);

        iNode = InsertDirectory(
            hwndTC, NULL, 0, szTemp, &pNode, IsCasePreservedDrive(DRIVEID(szDefaultDir)), bPartialSort,
            INVALID_FILE_ATTRIBUTES);

        if (pNode) {
            dwAttribs = ATTR_DIR | ATTR_ALL;
            cNodes = 0;
            bCancelTree = FALSE;

            if (szDefaultDir) {
                lstrcpy(szExpand, szDefaultDir + 3);  // skip "X:\"
                p = szExpand;

                while (*p) {  // null out all slashes

                    while (*p && *p != CHAR_BACKSLASH)
                        ++p;

                    if (*p)
                        *p++ = CHAR_NULL;
                }
                p++;
                *p = CHAR_NULL;  // double null terminated
            } else
                *szExpand = CHAR_NULL;

            if (!ReadDirLevel(hwndTC, pNode, szTemp, 1, 0, dwAttribs, bFullyExpand, szExpand, bPartialSort)) {
                SPC_SET_NOTREE(qFreeSpace);
            }
        }
    }

    if (szDefaultDir) {
        FindItemFromPath(hwndLB, szDefaultDir, FALSE, NULL, &pNode);
    }

    if (pNode != NULL) {
        SendMessage(hwndLB, LB_SELECTSTRING, (WPARAM)-1, (LPARAM)pNode);
    }

    UpdateStatus(GetParent(hwndTC));  // Redraw the Status Bar

    SendMessage(hwndLB, WM_SETREDRAW, TRUE, 0L);

    InvalidateRect(hwndLB, NULL, TRUE);
    UpdateWindow(hwndLB);  // make this look a bit better
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  FillOutTreeList() -                                                     */
/*     expand tree in place for the path give starting at the node given    */
/*                                                                          */
/*--------------------------------------------------------------------------*/

void FillOutTreeList(HWND hwndTC, LPWSTR szDir, DWORD nIndex, PDNODE pNode) {
    HWND hwndLB;
    DWORD dwAttribs;
    LPWSTR p;
    WCHAR szExists[MAXPATHLEN + 1];  // path that exists in tree
    WCHAR szExpand[MAXPATHLEN + 1];  // sequence of null terminated strings to expand

    hwndLB = GetDlgItem(hwndTC, IDCW_TREELISTBOX);

    // TODO: assert pNode is at nIndex.  and szDir begins with X:\  szDir is a superset of szExists

    SendMessage(hwndLB, WM_SETREDRAW, FALSE, 0L);

    dwAttribs = ATTR_DIR | ATTR_ALL;

    // get path to node that already exists in tree; will start reading from there
    GetTreePath(pNode, szExists);

    // convert szDir into a sequence of null terminated strings for each directory segment
    // TODO: shared function
    lstrcpy(
        szExpand,
        szDir + lstrlen(szExists) + 1);  // skip temp path (that which is already in tree) and intervening '\\'
    p = szExpand;

    while (*p) {  // null out all slashes

        while (*p && *p != CHAR_BACKSLASH)
            ++p;

        if (*p)
            *p++ = CHAR_NULL;
    }
    p++;
    *p = CHAR_NULL;  // double null terminated

    bCancelTree = 0;

    if (!ReadDirLevel(hwndTC, pNode, szExists, pNode->nLevels + 1, nIndex, dwAttribs, FALSE, szExpand, FALSE)) {
        SPC_SET_NOTREE(qFreeSpace);
    }

    if (FindItemFromPath(hwndLB, szDir, FALSE, NULL, &pNode)) {
        // found desired path in newly expanded list; select
        SendMessage(hwndLB, LB_SELECTSTRING, (WPARAM)-1, (LPARAM)pNode);
    }

    UpdateStatus(GetParent(hwndTC));  // Redraw the Status Bar

    SendMessage(hwndLB, WM_SETREDRAW, TRUE, 0L);

    InvalidateRect(hwndLB, NULL, TRUE);
    UpdateWindow(hwndLB);  // make this look a bit better
}

//
// FindItemFromPath()
//
// find the PDNODE and LBIndex for a given path
//
// in:
//      hwndLB          listbox of tree
//      lpszPath        path to search for (ANSI)
//      bReturnParent   TRUE if you want the parent, not the node
//
//
// returns:
//      TRUE if exact match; FALSE if not.
//      *pIndex is listbox index pNode returned; (DWORD)-1 if no match found
//      *ppNode is filled with pNode of node, or pNode of parent if bReturnParent is TRUE; NULL if not found
//

BOOL FindItemFromPath(HWND hwndLB, LPWSTR lpszPath, BOOL bReturnParent, DWORD* pIndex, PDNODE* ppNode) {
    DWORD i;
    LPWSTR p;
    PDNODE pNode;
    DWORD iPreviousNode;
    PDNODE pPreviousNode;
    WCHAR szElement[1 + MAXFILENAMELEN + 1];

    if (pIndex) {
        *pIndex = (DWORD)-1;
    }
    if (ppNode) {
        *ppNode = NULL;
    }

    if (!lpszPath || lstrlen(lpszPath) < 3 || lpszPath[1] != CHAR_COLON) {
        return FALSE;
    }

    i = 0;
    iPreviousNode = (DWORD)-1;
    pPreviousNode = NULL;

    while (*lpszPath) {
        /* NULL out szElement[1] so the backslash hack isn't repeated with
         * a first level directory of length 1.
         */
        szElement[1] = CHAR_NULL;

        /* Copy the next section of the path into 'szElement' */
        p = szElement;

        while (*lpszPath && *lpszPath != CHAR_BACKSLASH)
            *p++ = *lpszPath++;

        /* Add a backslash for the Root directory. */

        if (szElement[1] == CHAR_COLON)
            *p++ = CHAR_BACKSLASH;

        /* NULL terminate 'szElement' */
        *p = CHAR_NULL;

        /* Skip over the path's next Backslash. */

        if (*lpszPath)
            lpszPath++;

        else if (bReturnParent) {
            /* We're at the end of a path which includes a filename.  Return
             * the previously found parent.
             */
            if (pIndex) {
                *pIndex = iPreviousNode;
            }
            if (ppNode) {
                *ppNode = pPreviousNode;
            }
            return TRUE;
        }

        while (TRUE) {
            /* Out of LB items?  Not found. */
            if (SendMessage(hwndLB, LB_GETTEXT, i, (LPARAM)&pNode) == LB_ERR) {
                if (pIndex) {
                    *pIndex = iPreviousNode;
                }
                if (ppNode) {
                    *ppNode = pPreviousNode;
                }
                return FALSE;
            }

            if (pNode->pParent == pPreviousNode) {
                if (!lstrcmpi(szElement, pNode->szName)) {
                    /* We've found the element... */
                    iPreviousNode = i;
                    pPreviousNode = pNode;
                    break;
                }
            }
            i++;
        }
    }
    if (pIndex) {
        *pIndex = iPreviousNode;
    }
    if (ppNode) {
        *ppNode = pPreviousNode;
    }

    return TRUE;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  RectTreeItem() -                                                        */
/*                                                                          */
/*--------------------------------------------------------------------------*/

BOOL RectTreeItem(HWND hwndLB, int iItem, BOOL bFocusOn) {
    int len;
    HDC hdc;
    RECT rc;
    RECT rcClip;
    BOOL bSel;
    WORD wColor;
    PDNODE pNode;
    HBRUSH hBrush;
    WCHAR szPath[MAXPATHLEN];
    SIZE size;

    if (iItem == -1) {
    EmptyStatusAndReturn:
        SendMessage(hwndStatus, SB_SETTEXT, SBT_NOBORDERS | 255, (LPARAM)kEmptyString);
        UpdateWindow(hwndStatus);
        return FALSE;
    }

    // Are we over ourselves? (i.e. a selected item in the source listbox)

    bSel = (BOOL)SendMessage(hwndLB, LB_GETSEL, iItem, 0L);

    if (bSel && (hwndDragging == hwndLB))
        goto EmptyStatusAndReturn;

    if (SendMessage(hwndLB, LB_GETTEXT, iItem, (LPARAM)&pNode) == LB_ERR)
        goto EmptyStatusAndReturn;

    SendMessage(hwndLB, LB_GETITEMRECT, iItem, (LPARAM)(LPRECT)&rc);

    hdc = GetDC(hwndLB);

    /*
     *  Save the real extent.
     */
    size.cx = GetRealExtent(pNode, NULL, szPath, &len);
    size.cx += dyBorder;

    // rc.left always equals 0 regardless if the horizontal scrollbar
    // is scrolled.  Furthermore, the DC of the listbox uses the visible
    // upper-left corner as (0,0) instead of the conceptual one.

    // To fix this problem, we subtract of the offset between the visible
    // left edge and the conceptual one.  This is done by checking the
    // size of the visible window and subtracting this from the right
    // edge, which is correct.

    // moved up
    GetClientRect(hwndLB, &rcClip);

    rc.left = pNode->nLevels * dxText * 2 - (rc.right - (rcClip.right - rcClip.left));

    rc.right = rc.left + dxFolder + size.cx + 4 * dyBorderx2;

    IntersectRect(&rc, &rc, &rcClip);

    if (bFocusOn) {
        GetTreePath(pNode, szPath);
        StripBackslash(szPath);

        SetStatusText(
            SBT_NOBORDERS | 255, SST_FORMAT | SST_RESOURCE, (LPCWSTR)(DWORD_PTR)GetDragStatusText(iShowSourceBitmaps),
            szPath);
        UpdateWindow(hwndStatus);

        if (bSel) {
            wColor = COLOR_WINDOW;
            InflateRect(&rc, -dyBorder, -dyBorder);
        } else
            wColor = COLOR_WINDOWFRAME;
        if (hBrush = CreateSolidBrush(GetSysColor(wColor))) {
            FrameRect(hdc, &rc, hBrush);
            DeleteObject(hBrush);
        }
    } else {
        InvalidateRect(hwndLB, &rc, TRUE);
        UpdateWindow(hwndLB);
    }
    ReleaseDC(hwndLB, hdc);
    return TRUE;
}

//
// return the drive of the first window to respond to the FS_GETDRIVE
// message.  this usually starts from the source or dest of a drop
// and travels up until we find a drive or hit the MDI client
//
int GetDrive(HWND hwnd, POINT pt) {
    WCHAR chDrive;

    //
    // make sure we are not sending the FS_GETDRIVE message to other apps
    //
    if (GetWindowLongPtr(hwnd, GWLP_HINSTANCE) != (LONG_PTR)hAppInstance)
        return 0;

    chDrive = CHAR_NULL;
    while (hwnd && (hwnd != hwndMDIClient)) {
        chDrive = (WCHAR)SendMessage(hwnd, FS_GETDRIVE, 0, MAKELONG((WORD)pt.x, (WORD)pt.y));

        if (chDrive)
            return chDrive;

        hwnd = GetParent(hwnd);  // try the next higher up
    }

    return 0;
}

BOOL IsNetPath(PDNODE pNode) {
    // 2* buffer for single file overflow
    WCHAR szPath[MAXPATHLEN * 2];
    DWORD dwType;
    DRIVE drive;

    if (!WAITNET_TYPELOADED)
        return FALSE;

    //
    // Only do WNetGetDirectoryType if the drive
    // hasn't failed before on this call.
    //
    if (pNode->dwNetType == (DWORD)-1) {
        GetTreePath(pNode, szPath);
        drive = DRIVEID(szPath);

        if (!aDriveInfo[drive].bShareChkFail &&
            WNetGetDirectoryType(szPath, &dwType, !aDriveInfo[drive].bShareChkTried) == WN_SUCCESS) {
            pNode->dwNetType = dwType;
        } else {
            pNode->dwNetType = 0;
            aDriveInfo[drive].bShareChkFail = TRUE;
        }

        aDriveInfo[drive].bShareChkTried = TRUE;
    }

    return pNode->dwNetType;
}

void TCWP_DrawItem(LPDRAWITEMSTRUCT lpLBItem, HWND hwndLB, HWND hWnd) {
    int x, y, dy;
    int nLevel;
    HDC hdc;
    int len;
    RECT rc;
    BOOL bHasFocus, bDrawSelected;
    PDNODE pNode, pNTemp;
    DWORD rgbText;
    DWORD rgbBackground;
    HBRUSH hBrush, hOld;
    int iBitmap;
    DWORD view;

    // +1 added since IsNetPath->GetTreePath->GetTreePathIndirect
    // is recursive and adds extra '\' at end then strips it off
    WCHAR szPath[MAXPATHLEN + 1];

    SIZE size;

    if (lpLBItem->itemID == (DWORD)-1) {
        return;
    }

    hdc = lpLBItem->hDC;
    pNode = (PDNODE)lpLBItem->itemData;

    /*
     *  Save the real extent.
     */
    size.cx = GetRealExtent(pNode, NULL, szPath, &len);
    size.cx += dyBorder;

    rc = lpLBItem->rcItem;
    rc.left = pNode->nLevels * dxText * 2;
    rc.right = rc.left + dxFolder + size.cx + 4 * dyBorderx2;

    if (lpLBItem->itemAction & (ODA_DRAWENTIRE | ODA_SELECT)) {
        // draw the branches of the tree first

        nLevel = pNode->nLevels;

        x = (nLevel * dxText * 2) - dxText + dyBorderx2;
        dy = lpLBItem->rcItem.bottom - lpLBItem->rcItem.top;
        y = lpLBItem->rcItem.top + (dy / 2);

        if (hBrush = CreateSolidBrush(GetSysColor(COLOR_GRAYTEXT))) {
            hOld = (HBRUSH)SelectObject(hdc, hBrush);

            if (pNode->pParent) {
                /* Draw the horizontal line over to the (possible) folder. */
                PatBlt(hdc, x, y, dyText, dyBorder, PATCOPY);

                /* Draw the top part of the vertical line. */
                PatBlt(hdc, x, lpLBItem->rcItem.top, dyBorder, dy / 2, PATCOPY);

                /* If not the end of a node, draw the bottom part... */
                if (!(pNode->wFlags & TF_LASTLEVELENTRY))
                    PatBlt(hdc, x, y + dyBorder, dyBorder, dy / 2, PATCOPY);

                /* Draw the verticals on the left connecting other nodes. */
                pNTemp = pNode->pParent;
                while (pNTemp) {
                    nLevel--;
                    if (!(pNTemp->wFlags & TF_LASTLEVELENTRY))
                        PatBlt(
                            hdc, (nLevel * dxText * 2) - dxText + dyBorderx2, lpLBItem->rcItem.top, dyBorder, dy,
                            PATCOPY);

                    pNTemp = pNTemp->pParent;
                }
            }

            if (hOld)
                SelectObject(hdc, hOld);

            DeleteObject(hBrush);
        }

        bDrawSelected = (lpLBItem->itemState & ODS_SELECTED);
        bHasFocus = (GetFocus() == lpLBItem->hwndItem);

        // draw text with the proper background or rect

        if (bHasFocus && bDrawSelected) {
            rgbText = SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
            rgbBackground = SetBkColor(hdc, GetSysColor(COLOR_HIGHLIGHT));
        } else {
            //
            //  Set Text color of Compressed items to BLUE and Encrypted items
            //  to GREEN.
            //
            if (pNode->dwAttribs & ATTR_COMPRESSED) {
                rgbText = SetTextColor(hdc, RGB(0, 0, 255));
            } else if (pNode->dwAttribs & ATTR_ENCRYPTED) {
                rgbText = SetTextColor(hdc, RGB(0, 192, 0));
            } else {
                rgbText = SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            }
            rgbBackground = SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        }

        ExtTextOut(hdc, x + dxText + dxFolder + 2 * dyBorderx2, y - (dyText / 2), ETO_OPAQUE, &rc, szPath, len, NULL);

        // draw the bitmaps as needed

        // HACK: Don't draw the bitmap when moving

        if (iShowSourceBitmaps || (hwndDragging != hwndLB) || !bDrawSelected) {
            // Blt the proper folder bitmap

            view = (DWORD)GetWindowLongPtr(GetParent(hWnd), GWL_VIEW);

            if (IsNetPath(pNode)) {
                if (bDrawSelected)
                    iBitmap = BM_IND_OPENDFS;
                else
                    iBitmap = BM_IND_CLOSEDFS;
            } else {
                if (bDrawSelected) {
                    if (pNode->dwAttribs & (ATTR_SYMBOLIC | ATTR_JUNCTION))
                        iBitmap = BM_IND_OPENREPARSE;
                    else
                        iBitmap = BM_IND_OPEN;
                } else {
                    if (pNode->dwAttribs & (ATTR_SYMBOLIC | ATTR_JUNCTION))
                        iBitmap = BM_IND_CLOSEREPARSE;
                    else
                        iBitmap = BM_IND_CLOSE;
                }
            }

            // BitBlt(hdc, x + dxText + dyBorder, y-(dyFolder/2), dxFolder, dyFolder,
            //     hdcMem, iBitmap * dxFolder, (bHasFocus && bDrawSelected) ? dyFolder : 0, SRCCOPY);

            UINT dpi = GetDpiForWindow(hWnd);

            PngDraw(hdc, dpi, x + dxText + dyBorder, y - (dyFolder / 2), PNG_TYPE_ICON, iBitmap);
        }

        // restore text stuff and draw rect as required

        if (bDrawSelected) {
            if (bHasFocus) {
                SetTextColor(hdc, rgbText);
                SetBkColor(hdc, rgbBackground);
            } else {
                HBRUSH hbr;
                if (hbr = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT))) {
                    FrameRect(hdc, &rc, hbr);
                    DeleteObject(hbr);
                }
            }
        }
    }

    if (lpLBItem->itemAction == ODA_FOCUS)
        DrawFocusRect(hdc, &rc);
}

void InvalidateNetTypes(HWND hwndTree) {
    int cItems;
    int iItem;
    HWND hwndLB;
    PDNODE pNode;

    if (!hwndTree)
        return;

    hwndLB = GetDlgItem(hwndTree, IDCW_TREELISTBOX);

    cItems = (int)SendMessage(hwndLB, LB_GETCOUNT, 0, 0L);

    for (iItem = 0; iItem < cItems; iItem++) {
        if (SendMessage(hwndLB, LB_GETTEXT, iItem, (LPARAM)&pNode) == LB_ERR)
            break;
        pNode->dwNetType = (DWORD)-1;
    }

    InvalidateRect(hwndLB, NULL, TRUE);
    UpdateWindow(hwndLB);
}

void InvalidateAllNetTypes() {
    HWND hwndT, hwndNext, hwndDir;

    for (hwndT = GetWindow(hwndMDIClient, GW_CHILD); hwndT; hwndT = hwndNext) {
        hwndNext = GetWindow(hwndT, GW_HWNDNEXT);
        if (hwndT != hwndSearch && !GetWindow(hwndT, GW_OWNER)) {
            InvalidateNetTypes(HasTreeWindow(hwndT));
            if (hwndDir = HasDirWindow(hwndT))
                SendMessage(hwndDir, FS_CHANGEDISPLAY, CD_PATH, 0L);
        }
    }

    if (hwndSearch)
        InvalidateRect(hwndSearch, NULL, FALSE);
}

/* A helper for both ExpandLevel and TreeCtlWndProc.TC_COLLAPSELEVEL.
 * Code moved from TreeCtlWndProc to be shared.  EDH 13 Oct 91
 */
void CollapseLevel(HWND hwndLB, PDNODE pNode, int nIndex) {
    PDNODE pParentNode = pNode;
    int nIndexT = nIndex;
    UINT xTreeMax;

    //
    // Don't do anything while the tree is being built.
    //
    if (GetWindowLongPtr(GetParent(hwndLB), GWL_READLEVEL))
        return;

    /* Disable redrawing early. */
    SendMessage(hwndLB, WM_SETREDRAW, FALSE, 0L);

    xTreeMax = (UINT)GetWindowLongPtr(GetParent(hwndLB), GWL_XTREEMAX);

    nIndexT++;

    /* Remove all subdirectories. */

    while (TRUE) {
        /* Make sure we don't run off the end of the listbox. */
        if (SendMessage(hwndLB, LB_GETTEXT, nIndexT, (LPARAM)&pNode) == LB_ERR)
            break;

        if (pNode->nLevels <= pParentNode->nLevels)
            break;

        if (CALC_EXTENT(pNode) == xTreeMax) {
            xTreeMax = 0;
        }

        LocalFree((HANDLE)pNode);

        SendMessage(hwndLB, LB_DELETESTRING, nIndexT, 0L);
    }

    if (xTreeMax == 0) {
        ResetTreeMax(hwndLB, FALSE);
    }

    pParentNode->wFlags &= ~TF_EXPANDED;
    SendMessage(hwndLB, WM_SETREDRAW, TRUE, 0L);

    InvalidateRect(hwndLB, NULL, TRUE);
}

void ExpandLevel(HWND hWnd, WPARAM wParam, int nIndex, LPWSTR szPath) {
    HWND hwndLB;
    PDNODE pNode;
    int iNumExpanded;
    int iBottomIndex;
    int iTopIndex;
    int iNewTopIndex;
    int iExpandInView;
    int iCurrentIndex;
    RECT rc;
    DWORD dwAttribs;

    //
    // Don't do anything while the tree is being built.
    //
    if (GetWindowLongPtr(hWnd, GWL_READLEVEL))
        return;

    hwndLB = GetDlgItem(hWnd, IDCW_TREELISTBOX);

    if (nIndex == -1)
        if ((nIndex = (int)SendMessage(hwndLB, LB_GETCURSEL, 0, 0L)) == LB_ERR)
            return;

    SendMessage(hwndLB, LB_GETTEXT, nIndex, (LPARAM)&pNode);

    // collapse the current contents so we avoid doubling existing "plus" dirs

    if (pNode->wFlags & TF_EXPANDED) {
        if (wParam)
            CollapseLevel(hwndLB, pNode, nIndex);
        else
            return;
    }

    GetTreePath(pNode, szPath);

    cNodes = 0;
    bCancelTree = FALSE;

    SendMessage(hwndLB, WM_SETREDRAW, FALSE, 0L);  // Disable redrawing.

    iCurrentIndex = (int)SendMessage(hwndLB, LB_GETCURSEL, 0, 0L);
    iNumExpanded = (int)SendMessage(hwndLB, LB_GETCOUNT, 0, 0L);
    iTopIndex = (int)SendMessage(hwndLB, LB_GETTOPINDEX, 0, 0L);
    GetClientRect(hwndLB, &rc);
    iBottomIndex = iTopIndex + (rc.bottom + 1) / dyFileName;

    U_VolInfo(DRIVEID(szPath));

    if (IsTheDiskReallyThere(hWnd, szPath, FUNC_EXPAND, FALSE)) {
        dwAttribs = ATTR_DIR | ATTR_ALL;
        ReadDirLevel(
            hWnd, pNode, szPath, pNode->nLevels + 1, nIndex, dwAttribs, (BOOL)wParam, NULL,
            IS_PARTIALSORT(DRIVEID(szPath)));
    }

    // this is how many will be in view

    iExpandInView = (iBottomIndex - (int)iCurrentIndex);

    iNumExpanded = (int)SendMessage(hwndLB, LB_GETCOUNT, 0, 0L) - iNumExpanded;

    if (iNumExpanded >= iExpandInView) {
        iNewTopIndex = min((int)iCurrentIndex, iTopIndex + iNumExpanded - iExpandInView + 1);

        // Control tree view scroll behavior on expand via winfile.ini[Settings]ScrollOnExpand. Default == TRUE
        if (TRUE == bScrollOnExpand)
            SendMessage(hwndLB, LB_SETTOPINDEX, (WPARAM)iNewTopIndex, 0L);
    }

    SendMessage(hwndLB, WM_SETREDRAW, TRUE, 0L);

    // must must ivalidate uncond. BUG WB32#222
    // "because we could get here between ownerdraw messages"
    // if (iNumExpanded)

    InvalidateRect(hwndLB, NULL, TRUE);

    // Redraw the Status Bar

    UpdateStatus(GetParent(hWnd));
}

/////////////////////////////////////////////////////////////////////
//
// Name:     TreeControlWndProc
//
// Synopsis:
//
// Return:
// Assumes:
// Effects:
// Notes:
//
/////////////////////////////////////////////////////////////////////

LRESULT
CALLBACK
TreeControlWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    int iSel;
    int i, j;
    int nIndex;
    PDNODE pNode, pNodeNext;
    HWND hwndLB;
    HWND hwndParent;

    //
    // Buffer size must be *3 since TreeWndProc::FS_GETFILESPEC
    // calls GetMDIWindowText with (szPath+strlen(szPath)) then...
    // GetMDIWindowText expects 2* MAXFILELEN!
    //

    WCHAR szPath[MAXPATHLEN * 3];

    hwndLB = GetDlgItem(hwnd, IDCW_TREELISTBOX);
    hwndParent = GetParent(hwnd);

    switch (uMsg) {
        case FS_GETDRIVE:
            return (GetWindowLongPtr(hwndParent, GWL_TYPE) + L'A');

        case TC_COLLAPSELEVEL: {
            PDNODE pParentNode;

            //
            // Don't do anything while the tree is being built.
            //
            if (GetWindowLongPtr(hwnd, GWL_READLEVEL))
                break;

            if (wParam) {
                nIndex = (int)wParam;
            } else {
                nIndex = (int)SendMessage(hwndLB, LB_GETCURSEL, 0, 0L);
                if (nIndex == LB_ERR)
                    break;
            }

            SendMessage(hwndLB, LB_GETTEXT, nIndex, (LPARAM)&pParentNode);

            //
            // short circuit if we are already in this state
            //
            if (!(pParentNode->wFlags & TF_EXPANDED))
                break;

            CollapseLevel(hwndLB, pParentNode, nIndex);

            break;
        }

        case TC_EXPANDLEVEL:

            //
            // Don't do anything while the tree is being built.
            //
            if (GetWindowLongPtr(hwnd, GWL_READLEVEL))
                break;

            ExpandLevel(hwnd, wParam, (int)-1, szPath);
            break;

        case TC_TOGGLELEVEL:

            //
            // Don't do anything while the tree is being built.
            //
            if (GetWindowLongPtr(hwnd, GWL_READLEVEL))
                return 1;

            SendMessage(hwndLB, LB_GETTEXT, (int)SendMessage(hwndLB, LB_GETCURSEL, 0, 0L), (LPARAM)&pNode);

            if (pNode->wFlags & TF_EXPANDED)
                uMsg = TC_COLLAPSELEVEL;
            else
                uMsg = TC_EXPANDLEVEL;

            SendMessage(hwnd, uMsg, FALSE, 0L);
            break;

        case TC_RECALC_EXTENT: {
            ResetTreeMax((HWND)wParam, TRUE);

            break;
        }

        case TC_GETDIR:

            //
            // get a full path for a particular dir
            // wParam is the listbox index of path to get
            // lParam LOWORD is LPWSTR to buffer to fill in
            //
            // handle INVALID_HANDLE_VALUE case = cursel
            //

            if (wParam == (WPARAM)-1) {
                wParam = (WPARAM)SendMessage(hwndLB, LB_GETCURSEL, 0, 0L);

                /*
                 *  Make sure something is highlighted, so that pNode can
                 *  get set to something.  Simply set it to the first item.
                 */
                if (wParam == (WPARAM)-1) {
                    wParam = (WPARAM)0;
                    if (SendMessage(hwndLB, LB_SETCURSEL, wParam, 0L) == LB_ERR) {
                        return ((WPARAM)-1);
                    }
                }
            }

            SendMessage(hwndLB, LB_GETTEXT, wParam, (LPARAM)&pNode);
            GetTreePath(pNode, (LPWSTR)lParam);

            return (wParam);

        case TC_SETDIRECTORY: {
            //
            // set the selection in the tree to that for a given path
            //
            DWORD i;

            if (FindItemFromPath(hwndLB, (LPWSTR)lParam, wParam != 0, &i, NULL)) {
                SendMessage(hwndLB, LB_SETCURSEL, i, 0L);

                // update dir window if it exists (which also updates MDI text)
                SendMessage(hwnd, WM_COMMAND, GET_WM_COMMAND_MPS(IDCW_TREELISTBOX, hwndLB, LBN_SELCHANGE));
            }
            break;
        }

        case TC_SETDRIVE: {
#define fFullyExpand LOBYTE(wParam)
#define fDontSteal HIBYTE(wParam)
#define fDontSelChange HIWORD(wParam)
#define szDir (LPWSTR) lParam  // NULL -> default == window text.

            //
            // Don't do anything while the tree is being built.
            //
            if (GetWindowLongPtr(hwnd, GWL_READLEVEL))
                break;

            RECT rc;
            DWORD i;
            PDNODE pNode;

            // do the same as TC_SETDIRECTORY above for the simple case
            if (FindItemFromPath(hwndLB, (LPWSTR)lParam, 0, &i, &pNode)) {
                // found exact node already displayed; select it and continue
                SendMessage(hwndLB, LB_SETCURSEL, i, 0L);

                goto UpdateDirSelection;
            }

            if (!fFullyExpand && pNode) {
                // expand in place if pNode != null; index (i) also set
                FillOutTreeList(hwnd, szDir, i, pNode);

                goto UpdateDirSelection;
            }

            // else change drive (existing code)

            //
            // is the drive/dir specified?
            //
            if (szDir) {
                lstrcpy(szPath, szDir);
            } else {
                SendMessage(hwndParent, FS_GETDIRECTORY, COUNTOF(szPath), (LPARAM)szPath);
                StripBackslash(szPath);
            }

            CharUpperBuff(szPath, 1);  // make sure

            SetWindowLongPtr(hwndParent, GWL_TYPE, szPath[0] - TEXT('A'));

            //
            // resize for new vol label
            //
            GetClientRect(hwndParent, &rc);
            SendMessage(hwndParent, WM_SIZE, SIZENOMDICRAP, MAKELONG(rc.right, rc.bottom));

            //
            // ensure the disk is available if the whole dir structure is
            // to be expanded
            //
            if (!fFullyExpand || IsTheDiskReallyThere(hwnd, szPath, FUNC_EXPAND, FALSE))
                FillTreeListbox(hwnd, szPath, fFullyExpand, fDontSteal);

        UpdateDirSelection:
            if (!fDontSelChange) {
                //
                // and force the dir half to update with a fake SELCHANGE message
                //
                SendMessage(hwnd, WM_COMMAND, GET_WM_COMMAND_MPS(IDCW_TREELISTBOX, hwndLB, LBN_SELCHANGE));
            }
            break;
#undef fFullyExpand
#undef fDontSteal
#undef fDontSelChange
#undef szDir
        }

        case WM_CHARTOITEM: {
            int cItems;
            WCHAR ch;
            PDNODE pNode;
            WCHAR rgchMatch[MAXPATHLEN];
            SIZE_T cchMatch;

            //
            // backslash means the root
            //
            if ((ch = LOWORD(wParam)) == CHAR_BACKSLASH)
                return 0L;

            cItems = (int)SendMessage(hwndLB, LB_GETCOUNT, 0, 0L);
            i = (int)SendMessage(hwndLB, LB_GETCURSEL, 0, 0L);

            if (i < 0 || ch <= CHAR_SPACE)  // filter all other control chars
                return -2L;

            // if more that one character to match, start at current position; else next position
            if (TypeAheadString(ch, rgchMatch))
                j = 0;
            else
                j = 1;
            for (; j < cItems; j++) {
                SendMessage(hwndLB, LB_GETTEXT, (i + j) % cItems, (LPARAM)&pNode);

                //
                // Do it this way to be case insensitive.
                //
                cchMatch = wcslen(rgchMatch);
                if (cchMatch > wcslen(pNode->szName))
                    cchMatch = wcslen(pNode->szName);
                if (CompareString(
                        LOCALE_USER_DEFAULT, NORM_IGNORECASE, rgchMatch, (int)cchMatch, pNode->szName, (int)cchMatch) ==
                    2)
                    break;
            }

            if (j == cItems)
                return -2L;

            SendMessage(hwndLB, LB_SETTOPINDEX, (i + j) % cItems, 0L);
            return ((i + j) % cItems);
        }

        case WM_DESTROY:
            if (hwndLB == GetFocus()) {
                HWND hwndDir;

                if (hwndDir = HasDirWindow(hwndParent))
                    SetFocus(hwndDir);
                else
                    SetFocus(hwndDriveBar);
            }
            {
                IDropTarget* pDropTarget;

                pDropTarget = (IDropTarget*)GetWindowLongPtr(hwnd, GWL_OLEDROP);
                if (pDropTarget != NULL)
                    UnregisterDropWindow(hwnd, pDropTarget);
            }
            FreeAllTreeData(hwndLB);
            break;

        case WM_XBUTTONDOWN:
            // Handle mouse side buttons for history navigation
            // XBUTTON1 (back) and XBUTTON2 (forward)
            if (HIWORD(wParam) == XBUTTON1) {
                AppCommandProc(IDM_HISTORYBACK);
                return TRUE;
            } else if (HIWORD(wParam) == XBUTTON2) {
                AppCommandProc(IDM_HISTORYFWD);
                return TRUE;
            }
            break;

        case WM_CREATE:
            //
            // create the owner draw list box for the tree
            //
            hwndLB = CreateWindowEx(
                0L, kListbox, NULL, WS_TREESTYLE | WS_BORDER, 0, 0, 0, 0, hwnd, (HMENU)IDCW_TREELISTBOX, hAppInstance,
                NULL);

            if (!hwndLB)
                return -1L;

            SendMessage(hwndLB, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
            SetWindowLongPtr(hwnd, GWL_READLEVEL, 0);

            {
                WF_IDropTarget* pDropTarget;

                RegisterDropWindow(hwnd, &pDropTarget);
                SetWindowLongPtr(hwnd, GWL_OLEDROP, (LPARAM)pDropTarget);
            }
            break;

        case WM_DRAWITEM:
            TCWP_DrawItem((LPDRAWITEMSTRUCT)lParam, hwndLB, hwnd);
            break;

        case WM_FSC: {
            PDNODE pNodePrev;
            PDNODE pNodeT;
            DWORD dwFSCOperation;
            BOOLEAN bCreationOperation;

            dwFSCOperation = FSC_Operation(wParam);
            bCreationOperation = FALSE;

            if (!lParam || dwFSCOperation == FSC_REFRESH) {
                break;
            }

            if (dwFSCOperation == FSC_MKDIR || dwFSCOperation == FSC_JUNCTION || dwFSCOperation == FSC_SYMLINKD) {
                bCreationOperation = TRUE;
            }

            //
            // search for a tree node corresponding to the item (if it is being
            // removed), or to the item's parent (if it is being added.)  If an
            // item is not found, there is no further processing to perform.
            //
            if (!FindItemFromPath(hwndLB, (LPWSTR)lParam, bCreationOperation, (DWORD*)&nIndex, &pNode)) {
                break;
            }

            lstrcpy(szPath, (LPWSTR)lParam);
            StripPath(szPath);

            switch (dwFSCOperation) {
                case FSC_MKDIR:
                case FSC_JUNCTION:
                case FSC_SYMLINKD:

                    //
                    // auto expand the branch so they can see the new
                    // directory just created
                    //
                    if (!(pNode->wFlags & TF_EXPANDED) && (nIndex == (int)SendMessage(hwndLB, LB_GETCURSEL, 0, 0L)) &&
                        ((wParam & FSC_QUIET) == 0)) {
                        SendMessage(hwnd, TC_EXPANDLEVEL, FALSE, 0L);
                    }

                    //
                    // make sure this node isn't already here
                    //
                    if (FindItemFromPath(hwndLB, (LPWSTR)lParam, FALSE, NULL, NULL)) {
                        break;
                    }

                    {
                        //
                        // Insert it into the tree listbox
                        //
                        InsertDirectory(
                            hwnd, pNode, (WORD)nIndex, szPath, &pNodeT, IsCasePreservedDrive(DRIVEID(((LPWSTR)lParam))),
                            FALSE, INVALID_FILE_ATTRIBUTES);
                    }

                    break;

                case FSC_RMDIR:

                    //
                    // Don't do anything while the tree is being built.
                    //
                    if (GetWindowLongPtr(hwnd, GWL_READLEVEL))
                        break;

                    //
                    // NEVER delete the Root Dir!
                    //
                    if (nIndex == 0)
                        break;

                    if (pNode->wFlags & TF_LASTLEVELENTRY) {
                        // We are deleting the last subdirectory.
                        // If there are previous sibling directories, mark one
                        // as the last, else mark the parent as empty and unexpanded.
                        // It is necessary to do these checks if this bit
                        // is set, since if it isn't, there is another sibling
                        // with TF_LASTLEVELENTRY set, and so the parent is nonempty.
                        //
                        // Find the previous entry which has a level not deeper than
                        // the level of that being deleted.
                        i = nIndex;
                        do {
                            SendMessage(hwndLB, LB_GETTEXT, --i, (LPARAM)&pNodePrev);
                        } while (pNodePrev->nLevels > pNode->nLevels);

                        if (pNodePrev->nLevels == pNode->nLevels) {
                            // The previous directory is a sibling... it becomes
                            // the new last level entry.
                            pNodePrev->wFlags |= TF_LASTLEVELENTRY;
                        } else {
                            // In order to find this entry, the parent must have
                            // been expanded, so if the parent of the deleted dir
                            // has no listbox entries under it, it may be assumed that
                            // the directory has no children.
                            pNodePrev->wFlags &= ~(TF_HASCHILDREN | TF_EXPANDED);
                        }
                    }

                    // Are we deleting the current selection?
                    // if so we move the selection to the item above the current.
                    // this should work in all cases because you can't delete
                    // the root.

                    j = (int)SendMessage(hwndLB, LB_GETCOUNT, 0, 0L);
                    i = (int)SendMessage(hwndLB, LB_GETCURSEL, 0, 0L);

                    SendMessage(hwnd, TC_COLLAPSELEVEL, nIndex, 0L);
                    SendMessage(hwndLB, LB_DELETESTRING, nIndex, 0L);

                    if (i >= nIndex) {
                        //
                        // Set j to the number of dirs removed from the list
                        //
                        j -= (int)SendMessage(hwndLB, LB_GETCOUNT, 0, 0L);

                        if (i < nIndex + j) {
                            //
                            // In the quiet case, don't change selection.
                            // FSC_RENAME will handle this for us.
                            //
                            if ((wParam & FSC_QUIET) == 0) {
                                SendMessage(hwndLB, LB_SETCURSEL, nIndex - 1, 0L);
                                SendMessage(hwnd, WM_COMMAND, GET_WM_COMMAND_MPS(0, hwndLB, LBN_SELCHANGE));
                            }

                        } else {
                            SendMessage(hwndLB, LB_SETCURSEL, i - j, 0L);
                        }
                    }

                    if (CALC_EXTENT(pNode) == (ULONG)GetWindowLongPtr(hwnd, GWL_XTREEMAX)) {
                        ResetTreeMax(hwndLB, FALSE);
                    }

                    LocalFree((HANDLE)pNode);
                    break;
            }
            break;
        }

        case WM_COMMAND: {
            UINT id;

            id = GET_WM_COMMAND_ID(wParam, lParam);
            switch (GET_WM_COMMAND_CMD(wParam, lParam)) {
                case LBN_SELCHANGE: {
                    HWND hwndDir;
                    int CurSel;
                    UINT uStrLen;

                    //
                    // CurSel is returned from SendMessage
                    //
                    CurSel = (int)SendMessage(hwnd, TC_GETDIR, (WPARAM)-1, (LPARAM)szPath);
                    if (CurSel == -1) {
                        break;
                    }

                    AddBackslash(szPath);

                    uStrLen = lstrlen(szPath);
                    SendMessage(hwndParent, FS_GETFILESPEC, COUNTOF(szPath) - uStrLen, (LPARAM)(szPath + uStrLen));

                    if (hwndDir = HasDirWindow(hwndParent)) {
                        //
                        // update the dir window
                        //
                        id = CD_PATH;

                        //
                        // don't allow abort on first or last directories
                        //
                        if (CurSel > 0 && CurSel != ((int)SendMessage(hwndLB, LB_GETCOUNT, 0, 0L) - 1)) {
                            id = CD_PATH | CD_ALLOWABORT;
                        }

                        SendMessage(hwndDir, FS_CHANGEDISPLAY, id, (LPARAM)szPath);

                    } else {
                        // TODO: why isn't this part of TC_SETDRIVE?  currently when a tree only is shown, the MDI
                        // window text is not updated
                        SetMDIWindowText(hwndParent, szPath);
                    }

                    //
                    // Don't put ModifyWatchList here, since it slows down
                    // the allowabort case
                    //
                    UpdateStatus(hwndParent);
                    break;
                }

                case LBN_DBLCLK:
                    SendMessage(hwndFrame, WM_COMMAND, GET_WM_COMMAND_MPS(IDM_OPEN, 0, 0));
                    break;

                case LBN_SETFOCUS: {
                    RECT rect;

                    SetWindowLongPtr(hwndParent, GWL_LASTFOCUS, (LPARAM)GET_WM_COMMAND_HWND(wParam, lParam));
                    UpdateStatus(hwndParent);  // update the status bar
                UpdateSelection:

                    iSel = (int)SendMessage(hwndLB, LB_GETCURSEL, 0, 0L);

                    if ((int)SendMessage(hwndLB, LB_GETITEMRECT, (WPARAM)iSel, (LPARAM)&rect) != LB_ERR)

                        InvalidateRect(hwndLB, &rect, TRUE);

                    break;
                }

                case LBN_KILLFOCUS:
                    SetWindowLongPtr(hwndParent, GWL_LASTFOCUS, 0L);
                    SetWindowLongPtr(hwndParent, GWL_LASTFOCUS, (LPARAM)GET_WM_COMMAND_HWND(wParam, lParam));

                    goto UpdateSelection;
            }
            break;
        }

        case WM_CONTEXTMENU:
            ActivateCommonContextMenu(hwnd, hwndLB, lParam);
            break;

        case WM_MEASUREITEM:
            ((LPMEASUREITEMSTRUCT)lParam)->itemHeight = dyFileName;
            break;

        case WM_VKEYTOITEM:

            i = (int)SendMessage(hwndLB, LB_GETCURSEL, 0, 0L);
            if (i < 0)
                return -2L;

            j = 1;
            SendMessage(hwndLB, LB_GETTEXT, i, (LPARAM)&pNode);

            switch (GET_WM_VKEYTOITEM_CODE(wParam, lParam)) {
                case VK_LEFT:
                    TypeAheadString('\0', NULL);

                    // if node is expanded and no control key, just collapse
                    if ((pNode->wFlags & TF_EXPANDED) != 0 && GetKeyState(VK_CONTROL) >= 0) {
                        CollapseLevel(hwndLB, pNode, i);
                        return (i);
                    }

                    while (SendMessage(hwndLB, LB_GETTEXT, --i, (LPARAM)&pNodeNext) != LB_ERR) {
                        if (pNodeNext == pNode->pParent)
                            return (i);
                    }
                    goto SameSelection;

                case VK_RIGHT:
                    TypeAheadString('\0', NULL);

                    // if node is not expanded and no control key, just expand
                    if (!(pNode->wFlags & TF_EXPANDED) && GetKeyState(VK_CONTROL) >= 0) {
                        ExpandLevel(hwnd, 0, i, szPath);
                        return (i);
                    }

                    if ((SendMessage(hwndLB, LB_GETTEXT, i + 1, (LPARAM)&pNodeNext) == LB_ERR) ||
                        (pNodeNext->pParent != pNode)) {
                        goto SameSelection;
                    }
                    return (i + 1);

                case VK_UP:
                    j = -1;
                    /** FALL THROUGH ***/

                case VK_DOWN:
                    TypeAheadString('\0', NULL);

                    //
                    // If the control key is not down, use default behavior.
                    //
                    if (GetKeyState(VK_CONTROL) >= 0)
                        return (-1L);

                    while (SendMessage(hwndLB, LB_GETTEXT, i += j, (LPARAM)&pNodeNext) != LB_ERR) {
                        if (pNodeNext->pParent == pNode->pParent)
                            return (i);
                    }

                SameSelection:
                    MessageBeep(0);
                    return (-2L);

                case VK_F6:  // like excel
                case VK_TAB: {
                    HWND hwndDir, hwndSet, hwndNext, hwndTemp;
                    BOOL bDir;
                    BOOL bChangeDisplay = FALSE;

                    hwndNext = NULL;
                    TypeAheadString('\0', NULL);
                    GetTreeWindows(hwndParent, NULL, &hwndDir);

                    //
                    // Check to see if we can change to the directory window
                    //
                    bDir = hwndDir != NULL;
                    if (bDir) {
                        HWND hwndLB;

                        bChangeDisplay = (BOOL)GetWindowLongPtr(hwndDir, GWLP_USERDATA);

                        hwndLB = GetDlgItem(hwndDir, IDCW_LISTBOX);
                        if (hwndLB && !bChangeDisplay) {
                            PVOID pv;
                            SendMessage(hwndLB, LB_GETTEXT, 0, (LPARAM)&pv);
                            bDir = pv != NULL;
                        }
                    }

                    if (GetKeyState(VK_SHIFT) < 0) {
                        if (bDriveBar) {
                            hwndSet = hwndDriveBar;
                        } else {
                            if (bDir) {
                                hwndSet = hwndDir;
                                hwndNext = hwnd;
                            } else {
                                hwndSet = hwnd;
                            }
                        }
                    } else {
                        hwndTemp = (!bDriveBar) ? hwnd : hwndDriveBar;

                        hwndSet = bDir ? hwndDir : hwndTemp;
                        hwndNext = hwndTemp;
                    }

                    SetFocus(hwndSet);
                    if ((hwndSet == hwndDir) && bChangeDisplay) {
                        SetWindowLongPtr(hwndDir, GWL_NEXTHWND, (LPARAM)hwndNext);
                    }

                    return -2L;  // I dealt with this!
                }

                case VK_BACK: {
                    int nStartLevel;

                    TypeAheadString('\0', NULL);
                    if (i <= 0)
                        return -2L;  // root case

                    nStartLevel = pNode->nLevels;

                    do {
                        SendMessage(hwndLB, LB_GETTEXT, --i, (LPARAM)&pNodeNext);
                    } while (i > 0 && pNodeNext->nLevels >= nStartLevel);

                    return i;
                }

                default:
                    // Select disc by pressing CTRL + ALT + letter
                    if ((GetKeyState(VK_CONTROL) < 0) && (GetKeyState(VK_MENU) < 0))
                        return SendMessage(hwndDriveBar, uMsg, wParam, lParam);

                    return -1L;
            }
            break;

        case WM_SETFOCUS:
        case WM_LBUTTONDOWN:
            SetFocus(hwndLB);
            break;

        case WM_SIZE:
            if (!IsIconic(hwndParent)) {
                int iMax;

                MoveWindow(hwndLB, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);

                // Resizing doesn't send invalidate region, so
                // we must do it manually if the scroll bar

                // invalidate tree rect (if one exists)
                // (actually only do if sb thumb is _not_ at extreme left,
                // the sb range may be re-coded later so for safety, invalidate
                // in all cases.

                InvalidateRect(hwndLB, NULL, TRUE);

                iMax = (int)SendMessage(hwndLB, LB_GETCURSEL, 0, 0L);
                if (iMax >= 0) {
                    RECT rc;
                    int top, bottom;

                    GetClientRect(hwndLB, &rc);
                    top = (int)SendMessage(hwndLB, LB_GETTOPINDEX, 0, 0L);
                    bottom = top + rc.bottom / dyFileName;
                    if (iMax < top || iMax > bottom)
                        SendMessage(hwndLB, LB_SETTOPINDEX, iMax - ((bottom - top) / 2), 0L);
                }
            }
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0L;
}

int BuildTreeName(LPWSTR lpszPath, int iLen, int iSize) {
    DRIVE drive = DRIVEID(lpszPath);

    if (3 != iLen || CHAR_BACKSLASH != lpszPath[2])
        return iLen;

    lstrcat(lpszPath, SZ_FILESYSNAMESEP);
    iLen = lstrlen(lpszPath);

    // Add type
    U_VolInfo(drive);

    if (ERROR_SUCCESS == GETRETVAL(VolInfo, drive)) {
        StrNCpy(&lpszPath[iLen], aDriveInfo[drive].szFileSysName, iSize - iLen - 1);
    }

    iLen = lstrlen(lpszPath);
    return iLen;
}

/*
 *  Get the real text extent for the current directory and save it
 *  in the pNode.
 */
UINT GetRealExtent(PDNODE pNode, HWND hwndLB, LPWSTR szPath, int* pLen)

{
    HDC hdc;
    HFONT hOld;
    SIZE size;
    WCHAR szTemp[MAXPATHLEN];

    if (szPath == NULL) {
        szPath = szTemp;
    }

    *pLen = lstrlen(pNode->szName);
    lstrcpy(szPath, pNode->szName);

    if ((wTextAttribs & TA_LOWERCASE) && (pNode->wFlags & TF_LOWERCASE) || (wTextAttribs & TA_LOWERCASEALL)) {
        CharLower(szPath);
    }

    *pLen = BuildTreeName(szPath, *pLen, MAXPATHLEN);

    if (hwndLB != NULL) {
        hdc = GetDC(hwndLB);
        hOld = (HFONT)SelectObject(hdc, hFont);
        GetTextExtentPoint32(hdc, szPath, *pLen, &size);
        if (hOld)
            SelectObject(hdc, hOld);

        pNode->dwExtent = size.cx;
        ReleaseDC(hwndLB, hdc);
    }

    return (pNode->dwExtent);
}

/*
 *  Resets the xTreeMax value.  If fReCalcExtent is TRUE, it
 *  also recalculates all of the text extents for each pNode in the
 *  list box.
 */
void ResetTreeMax(HWND hwndLB, BOOL fReCalcExtent)

{
    DWORD NumItems;
    DWORD ctr;
    PDNODE pNode;
    int Len;
    UINT xNew, xTreeMax;

    NumItems = (DWORD)SendMessage(hwndLB, LB_GETCOUNT, 0, 0);

    xTreeMax = 0;

    for (ctr = 0; ctr < NumItems; ctr++) {
        SendMessage(hwndLB, LB_GETTEXT, (WPARAM)ctr, (LPARAM)&pNode);

        if (fReCalcExtent) {
            GetRealExtent(pNode, hwndLB, NULL, &Len);
        }

        if (xTreeMax < (xNew = CALC_EXTENT(pNode))) {
            xTreeMax = xNew;
        }
    }

    SetWindowLongPtr(GetParent(hwndLB), GWL_XTREEMAX, xTreeMax);
    SendMessage(hwndLB, LB_SETHORIZONTALEXTENT, xTreeMax, 0L);
}
