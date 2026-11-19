#include "json.cpp"
#include "shop.h"
#include <limits>

extern "C" int SmrDeductProfile(const char* logs, uint32_t resource, unsigned cost, char* result, size_t size)
{
    using namespace hfjson;
    std::wstring profilePath;
    std::string text, why;
    Value ready, profile;
    const std::wstring logPath = Wide(logs ? logs : "");
    for (const wchar_t* suffix : {L"\\backend\\ready.json", L"\\..\\backend\\ready.json"})
    {
        if (!ReadFile(logPath + suffix, &text, &why)) continue;
        if (!Parse(text, &ready, &why)) break;
        profilePath = Wide(ready.GetString("profile"));
        break;
    }
    if (profilePath.empty())
    {
        snprintf(result, size, "Profile NOT edited: session profile path unavailable.");
        return 0;
    }
    if (!ReadFile(profilePath, &text, &why) || !Parse(text, &profile, &why))
    {
        snprintf(result, size, "Profile NOT edited: %s.", why.c_str());
        return 0;
    }
    Value* inventory = profile.Find("inventory");
    Value* items = inventory && inventory->kind == Value::Object ? inventory->Find("items") : nullptr;
    Value* amount = nullptr;
    if (items && items->kind == Value::Array)
        for (auto& item : items->items)
            if (item.GetNumber("mixId", -1) == resource)
            {
                if (amount)
                {
                    snprintf(result, size, "Profile NOT edited: duplicate resource entries.");
                    return 0;
                }
                amount = item.Find("amount");
            }
    unsigned long long before = 0;
    if (!amount || amount->kind != Value::Number || amount->text.empty() ||
        amount->text.find_first_not_of("0123456789") != std::string::npos ||
        amount->text.size() > 10 || !cost || (before = strtoull(amount->text.c_str(), nullptr, 10)) < cost ||
        before > std::numeric_limits<unsigned>::max())
    {
        snprintf(result, size, "Profile NOT edited: resource amount missing, invalid or insufficient.");
        return 0;
    }
    *amount = Value::MakeNumber(static_cast<double>(before - cost), true);
    const std::string updated = Write(profile);
    if (!WriteAtomically(profilePath, updated))
    {
        snprintf(result, size, "Profile write FAILED (Windows error %lu).",
            static_cast<unsigned long>(GetLastError()));
        return 0;
    }
    if (!ReadFile(profilePath, &text, &why) || text != updated)
    {
        snprintf(result, size, "Profile write could NOT be verified (unreadable or changed again).");
        return 0;
    }
    snprintf(result, size, "Profile resource %u: %u -> %u.", resource,
        static_cast<unsigned>(before), static_cast<unsigned>(before - cost));
    return 1;
}
