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
        snprintf(result, size, "Expected an object containing the four upgrade sections.");
        return 0;
    }
    constexpr unsigned orbital = 1u << SMR_UPGRADE_ORBITAL, eagle = 1u << SMR_UPGRADE_EAGLE,
        fixedCooldown = eagle | (1u << SMR_UPGRADE_DEFENSIVE) | (1u << SMR_UPGRADE_SUPPORT),
        all = orbital | fixedCooldown;
    constexpr const char* sections[] = {"", "orbital", "eagle", "defensive", "support"};
    constexpr struct {
        const char* name;
        unsigned sections;
        double min, max;
        double SmrUpgradeTuning::* number;
        unsigned SmrUpgradeTuning::* integer;
    } fields[] = {
        {"forge_credits", all, 1, UINT32_MAX, nullptr, &SmrUpgradeTuning::forge_credits},
        {"cooldown_percent", orbital, -100, 10000, &SmrUpgradeTuning::cooldown_percent, nullptr},
        {"other_cooldown_percent", all, -100, 10000, &SmrUpgradeTuning::other_cooldown_percent, nullptr},
        {"cooldown_seconds", fixedCooldown, 0, 86400, &SmrUpgradeTuning::cooldown_seconds, nullptr},
        {"call_in_extra_seconds", eagle, 0, 86400, &SmrUpgradeTuning::call_in_extra_seconds, nullptr},
        {"rearm_seconds", eagle, 0, 86400, &SmrUpgradeTuning::rearm_seconds, nullptr},
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
                (field.integer && value.text.find_first_not_of("0123456789") != std::string::npos))
            {
                snprintf(result, size, "%s.%s must be a %s from %.0f to %.0f.", sections[index], field.name,
                    field.integer ? "whole number" : "number", field.min, field.max);
                return 0;
            }
            if (field.integer) loaded[index].*field.integer = static_cast<unsigned>(number);
            else loaded[index].*field.number = number;
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
        if (!(seenSections & (1u << i)))
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
