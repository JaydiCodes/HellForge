#include <cmath>

static int ReadUpgradeConfig(const std::wstring& path, SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT],
    char* result, size_t size)
{
    using namespace hfjson;
    Value document;
    std::string text, why;
    if (!ReadFile(path, &text, &why) || !Parse(text, &document, &why))
    {
        snprintf(result, size, "%s", why.c_str());
        return 0;
    }
    if (document.kind != Value::Object)
    {
        snprintf(result, size, "Expected an object containing the upgrade sections.");
        return 0;
    }
    constexpr unsigned eagle = 1u << SMR_UPGRADE_EAGLE,
        percentCooldown = (1u << SMR_UPGRADE_ORBITAL) | (1u << SMR_UPGRADE_DEFENSIVE) | (1u << SMR_UPGRADE_SUPPORT),
        required = eagle | percentCooldown, all = required | (1u << SMR_UPGRADE_EXTRA_SLOT);
    constexpr const char* sections[] = {"", "orbital", "eagle", "defensive", "support", "extra_slot"};
    constexpr struct {
        const char* name;
        unsigned sections;
        double min, max;
        double SmrUpgradeTuning::* number; /* null for whole-number prices */
        size_t currency;
    } fields[] = {
        {"forge_credits", all, 1, UINT32_MAX, nullptr, FORGE_CREDITS},
        {"common_samples", all, 0, UINT32_MAX, nullptr, COMMON_SAMPLES},
        {"rare_samples", all, 0, UINT32_MAX, nullptr, RARE_SAMPLES},
        {"super_samples", all, 0, UINT32_MAX, nullptr, SUPER_SAMPLES},
        {"cooldown_percent", percentCooldown, -100, 10000, &SmrUpgradeTuning::cooldown_percent, 0},
        {"other_cooldown_percent", all, -100, 10000, &SmrUpgradeTuning::other_cooldown_percent, 0},
        {"cooldown_seconds", eagle, 0, 86400, &SmrUpgradeTuning::cooldown_seconds, 0},
        {"call_in_extra_seconds", eagle, 0, 86400, &SmrUpgradeTuning::call_in_extra_seconds, 0},
        {"rearm_seconds", eagle, 0, 86400, &SmrUpgradeTuning::rearm_seconds, 0},
    };
    SmrUpgradeTuning loaded[SMR_UPGRADE_COUNT] = {};
    unsigned seenSections = 0;
    for (const auto& member : document.members)
    {
        unsigned index = 1;
        while (index < SMR_UPGRADE_COUNT && member.first != sections[index]) ++index;
        const unsigned bit = 1u << index;
        if (index == SMR_UPGRADE_COUNT || (seenSections & bit) || member.second.kind != Value::Object)
        {
            snprintf(result, size, "Unknown, duplicate or non-object upgrade section: %s.", member.first.c_str());
            return 0;
        }
        seenSections |= bit;
        unsigned seen = 0;
        for (const auto& entry : member.second.members)
        {
            size_t i = 0;
            while (i < sizeof fields / sizeof fields[0] && entry.first != fields[i].name) ++i;
            if (i == sizeof fields / sizeof fields[0] || !(fields[i].sections & bit) || (seen & (1u << i)))
            {
                snprintf(result, size, "Unknown or duplicate setting: %s.%s.", sections[index], entry.first.c_str());
                return 0;
            }
            const auto& field = fields[i];
            const auto& value = entry.second;
            char* end = nullptr;
            const double number = strtod(value.text.c_str(), &end);
            if (value.kind != Value::Number || end == value.text.c_str() || *end ||
                !std::isfinite(number) || number < field.min || number > field.max ||
                (!field.number && value.text.find_first_not_of("0123456789") != std::string::npos))
            {
                snprintf(result, size, "%s.%s must be a %s from %.0f to %.0f.", sections[index], field.name,
                    field.number ? "number" : "whole number", field.min, field.max);
                return 0;
            }
            if (field.number) loaded[index].*field.number = number;
            else loaded[index].price[field.currency] = static_cast<unsigned>(number);
            seen |= 1u << i;
        }
        for (size_t i = 0; i < sizeof fields / sizeof fields[0]; ++i)
            if ((fields[i].sections & bit) && !(seen & (1u << i)))
            {
                snprintf(result, size, "Missing setting: %s.%s.", sections[index], fields[i].name);
                return 0;
            }
    }
    for (unsigned i = 1; i < SMR_UPGRADE_COUNT; ++i)
        if ((required & (1u << i)) && !(seenSections & (1u << i)))
        {
            snprintf(result, size, "Missing upgrade section: %s.", sections[i]);
            return 0;
        }
    for (unsigned i = 0; i < SMR_UPGRADE_COUNT; ++i) tuning[i] = loaded[i];
    return 1;
}

extern "C" int SmrLoadUpgradeConfig(SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT], char* result, size_t size)
{
    const auto path = ConfigPath(L"destroyer-upgrades.json", result, size);
    return !path.empty() && ReadUpgradeConfig(path, tuning, result, size);
}
