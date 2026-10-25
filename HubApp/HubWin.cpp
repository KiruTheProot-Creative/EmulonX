// SPDX-License-Identifier: AGPL-3.0-or-later
#include "HubWin.h"

#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <restartmanager.h>
 #pragma comment (lib, "Rstrtmgr.lib")
#endif

namespace emx::hub::win
{
std::vector<std::wstring> programsUsing (const std::vector<std::wstring>& files)
{
    std::vector<std::wstring> names;
   #ifdef _WIN32
    std::vector<LPCWSTR> paths;
    for (auto& f : files)
        if (GetFileAttributesW (f.c_str()) != INVALID_FILE_ATTRIBUTES)
            paths.push_back (f.c_str());
    if (paths.empty())
        return names;

    DWORD session = 0;
    WCHAR key[CCH_RM_SESSION_KEY + 1] = {};
    if (RmStartSession (&session, 0, key) != ERROR_SUCCESS)
        return names;
    if (RmRegisterResources (session, (UINT) paths.size(), paths.data(), 0, nullptr, 0, nullptr) == ERROR_SUCCESS)
    {
        UINT needed = 0, count = 0;
        DWORD reasons = 0;
        DWORD result = RmGetList (session, &needed, &count, nullptr, &reasons);
        if (result == ERROR_MORE_DATA && needed > 0)
        {
            std::vector<RM_PROCESS_INFO> info (needed);
            count = needed;
            if (RmGetList (session, &needed, &count, info.data(), &reasons) == ERROR_SUCCESS)
                for (UINT i = 0; i < count; ++i)
                {
                    std::wstring n = info[i].strAppName;
                    bool seen = false;
                    for (auto& x : names)
                        seen = seen || x == n;
                    if (! n.empty() && ! seen)
                        names.push_back (n);
                }
        }
    }
    RmEndSession (session);
   #else
    (void) files;
   #endif
    return names;
}
} // namespace emx::hub::win
