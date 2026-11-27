#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <cfloat>
#include "json.h"
#include "config_path.h"
#include "../shop/shop.h"

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
    constexpr unsigned defaultRequisition = 2000;
    constexpr const char* sections[] = {"", "orbital", "eagle", "defensive", "support", "extra_slot"};
    constexpr struct {
        const char* name;
        unsigned sections;
        double min, max;
        double SmrUpgradeTuning::* number; /* null for whole-number prices */
        size_t currency;
    } fields[] = {
        {"requisition", all, 0, UINT32_MAX, nullptr, REQUISITION_SLIPS},
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
    bool usedDefaultRequisition = false;
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
                if (!fields[i].number && fields[i].currency == REQUISITION_SLIPS)
                {
                    loaded[index].price[REQUISITION_SLIPS] = defaultRequisition;
                    usedDefaultRequisition = true;
                    continue;
                }
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
    if (usedDefaultRequisition) snprintf(result, size, "Missing requisition settings use %u Req.", defaultRequisition);
    else if (size) result[0] = '\0';
    return 1;
}

extern "C" int SmrLoadUpgradeConfig(SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT], char* result, size_t size)
{
    const auto path = ConfigPath(L"destroyer-upgrades.json", result, size);
    return !path.empty() && ReadUpgradeConfig(path, tuning, result, size);
}

static bool ReadConfigNumber(const hfjson::Value& value, double min, double max, bool whole, double* number)
{
    char* end = nullptr;
    *number = strtod(value.text.c_str(), &end);
    return value.kind == hfjson::Value::Number && end != value.text.c_str() && !*end && std::isfinite(*number) &&
        *number >= min && *number <= max && (!whole || value.text.find_first_not_of("0123456789") == std::string::npos);
}

