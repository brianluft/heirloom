/********************************************************************

   WNetCaps.c

   Returns net status

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#include "winfile.h"
#include "wnetcaps.h"

/////////////////////////////////////////////////////////////////////
//
// Name:     WNetStat
//
// Synopsis: Caches and returns requested network status information
//
// IN        nIndex  NS_* request
//                   NS_REFRESH refreshes cached info
//
// Return:   BOOL    on NS_* => answer
//                   on NS_REFRESH => unstable (FALSE)
//
// Assumes:  nIndex < 1 << 31 and  nIndex an even power of 2
//
// Effects:
//
//
// Notes:
//
/////////////////////////////////////////////////////////////////////

BOOL WNetStat(int nIndex) {
    static DWORD fdwRet = (DWORD)-1;
    DWORD dwError;

    BOOL bNetwork = FALSE;
    BOOL bConnect = FALSE;

    HKEY hKey;

    DWORD dwcbBuffer = 0;

    if (
//
// Disable NS_REFRESH since we test for network installed on disk,
// not network services started.
//
#if NSREFRESH
        NS_REFRESH == nIndex ||
#endif
        (DWORD)-1 == fdwRet) {

        fdwRet = 0;

        //
        // Check for connection dialog
        //

        dwError = RegOpenKey(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\NetworkProvider\\Order", &hKey);

        if (!dwError) {
            dwError = RegQueryValueEx(hKey, L"ProviderOrder", NULL, NULL, NULL, &dwcbBuffer);

            if (ERROR_SUCCESS == dwError && dwcbBuffer > 1) {
                bNetwork = TRUE;
            }

            RegCloseKey(hKey);
        }

        if (bNetwork) {
            fdwRet |= NS_CONNECTDLG | NS_CONNECT;
        }

        //
        // Check for share-ability
        //

        dwError = RegOpenKey(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Services\\LanmanServer", &hKey);

        if (!dwError) {
            fdwRet |= NS_SHAREDLG | NS_PROPERTYDLG;
            RegCloseKey(hKey);
        }
    }

    return fdwRet & nIndex ? TRUE : FALSE;
}
