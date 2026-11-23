#include "json.h"
#include "../shop/shop.h"
#include <limits>

static int ReadPrices(const std::wstring& path, const char* const* names, unsigned* prices,
    size_t count, char* result, size_t size)
{
    using namespace hfjson;
    std::string text, why;
    Value document;
    if (!ReadFile(path, &text, &why) || !Parse(text, &document, &why))
    {
        snprintf(result, size, "%s", why.c_str());
        return 0;
    }
    if (document.kind != Value::Object || !count)
    {
        snprintf(result, size, "Expected an object containing every supported item and its price.");
        return 0;
    }
    std::vector<unsigned> loaded(count);
    for (const auto& member : document.members)
    {
        size_t index = 0;
        while (index < count && member.first != names[index]) ++index;
        if (index == count)
        {
            snprintf(result, size, "Unknown item: %s.", member.first.c_str());
            return 0;
        }
        if (loaded[index])
        {
            snprintf(result, size, "Duplicate item: %s.", names[index]);
            return 0;
        }
        const Value& value = member.second;
        const unsigned maximum = static_cast<unsigned>(std::numeric_limits<unsigned>::max() / count);
        if (value.kind != Value::Number || value.text.empty() || value.text.size() > 10 ||
            value.text.find_first_not_of("0123456789") != std::string::npos ||
            strtoull(value.text.c_str(), nullptr, 10) == 0 ||
            strtoull(value.text.c_str(), nullptr, 10) > maximum)
        {
            snprintf(result, size, "Invalid price for %s: use a whole number from 1 to %u.", names[index], maximum);
            return 0;
        }
        loaded[index] = static_cast<unsigned>(strtoull(value.text.c_str(), nullptr, 10));
    }
    for (size_t i = 0; i < count; ++i)
        if (!loaded[i])
        {
            snprintf(result, size, "Missing item: %s.", names[i]);
            return 0;
        }
    for (size_t i = 0; i < count; ++i) prices[i] = loaded[i];
    return 1;
}

static std::wstring ConfigPath(const wchar_t* filename, char* result, size_t size)
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

extern "C" int SmrLoadPrices(const char* const* names, unsigned* prices, size_t count, char* result, size_t size)
{
    const auto path = ConfigPath(L"prices.json", result, size);
    return !path.empty() && ReadPrices(path, names, prices, count, result, size);
}
