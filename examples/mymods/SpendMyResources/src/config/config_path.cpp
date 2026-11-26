#include <windows.h>
#include <cstdio>
#include "config_path.h"

std::wstring ConfigPath(const wchar_t* filename, char* result, size_t size)
{
    static const char moduleAnchor = 0;
    HMODULE module = nullptr;
    wchar_t path[32768];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&moduleAnchor), &module))
    {
        snprintf(result, size, "Cannot locate the mod DLL (Windows error %lu).", GetLastError());
        return {};
    }
    DWORD length = GetModuleFileNameW(module, path, sizeof path / sizeof path[0]);
    if (!length || length >= sizeof path / sizeof path[0])
    {
        snprintf(result, size, "Cannot read the mod DLL path (Windows error %lu).", GetLastError());
        return {};
    }
    const std::wstring dllPath(path, length);
    return dllPath.substr(0, dllPath.find_last_of(L'\\') + 1) + filename;
}