static int ReadHelldiverConfig(const std::wstring& path, SmrHelldiverTuning tuning[SMR_PERK_COUNT],
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
        snprintf(result, size, "Expected an object containing the plus, elite and super sections.");
        return 0;
    }
    constexpr const char* tiers[SMR_TIER_COUNT] = {"plus", "elite", "super"};
    constexpr const char* perks[SMR_PERK_COUNT] = {"", "primary_tradeoff", "secondary_tradeoff", "grenade_kit",
        "armored", "sprinter", "endurance", "auto_stim", "field_medic"};
    constexpr struct { const char* name; size_t currency; } currencies[] = {
        {"requisition", REQUISITION_SLIPS}, {"common_samples", COMMON_SAMPLES}, {"rare_samples", RARE_SAMPLES},
        {"super_samples", SUPER_SAMPLES}, {"forge_credits", FORGE_CREDITS},
    };
    using P = SmrHelldiverPerk;
    constexpr struct {
        const char* name;
        unsigned perks;
        double min, max;
        double SmrHelldiverTuning::* number;
    } fields[] = {
        {"primary_ammo_percent", (1u << SMR_PERK_PRIMARY_TRADEOFF) | (1u << SMR_PERK_SECONDARY_TRADEOFF) |
            (1u << SMR_PERK_GRENADE_KIT) | (1u << SMR_PERK_ARMORED),
            -100, 1000, &SmrHelldiverTuning::primary_ammo_percent},
        {"secondary_ammo_percent", (1u << SMR_PERK_PRIMARY_TRADEOFF) | (1u << SMR_PERK_SECONDARY_TRADEOFF) |
            (1u << SMR_PERK_GRENADE_KIT) | (1u << SMR_PERK_ARMORED),
            -100, 1000, &SmrHelldiverTuning::secondary_ammo_percent},
        {"throwables_percent", 1u << SMR_PERK_GRENADE_KIT, -100, 1000, &SmrHelldiverTuning::throwables_percent},
        {"armor_percent", (1u << SMR_PERK_GRENADE_KIT) | (1u << SMR_PERK_ARMORED) |
            (1u << SMR_PERK_AUTO_STIM) | (1u << SMR_PERK_FIELD_MEDIC), -90, 1000,
            &SmrHelldiverTuning::armor_percent},
        {"sprint_percent", (1u << SMR_PERK_SPRINTER) | (1u << SMR_PERK_ENDURANCE), -90, 1000,
            &SmrHelldiverTuning::sprint_percent},
        {"stamina_percent", (1u << SMR_PERK_SPRINTER) | (1u << SMR_PERK_ENDURANCE), -90, 1000,
            &SmrHelldiverTuning::stamina_percent},
        {"movement_percent", 1u << SMR_PERK_FIELD_MEDIC, -90, 1000, &SmrHelldiverTuning::movement_percent},
        {"health_percent", 1u << SMR_PERK_AUTO_STIM, 1, 99, &SmrHelldiverTuning::health_percent},
        {"cooldown_seconds", 1u << SMR_PERK_AUTO_STIM, 0, 86400, &SmrHelldiverTuning::cooldown_seconds},
    };
    SmrHelldiverTuning loaded[SMR_PERK_COUNT] = {};
    unsigned seenTiers = 0;
    bool usedDefaultSuperArmor = false;
    for (const auto& section : document.members)
    {
        unsigned tier = 0;
        while (tier < SMR_TIER_COUNT && section.first != tiers[tier]) ++tier;
        if (tier == SMR_TIER_COUNT || (seenTiers & (1u << tier)) || section.second.kind != Value::Object)
        {
            snprintf(result, size, "Unknown, duplicate or non-object Helldiver tier: %s.", section.first.c_str());
            return 0;
        }
        seenTiers |= 1u << tier;
        unsigned price[CURRENCY_COUNT] = {}, seenCurrencies = 0, seenPerks = 0;
        for (const auto& entry : section.second.members)
        {
            size_t c = 0;
            while (c < sizeof currencies / sizeof currencies[0] && entry.first != currencies[c].name) ++c;
            if (c < sizeof currencies / sizeof currencies[0])
            {
                double number;
                if (seenCurrencies & (1u << c))
                {
                    snprintf(result, size, "Duplicate setting: %s.%s.", tiers[tier], entry.first.c_str());
                    return 0;
                }
                if (!ReadConfigNumber(entry.second, 0, UINT32_MAX, true, &number))
                {
                    snprintf(result, size, "%s.%s must be a whole number from 0 to %u.", tiers[tier],
                        entry.first.c_str(), UINT32_MAX);
                    return 0;
                }
                seenCurrencies |= 1u << c;
                price[currencies[c].currency] = static_cast<unsigned>(number);
                continue;
            }
            unsigned perk = 1;
            while (perk < SMR_PERK_COUNT && entry.first != perks[perk]) ++perk;
            if (perk == SMR_PERK_COUNT || static_cast<unsigned>(SmrPerkTier(static_cast<P>(perk))) != tier || (seenPerks & (1u << perk)) ||
                entry.second.kind != Value::Object)
            {
                snprintf(result, size, "Unknown, duplicate or non-object setting: %s.%s.", tiers[tier],
                    entry.first.c_str());
                return 0;
            }
            seenPerks |= 1u << perk;
            unsigned seen = 0;
            for (const auto& setting : entry.second.members)
            {
                size_t i = 0;
                while (i < sizeof fields / sizeof fields[0] && setting.first != fields[i].name) ++i;
                if (i == sizeof fields / sizeof fields[0] || !(fields[i].perks & (1u << perk)) || (seen & (1u << i)))
                {
                    snprintf(result, size, "Unknown or duplicate setting: %s.%s.%s.", tiers[tier], perks[perk],
                        setting.first.c_str());
                    return 0;
                }
                double number;
                if (!ReadConfigNumber(setting.second, fields[i].min, fields[i].max, false, &number))
                {
                    snprintf(result, size, "%s.%s.%s must be a number from %.0f to %.0f.", tiers[tier], perks[perk],
                        fields[i].name, fields[i].min, fields[i].max);
                    return 0;
                }
                loaded[perk].*fields[i].number = number;
                seen |= 1u << i;
            }
            for (size_t i = 0; i < sizeof fields / sizeof fields[0]; ++i)
                if ((fields[i].perks & (1u << perk)) && !(seen & (1u << i)))
                {
                    if (fields[i].number == &SmrHelldiverTuning::armor_percent &&
                        (perk == SMR_PERK_AUTO_STIM || perk == SMR_PERK_FIELD_MEDIC))
                    {
                        loaded[perk].armor_percent = -10;
                        usedDefaultSuperArmor = true;
                        continue;
                    }
                    snprintf(result, size, "Missing setting: %s.%s.%s.", tiers[tier], perks[perk], fields[i].name);
                    return 0;
                }
        }
        if (!seenCurrencies)
        {
            snprintf(result, size, "Missing price for Helldiver tier: %s.", tiers[tier]);
            return 0;
        }
        for (unsigned perk = 1; perk < SMR_PERK_COUNT; ++perk)
        {
            if (static_cast<unsigned>(SmrPerkTier(static_cast<P>(perk))) != tier) continue;
            if (!(seenPerks & (1u << perk)))
            {
                snprintf(result, size, "Missing setting: %s.%s.", tiers[tier], perks[perk]);
                return 0;
            }
            memcpy(loaded[perk].price, price, sizeof price);
        }
    }
    for (unsigned tier = 0; tier < SMR_TIER_COUNT; ++tier)
        if (!(seenTiers & (1u << tier)))
        {
            snprintf(result, size, "Missing Helldiver tier: %s.", tiers[tier]);
            return 0;
        }
    for (unsigned i = 0; i < SMR_PERK_COUNT; ++i) tuning[i] = loaded[i];
        if (usedDefaultSuperArmor) snprintf(result, size, "Missing super armor settings use -10%%.");
        else if (size) result[0] = '\0';
    return 1;
}

extern "C" int SmrLoadHelldiverConfig(SmrHelldiverTuning tuning[SMR_PERK_COUNT], char* result, size_t size)
{
    const auto path = ConfigPath(L"helldiver-upgrades.json", result, size);
    return !path.empty() && ReadHelldiverConfig(path, tuning, result, size);
}

