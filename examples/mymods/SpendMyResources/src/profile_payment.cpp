#include "json.cpp"
#include "shop.h"
#include <limits>

static int ProfileAmount(const hfjson::Value* amount, unsigned* out)
{
    if (!amount || amount->kind != hfjson::Value::Number || amount->text.empty() ||
        amount->text.size() > 10 || amount->text.find_first_not_of("0123456789") != std::string::npos)
        return 0;
    unsigned long long value = strtoull(amount->text.c_str(), nullptr, 10);
    if (value > std::numeric_limits<unsigned>::max()) return 0;
    *out = static_cast<unsigned>(value);
    return 1;
}

static int UpdateProfile(const char* logs, uint32_t resource, unsigned cost, uint32_t target, unsigned credit,
    char* result, size_t size)
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
    Value* sourceItem = nullptr;
    Value* targetItem = nullptr;
    if (items && items->kind == Value::Array)
        for (auto& item : items->items)
        {
            if (item.GetNumber("mixId", -1) == resource)
            {
                if (sourceItem)
                {
                    snprintf(result, size, "Profile NOT edited: duplicate resource entries.");
                    return 0;
                }
                sourceItem = &item;
            }
            if (target && item.GetNumber("mixId", -1) == target)
            {
                if (targetItem)
                {
                    snprintf(result, size, "Profile NOT edited: duplicate destination entries.");
                    return 0;
                }
                targetItem = &item;
            }
        }
    unsigned before = 0, targetBefore = 0;
    if (!cost || !ProfileAmount(sourceItem ? sourceItem->Find("amount") : nullptr, &before) || before < cost)
    {
        snprintf(result, size, "Profile NOT edited: resource amount missing, invalid or insufficient.");
        return 0;
    }
    if (target && (!credit || target == resource ||
        (targetItem && !ProfileAmount(targetItem->Find("amount"), &targetBefore)) ||
        targetBefore > std::numeric_limits<unsigned>::max() - credit))
    {
        snprintf(result, size, "Profile NOT edited: destination amount invalid or would overflow.");
        return 0;
    }
    sourceItem->At("amount") = Value::MakeNumber(before - cost, true);
    if (target)
    {
        if (targetItem) targetItem->At("amount") = Value::MakeNumber(targetBefore + credit, true);
        else
        {
            Value added;
            added.kind = Value::Object;
            added.At("mixId") = Value::MakeNumber(target, true);
            added.At("amount") = Value::MakeNumber(credit, true);
            items->items.push_back(std::move(added));
        }
    }
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

extern "C" int SmrDeductProfile(const char* logs, uint32_t resource, unsigned cost, char* result, size_t size)
{
    return UpdateProfile(logs, resource, cost, 0, 0, result, size);
}

extern "C" int SmrExchangeProfile(const char* logs, uint32_t source, unsigned debit, uint32_t target, unsigned credit,
    char* result, size_t size)
{
    if (!target)
    {
        snprintf(result, size, "Profile NOT edited: destination resource missing.");
        return 0;
    }
    return UpdateProfile(logs, source, debit, target, credit, result, size);
}
