#include "json.h"
#include "config_path.h"
#include "../shop/shop.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static int QualifiedModContent(const std::string& name)
{
    return name.find(':') != std::string::npos || name.find('/') != std::string::npos;
}

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
    std::vector<bool> listed(count);
    for (const auto& member : document.members)
    {
        size_t index = 0;
        while (index < count && member.first != names[index]) ++index;
        if (index == count)
        {
            if (QualifiedModContent(member.first)) continue;
            snprintf(result, size, "Unknown item: %s.", member.first.c_str());
            return 0;
        }
        if (listed[index])
        {
            snprintf(result, size, "Duplicate item: %s.", names[index]);
            return 0;
        }
        const Value& value = member.second;
        const unsigned maximum = static_cast<unsigned>(std::numeric_limits<unsigned>::max() / count);
        if (value.kind != Value::Number || value.text.empty() || value.text.size() > 10 ||
            value.text.find_first_not_of("0123456789") != std::string::npos ||
            strtoull(value.text.c_str(), nullptr, 10) > maximum)
        {
            snprintf(result, size, "Invalid price for %s: use a whole number from 0 to %u.", names[index], maximum);
            return 0;
        }
        loaded[index] = static_cast<unsigned>(strtoull(value.text.c_str(), nullptr, 10));
        listed[index] = true;
    }
    for (size_t i = 0; i < count; ++i)
        if (!listed[i])
        {
            snprintf(result, size, "Missing item: %s.", names[i]);
            return 0;
        }
    for (size_t i = 0; i < count; ++i) prices[i] = loaded[i];
    return 1;
}

extern "C" int SmrLoadPrices(const char* const* names, unsigned* prices, size_t count, char* result, size_t size)
{
    const auto path = ConfigPath(L"prices.json", result, size);
    return !path.empty() && ReadPrices(path, names, prices, count, result, size);
}

/* Unlike prices.json, enemies may be left out: they keep the default value. */
static int ReadKillRewards(const std::wstring& path, const char* const* names, unsigned* values, size_t count,
    char* result, size_t size)
{
    using namespace hfjson;
    std::string text, why;
    Value document;
    if (!ReadFile(path, &text, &why) || !Parse(text, &document, &why))
    {
        snprintf(result, size, "%s", why.c_str());
        return 0;
    }
    if (document.kind != Value::Object)
    {
        snprintf(result, size, "Expected an object of enemy names and their Requisition values.");
        return 0;
    }
    std::vector<unsigned> loaded(count, SMR_KILL_DEFAULT_VALUE);
    std::vector<bool> listed(count);
    for (const auto& member : document.members)
    {
        size_t index = 0;
        while (index < count && member.first != names[index]) ++index;
        if (index == count)
        {
            if (QualifiedModContent(member.first)) continue;
            snprintf(result, size, "Unknown enemy: %s.", member.first.c_str());
            return 0;
        }
        if (listed[index])
        {
            snprintf(result, size, "Duplicate enemy: %s.", names[index]);
            return 0;
        }
        const Value& value = member.second;
        if (value.kind != Value::Number || value.text.empty() || value.text.size() > 7 ||
            value.text.find_first_not_of("0123456789") != std::string::npos ||
            strtoul(value.text.c_str(), nullptr, 10) > SMR_KILL_MAX_VALUE)
        {
            snprintf(result, size, "Invalid value for %s: use a whole number from 0 to %u.", names[index],
                SMR_KILL_MAX_VALUE);
            return 0;
        }
        loaded[index] = static_cast<unsigned>(strtoul(value.text.c_str(), nullptr, 10));
        listed[index] = true;
    }
    for (size_t i = 0; i < count; ++i) values[i] = loaded[i];
    return 1;
}

extern "C" int SmrLoadKillRewards(const char* const* names, unsigned* values, size_t count, char* result, size_t size)
{
    const auto path = ConfigPath(L"kill-rewards.json", result, size);
    return !path.empty() && ReadKillRewards(path, names, values, count, result, size);
}