static bool EquipmentObject(const hfjson::Value& value, const std::string& path, std::string& why)
{
    if (value.kind != hfjson::Value::Object || value.members.empty())
    {
        why = "Expected a non-empty object: " + path;
        return false;
    }
    for (size_t i = 0; i < value.members.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (value.members[i].first == value.members[j].first)
            {
                why = "Duplicate setting: " + path + "." + value.members[i].first;
                return false;
            }
    return true;
}

using EquipmentValues = std::vector<std::pair<double*, double>>;

static bool ReadEquipmentChoice(const hfjson::Value& object, const SmrEquipmentChoice& choice,
    const std::string& path, EquipmentValues& values, std::string& why)
{
    if (!EquipmentObject(object, path, why)) return false;
    for (size_t i = 0; i < choice.edit_count; ++i)
    {
        auto& edit = choice.edits[i];
        const auto* value = object.Find(edit.key);
        const bool whole = edit.field.type != HF_TYPE_F32;
        const double max = whole ?
            (edit.field.type == HF_TYPE_I32 ? static_cast<double>(INT32_MAX) : static_cast<double>(UINT32_MAX)) : FLT_MAX;
        double number;
        if (!value || !ReadConfigNumber(*value, 0, max, whole, &number))
        {
            why = "Missing or invalid " + path + "." + edit.key + " (expected a non-negative " +
                (whole ? "whole number" : "number") + " within the field's range).";
            return false;
        }
        values.emplace_back(&edit.value, number);
    }
    if (object.members.size() != choice.edit_count)
    {
        why = "Unknown setting in " + path;
        return false;
    }
    return true;
}

static bool ReadEquipmentTier(const hfjson::Value& object, const SmrEquipmentGroup& group,
    const std::string& path, std::array<unsigned, CURRENCY_COUNT>& price, EquipmentValues& values, std::string& why)
{
    if (!EquipmentObject(object, path, why)) return false;
    constexpr const char* currencies[CURRENCY_COUNT] = {
        "requisition", "common_samples", "rare_samples", "super_samples", "forge_credits"
    };
    bool priced = false;
    size_t choices = 0;
    for (const auto& member : object.members)
    {
        size_t c = 0;
        while (c < CURRENCY_COUNT && member.first != currencies[c]) ++c;
        if (c < CURRENCY_COUNT)
        {
            double number;
            if (!ReadConfigNumber(member.second, 0, UINT32_MAX, true, &number))
            {
                why = "Invalid price: " + path + "." + member.first + " (expected a whole number from 0 to 4294967295).";
                return false;
            }
            price[c] = static_cast<unsigned>(number);
            priced = true;
            continue;
        }
        size_t i = 0;
        while (i < group.count && member.first != group.choices[i].title) ++i;
        if (i == group.count)
        {
            why = "Unknown setting: " + path + "." + member.first;
            return false;
        }
        if (!ReadEquipmentChoice(member.second, group.choices[i], path + "." + member.first, values, why)) return false;
        ++choices;
    }
    if (!priced || choices != group.count)
    {
        why = "Missing price or upgrade choice in " + path;
        return false;
    }
    return true;
}

static int ReadEquipmentConfig(const std::wstring& path, SmrEquipmentGroup* groups, size_t count,
    char* result, size_t size)
{
    using namespace hfjson;
    Value document;
    std::string text, why;
    constexpr const char* tiers[SMR_TIER_COUNT] = {"plus", "elite", "super"};
    std::vector<std::array<unsigned, CURRENCY_COUNT>> prices(count);
    std::vector<bool> seen(count);
    EquipmentValues values;
    if (!ReadFile(path, &text, &why) || !Parse(text, &document, &why) ||
        !EquipmentObject(document, "equipment", why)) goto failed;
    for (const auto& weapon : document.members)
    {
        if (!EquipmentObject(weapon.second, weapon.first, why)) goto failed;
        for (const auto& tier : weapon.second.members)
        {
            size_t i = 0;
            while (i < count && (weapon.first != groups[i].weapon || tier.first != tiers[groups[i].tier])) ++i;
            const std::string name = weapon.first + "." + tier.first;
            if (i == count)
            {
                why = "Unknown equipment tier: " + name;
                goto failed;
            }
            if (!ReadEquipmentTier(tier.second, groups[i], name, prices[i], values, why)) goto failed;
            seen[i] = true;
        }
    }
    for (size_t i = 0; i < count; ++i)
        if (!seen[i])
        {
            why = std::string("Missing equipment tier: ") + groups[i].weapon + "." + tiers[groups[i].tier];
            goto failed;
        }
    for (const auto& value : values) *value.first = value.second;
    for (size_t i = 0; i < count; ++i) memcpy(groups[i].price, prices[i].data(), sizeof groups[i].price);
    if (size) result[0] = '\0';
    return 1;
failed:
    snprintf(result, size, "%s", why.c_str());
    return 0;
}

extern "C" int SmrLoadEquipmentConfig(SmrEquipmentGroup* groups, size_t count, char* result, size_t size)
{
    const auto path = ConfigPath(L"equipment-upgrades.json", result, size);
    return !path.empty() && ReadEquipmentConfig(path, groups, count, result, size);
}
