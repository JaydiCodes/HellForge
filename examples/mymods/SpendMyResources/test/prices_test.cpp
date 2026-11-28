#include "json.cpp"
#include "../src/config/prices.cpp"
#include "../src/config/upgrade_config.cpp"
#include "../src/access/access_items.h"
#include "../src/gameplay/equipment_catalog.h"
#include <algorithm>

#define CHECK(test) do { if (!(test)) { printf("FAIL line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

static void TestUpgradeConfig(const std::wstring& shippedPath, const std::wstring& tempPath)
{
    using namespace hfjson;
    char result[256] = {};
    SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT] = {};
    CHECK(ReadUpgradeConfig(shippedPath, tuning, result, sizeof result));
    const unsigned shippedPrice[CURRENCY_COUNT] = {2000, 15, 10, 5, 50};
    for (int upgrade = SMR_UPGRADE_ORBITAL; upgrade < SMR_UPGRADE_COUNT; ++upgrade)
        CHECK(!memcmp(tuning[upgrade].price, shippedPrice, sizeof shippedPrice));
    CHECK(tuning[SMR_UPGRADE_ORBITAL].cooldown_percent == -66);
    CHECK(tuning[SMR_UPGRADE_EAGLE].rearm_seconds == 30 && tuning[SMR_UPGRADE_EAGLE].call_in_extra_seconds == 2 &&
        tuning[SMR_UPGRADE_EAGLE].cooldown_seconds == 20);
    CHECK(tuning[SMR_UPGRADE_DEFENSIVE].cooldown_percent == -66 && tuning[SMR_UPGRADE_SUPPORT].cooldown_percent == -66);
    CHECK(tuning[SMR_UPGRADE_DEFENSIVE].other_cooldown_percent == 50 && tuning[SMR_UPGRADE_SUPPORT].other_cooldown_percent == 50);
    CHECK(tuning[SMR_UPGRADE_EXTRA_SLOT].other_cooldown_percent == 10);
    std::string text, why;
    Value valid;
    CHECK(ReadFile(shippedPath, &text) && Parse(text, &valid, &why));
    valid.At("orbital").At("cooldown_percent") = Value::MakeNumber(-72.5, false);
    valid.At("eagle").At("requisition") = Value::MakeNumber(1375, true);
    valid.At("eagle").At("forge_credits") = Value::MakeNumber(7, true);
    valid.At("eagle").At("common_samples") = Value::MakeNumber(0, true);
    valid.At("eagle").At("super_samples") = Value::MakeNumber(3, true);
    valid.At("support").At("cooldown_percent") = Value::MakeNumber(-62.5, false);
    valid.At("support").At("requisition") = Value::MakeNumber(0, true);
    CHECK(WriteAtomically(tempPath, Write(valid)));
    CHECK(ReadUpgradeConfig(tempPath, tuning, result, sizeof result));
    const unsigned eaglePrice[CURRENCY_COUNT] = {1375, 0, 10, 3, 7};
    CHECK(tuning[SMR_UPGRADE_ORBITAL].cooldown_percent == -72.5 &&
        !memcmp(tuning[SMR_UPGRADE_EAGLE].price, eaglePrice, sizeof eaglePrice));
    CHECK(tuning[SMR_UPGRADE_SUPPORT].cooldown_percent == -62.5 && tuning[SMR_UPGRADE_DEFENSIVE].cooldown_percent == -66);
    CHECK(tuning[SMR_UPGRADE_SUPPORT].price[REQUISITION_SLIPS] == 0);
    const struct { const char* field; const char* value; } badPrices[] = {
        {"requisition", "-1"}, {"requisition", "1.5"}, {"requisition", "\"2000\""}, {"requisition", "4294967296"},
        {"forge_credits", "0"}, {"forge_credits", "-1"}, {"forge_credits", "1.5"}, {"forge_credits", "1e3"},
        {"forge_credits", "\"10\""}, {"forge_credits", "4294967296"}, {"forge_credits", "1e999"},
        {"common_samples", "-1"}, {"rare_samples", "2.5"}, {"super_samples", "\"1\""}, {"super_samples", "4294967296"},
    };
    for (const auto& bad : badPrices)
    {
        Value document = valid, value;
        CHECK(Parse(bad.value, &value, &why));
        document.At("support").At(bad.field) = value;
        CHECK(WriteAtomically(tempPath, Write(document)));
        CHECK(!ReadUpgradeConfig(tempPath, tuning, result, sizeof result) && result[0]);
        CHECK(tuning[SMR_UPGRADE_ORBITAL].cooldown_percent == -72.5 && tuning[SMR_UPGRADE_EAGLE].price[FORGE_CREDITS] == 7);
    }
    for (int i = 0; i < 12; ++i)
    {
        Value document = valid;
        if (i == 0) document.Erase("eagle");
        if (i == 1) document.At("support").Erase("cooldown_percent");
        if (i == 2) document.At("eagle").At("typo") = Value::MakeNumber(10, true);
        if (i == 3) document.At("eagle").members.emplace_back("forge_credits", Value::MakeNumber(10, true));
        if (i == 4) document.At("defensive").At("cooldown_percent") = Value::MakeNumber(-101, true);
        if (i == 5) document.At("defensive").At("uses") = Value::MakeNumber(10, true);
        if (i == 6) document.At("support").At("uses") = Value::MakeNumber(5, true);
        if (i == 7) document.At("defensive").At("cooldown_seconds") = Value::MakeNumber(30, true);
        if (i == 8) document.At("support").At("cooldown_seconds") = Value::MakeNumber(60, true);
        if (i == 9) document.At("extra_slot").Erase("forge_credits");
        if (i == 10) document.At("extra_slot").At("cooldown_percent") = Value::MakeNumber(-30, true);
        if (i == 11) document.At("orbital").Erase("rare_samples");
        CHECK(WriteAtomically(tempPath, Write(document)));
        CHECK(!ReadUpgradeConfig(tempPath, tuning, result, sizeof result) && result[0]);
        CHECK(tuning[SMR_UPGRADE_SUPPORT].cooldown_percent == -62.5 && tuning[SMR_UPGRADE_DEFENSIVE].cooldown_percent == -66);
    }
    valid.At("eagle").Erase("requisition");
    valid.Erase("extra_slot");
    CHECK(WriteAtomically(tempPath, Write(valid)));
    CHECK(ReadUpgradeConfig(tempPath, tuning, result, sizeof result) && strstr(result, "2000 Req"));
    CHECK(tuning[SMR_UPGRADE_EAGLE].price[REQUISITION_SLIPS] == 2000 &&
        tuning[SMR_UPGRADE_SUPPORT].price[REQUISITION_SLIPS] == 0);
    CHECK(tuning[SMR_UPGRADE_EXTRA_SLOT].price[FORGE_CREDITS] == 0 && tuning[SMR_UPGRADE_SUPPORT].cooldown_percent == -62.5);
    CHECK(DeleteFileW(tempPath.c_str()));
    CHECK(!ReadUpgradeConfig(tempPath, tuning, result, sizeof result) && result[0]);
    puts("PASS destroyer config: shipped defaults, custom values, strict validation, and atomic failure.");
}

static void TestHelldiverConfig(const std::wstring& shippedPath, const std::wstring& tempPath)
{
    using namespace hfjson;
    char result[256] = {};
    SmrHelldiverTuning tuning[SMR_PERK_COUNT] = {};
    const int loaded = ReadHelldiverConfig(shippedPath, tuning, result, sizeof result);
    if (!loaded) printf("Helldiver configuration rejected: %s\n", result);
    CHECK(loaded);
    CHECK(tuning[SMR_PERK_PRIMARY_TRADEOFF].price[COMMON_SAMPLES] == 15 && tuning[SMR_PERK_GRENADE_KIT].price[COMMON_SAMPLES] == 15 &&
        tuning[SMR_PERK_ARMORED].price[RARE_SAMPLES] == 10 && tuning[SMR_PERK_FIELD_MEDIC].price[SUPER_SAMPLES] == 5 &&
        !tuning[SMR_PERK_ARMORED].price[COMMON_SAMPLES] && !tuning[SMR_PERK_NONE].price[COMMON_SAMPLES]);
    CHECK(tuning[SMR_PERK_PRIMARY_TRADEOFF].primary_ammo_percent == -50 &&
        tuning[SMR_PERK_PRIMARY_TRADEOFF].secondary_ammo_percent == 66);
    CHECK(tuning[SMR_PERK_SECONDARY_TRADEOFF].primary_ammo_percent == 50 &&
        tuning[SMR_PERK_SECONDARY_TRADEOFF].secondary_ammo_percent == -66);
    CHECK(tuning[SMR_PERK_GRENADE_KIT].throwables_percent == 66 && tuning[SMR_PERK_GRENADE_KIT].primary_ammo_percent == -33 &&
        tuning[SMR_PERK_GRENADE_KIT].secondary_ammo_percent == -33);
    CHECK(tuning[SMR_PERK_GRENADE_KIT].armor_percent == 0 && tuning[SMR_PERK_ARMORED].armor_percent == 25 &&
        tuning[SMR_PERK_ARMORED].primary_ammo_percent == -10);
    CHECK(tuning[SMR_PERK_SPRINTER].sprint_percent == 15 && tuning[SMR_PERK_SPRINTER].stamina_percent == -25 &&
        tuning[SMR_PERK_ENDURANCE].stamina_percent == 25);
    CHECK(tuning[SMR_PERK_AUTO_STIM].health_percent == 20 && tuning[SMR_PERK_AUTO_STIM].cooldown_seconds == 30 &&
        tuning[SMR_PERK_AUTO_STIM].armor_percent == -20);
    CHECK(tuning[SMR_PERK_FIELD_MEDIC].movement_percent == -20 &&
        tuning[SMR_PERK_FIELD_MEDIC].armor_percent == -10 && tuning[SMR_PERK_FIELD_MEDIC].heal_per_second == 2.5);
    std::string text, why;
    Value valid;
    CHECK(ReadFile(shippedPath, &text) && Parse(text, &valid, &why));
    valid.At("elite").At("requisition") = Value::MakeNumber(1500, true);
    valid.At("super").At("auto_stim").At("health_percent") = Value::MakeNumber(12.5, false);
    CHECK(WriteAtomically(tempPath, Write(valid)));
    CHECK(ReadHelldiverConfig(tempPath, tuning, result, sizeof result));
    CHECK(tuning[SMR_PERK_SPRINTER].price[REQUISITION_SLIPS] == 1500 && tuning[SMR_PERK_SPRINTER].price[RARE_SAMPLES] == 10 &&
        tuning[SMR_PERK_AUTO_STIM].health_percent == 12.5);
    {
        Value document = valid;
        document.At("super").At("auto_stim").At("armor_percent") = Value::MakeNumber(-15.5, false);
        document.At("super").At("field_medic").At("armor_percent") = Value::MakeNumber(0, true);
        document.At("super").At("field_medic").At("heal_per_second") = Value::MakeNumber(2.5, false);
        CHECK(WriteAtomically(tempPath, Write(document)));
        CHECK(ReadHelldiverConfig(tempPath, tuning, result, sizeof result) && !result[0]);
        CHECK(tuning[SMR_PERK_AUTO_STIM].armor_percent == -15.5 && tuning[SMR_PERK_FIELD_MEDIC].armor_percent == 0);
        CHECK(tuning[SMR_PERK_FIELD_MEDIC].heal_per_second == 2.5);
    }
    for (const char* perk : {"auto_stim", "field_medic"})
    {
        Value document = valid;
        document.At("super").At(perk).Erase("armor_percent");
        CHECK(WriteAtomically(tempPath, Write(document)));
        CHECK(ReadHelldiverConfig(tempPath, tuning, result, sizeof result) && strstr(result, "-10%"));
        /* Only the erased perk falls back; the other keeps its shipped armor value. */
        CHECK(tuning[strcmp(perk, "auto_stim") ? SMR_PERK_FIELD_MEDIC : SMR_PERK_AUTO_STIM].armor_percent == -10);
    }
    {
        Value document = valid;
        document.At("super").At("field_medic").Erase("heal_per_second");
        CHECK(WriteAtomically(tempPath, Write(document)));
        CHECK(ReadHelldiverConfig(tempPath, tuning, result, sizeof result) && strstr(result, "heal_per_second"));
        CHECK(tuning[SMR_PERK_FIELD_MEDIC].heal_per_second == 1);
    }
    for (int i = 0; i < 20; ++i)
    {
        Value document = valid;
        if (i == 0) document.Erase("super");
        if (i == 1) document.At("plus").Erase("grenade_kit");
        if (i == 2) document.At("elite").At("armored").Erase("armor_percent");
        if (i == 3) document.At("elite").At("armored").At("sprint_percent") = Value::MakeNumber(5, true);
        if (i == 4) document.At("plus").At("armored") = document.At("elite").At("armored");
        if (i == 5) document.At("plus").Erase("common_samples");
        if (i == 6) document.At("super").At("super_samples") = Value::MakeNumber(1.5, false);
        if (i == 7) document.At("super").At("auto_stim").At("health_percent") = Value::MakeNumber(100, true);
        if (i == 8) document.At("elite").At("sprinter").At("sprint_percent") = Value::MakeNumber(-95, true);
        if (i == 9) document.At("typo") = document.At("plus");
        if (i == 10) document.At("elite").members.emplace_back("rare_samples", Value::MakeNumber(1, true));
        if (i == 11) document.At("super").At("field_medic") = Value::MakeNumber(2, true);
        if (i == 12) document.At("plus").At("primary_tradeoff").Erase("primary_ammo_percent");
        if (i == 13) document.At("plus").At("secondary_tradeoff").Erase("secondary_ammo_percent");
        if (i == 14) document.At("elite").At("armored").At("damage_taken_percent") = Value::MakeNumber(-20, true);
        if (i == 15) document.At("super").At("auto_stim").At("stim_heal_percent") = Value::MakeNumber(-33, true);
        if (i == 16) document.At("super").At("auto_stim").At("armor_percent") = Value::MakeNumber(-91, true);
        if (i == 17) document.At("super").At("field_medic").At("armor_percent") = Value::MakeNumber(-91, true);
        if (i == 18) document.At("super").At("field_medic").At("injury_seconds") = Value::MakeNumber(0.5, false);
        if (i == 19) document.At("super").At("field_medic").At("heal_per_second") = Value::MakeNumber(-1, true);
        CHECK(WriteAtomically(tempPath, Write(document)));
        CHECK(!ReadHelldiverConfig(tempPath, tuning, result, sizeof result) && result[0]);
        CHECK(tuning[SMR_PERK_AUTO_STIM].health_percent == 12.5 && tuning[SMR_PERK_SPRINTER].price[REQUISITION_SLIPS] == 1500);
    }
    CHECK(DeleteFileW(tempPath.c_str()));
    CHECK(!ReadHelldiverConfig(tempPath, tuning, result, sizeof result) && result[0]);
    puts("PASS helldiver config: shipped tiers and perks, custom values, strict validation, and atomic failure.");
}

static const struct { uint64_t resource; const char* name; } kEnemies[] = {
#include "smr_enemies.inc"
};
static constexpr size_t kEnemyCount = sizeof kEnemies / sizeof kEnemies[0];

static void TestKillRewards(const std::wstring& shippedPath, const std::wstring& tempPath)
{
    using namespace hfjson;
    const char* names[kEnemyCount];
    for (size_t i = 0; i < kEnemyCount; ++i) names[i] = kEnemies[i].name;
    std::vector<unsigned> values(kEnemyCount, 7);
    char result[256] = {};
    CHECK(ReadKillRewards(shippedPath, names, values.data(), kEnemyCount, result, sizeof result));
    Value shipped;
    std::string text, why;
    CHECK(ReadFile(shippedPath, &text) && Parse(text, &shipped, &why) && !shipped.members.empty());
    size_t listed = 0;
    for (size_t i = 0; i < kEnemyCount; ++i)
    {
        CHECK(!strstr(names[i], "SEAF"));
        const Value* value = shipped.Find(names[i]);
        listed += value != nullptr;
        CHECK(values[i] == (value ? shipped.GetNumber(names[i]) : SMR_KILL_DEFAULT_VALUE));
    }
    CHECK(listed == shipped.members.size());

    Value valid;
    valid.kind = Value::Object;
    valid.At(names[0]) = Value::MakeNumber(0, true);
    valid.At(names[1]) = Value::MakeNumber(SMR_KILL_MAX_VALUE, true);
    CHECK(WriteAtomically(tempPath, Write(valid)));
    CHECK(ReadKillRewards(tempPath, names, values.data(), kEnemyCount, result, sizeof result));
    CHECK(values[0] == 0 && values[1] == SMR_KILL_MAX_VALUE && values[2] == SMR_KILL_DEFAULT_VALUE);
    for (int test = 0; test < 10; ++test)
    {
        Value document = valid, value;
        const char* invalid[] = {"-1", "1.5", "1e3", "\"5\"", "true", "1000001", "99999999999"};
        if (test < 7)
        {
            CHECK(Parse(invalid[test], &value, &why));
            document.At(names[2]) = value;
        }
        if (test == 7) document.At("Unknown enemy") = Value::MakeNumber(5, true);
        if (test == 8) document.members.emplace_back(names[0], Value::MakeNumber(5, true));
        if (test == 9) document.kind = Value::Array;
        CHECK(WriteAtomically(tempPath, Write(document)));
        CHECK(!ReadKillRewards(tempPath, names, values.data(), kEnemyCount, result, sizeof result) && result[0]);
        CHECK(values[0] == 0 && values[1] == SMR_KILL_MAX_VALUE && values[2] == SMR_KILL_DEFAULT_VALUE);
    }
    puts("PASS kill rewards config: shipped enemy values, defaults, zero and maximum, strict validation.");
}

static std::vector<double> EquipmentSnapshot()
{
    std::vector<double> values;
    for (const auto& group : kEquipmentGroups)
    {
        for (unsigned price : group.price) values.push_back(price);
        for (size_t c = 0; c < group.count; ++c)
            for (size_t e = 0; e < group.choices[c].edit_count; ++e)
                values.push_back(group.choices[c].edits[e].value);
    }
    return values;
}

static void TestEquipmentConfig(const std::wstring& shippedPath, const std::wstring& tempPath)
{
    using namespace hfjson;
    char result[256] = {};
    CHECK(ReadEquipmentConfig(shippedPath, kEquipmentGroups, EQUIPMENT_GROUP_COUNT, result, sizeof result));
    CHECK(kHe[0].value == 100 && kAphe[1].value == 750 && kApfsds[2].value == 6 &&
        kFdi[1].value == 450 && kApi[0].value == 450);
    Value valid;
    std::string text, why;
    CHECK(ReadFile(shippedPath, &text) && Parse(text, &valid, &why));
    for (const auto& group : kEquipmentGroups)
    {
        CHECK(group.price[SUPER_SAMPLES] == 5 && !group.price[REQUISITION_SLIPS]);
        auto& tier = valid.At(group.weapon).At("super");
        tier.At("requisition") = Value::MakeNumber(250, true);
        tier.At("common_samples") = Value::MakeNumber(3, true);
        tier.At("rare_samples") = Value::MakeNumber(4, true);
        tier.At("super_samples") = Value::MakeNumber(7, true);
        tier.At("forge_credits") = Value::MakeNumber(2, true);
        for (size_t c = 0; c < group.count; ++c)
            for (size_t e = 0; e < group.choices[c].edit_count; ++e)
            {
                const auto& edit = group.choices[c].edits[e];
                const bool whole = edit.field.type != HF_TYPE_F32;
                tier.At(group.choices[c].title).At(edit.key) = Value::MakeNumber(whole ? 17 : 2.75, whole);
            }
        std::reverse(tier.members.begin(), tier.members.end());
    }
    std::reverse(valid.members.begin(), valid.members.end());
    CHECK(WriteAtomically(tempPath, Write(valid)));
    CHECK(ReadEquipmentConfig(tempPath, kEquipmentGroups, EQUIPMENT_GROUP_COUNT, result, sizeof result) && !result[0]);
    for (const auto& group : kEquipmentGroups)
    {
        const unsigned expected[] = {250, 3, 4, 7, 2};
        CHECK(!memcmp(group.price, expected, sizeof expected));
        for (size_t c = 0; c < group.count; ++c)
            for (size_t e = 0; e < group.choices[c].edit_count; ++e)
            {
                const auto& edit = group.choices[c].edits[e];
                CHECK(edit.value == (edit.field.type == HF_TYPE_F32 ? 2.75 : 17));
            }
    }
    const auto before = EquipmentSnapshot();
    for (int i = 0; i < 14; ++i)
    {
        Value document = valid;
        auto& tier = document.At("R-36 Eruptor").At("super");
        auto& choice = tier.At("HE");
        if (i == 0) document.Erase("R-4 Hyena");
        if (i == 1) document.At("Unknown weapon") = document.At("R-4 Hyena");
        if (i == 2) document.members.emplace_back("R-36 Eruptor", document.At("R-36 Eruptor"));
        if (i == 3) tier.Erase("HE");
        if (i == 4) choice.Erase("damage.standard_damage");
        if (i == 5) choice.At("unknown") = Value::MakeNumber(1, true);
        if (i == 6) choice.members.emplace_back("damage.standard_damage", Value::MakeNumber(1, true));
        if (i == 7) tier.members.emplace_back("HE", choice);
        if (i == 8) document.At("R-36 Eruptor").At("elite") = tier;
        if (i == 9) tier.At("super_samples") = Value::MakeNumber(-1, true);
        if (i == 10) choice.At("damage.standard_damage") = Value::MakeNumber(2147483648.0, true);
        if (i == 11) choice.At("damage.ap_direct") = Value::MakeNumber(2.5, false);
        if (i == 12) choice.kind = Value::Array;
        if (i == 13) choice.At("damage.ap_direct") = Value::MakeNumber(4294967296.0, true);
        CHECK(WriteAtomically(tempPath, Write(document)));
        const int accepted = ReadEquipmentConfig(tempPath, kEquipmentGroups, EQUIPMENT_GROUP_COUNT, result, sizeof result);
        if (accepted) printf("Accepted invalid equipment case %d\n", i);
        CHECK(!accepted && result[0]);
        CHECK(EquipmentSnapshot() == before);
    }
    for (const char* bad : {"-1", "\"2\"", "null", "true", "1e999", "1e40"})
    {
        Value document = valid, value;
        CHECK(Parse(bad, &value, &why));
        document.At("R-4 Hyena").At("super").At("super_samples") = Value::MakeNumber(0, true);
        document.At("R-36 Eruptor").At("super").At("HE").At("explosion.inner_radius") = value;
        CHECK(WriteAtomically(tempPath, Write(document)));
        CHECK(!ReadEquipmentConfig(tempPath, kEquipmentGroups, EQUIPMENT_GROUP_COUNT, result, sizeof result) && result[0]);
        CHECK(EquipmentSnapshot() == before);
    }
    CHECK(WriteAtomically(tempPath, "{"));
    CHECK(!ReadEquipmentConfig(tempPath, kEquipmentGroups, EQUIPMENT_GROUP_COUNT, result, sizeof result) && result[0]);
    CHECK(DeleteFileW(tempPath.c_str()));
    CHECK(!ReadEquipmentConfig(tempPath, kEquipmentGroups, EQUIPMENT_GROUP_COUNT, result, sizeof result) && result[0]);
    CHECK(EquipmentSnapshot() == before);
    puts("PASS equipment config: shipped values, custom prices and every stat, strict validation, atomic failure.");
}

int main(int argc, char** argv)
{
    using namespace hfjson;
    const bool pricesOnly = argc == 3 && !strcmp(argv[1], "--prices");
    const bool equipmentOnly = argc == 3 && !strcmp(argv[1], "--equipment");
    CHECK(argc == 6 || pricesOnly || equipmentOnly);
    if (!pricesOnly)
    {
        wchar_t equipmentTemp[MAX_PATH], equipmentFile[MAX_PATH];
        CHECK(GetTempPathW(MAX_PATH, equipmentTemp) && GetTempFileNameW(equipmentTemp, L"smr", 0, equipmentFile));
        TestEquipmentConfig(Wide(argv[equipmentOnly ? 2 : 5]), equipmentFile);
        if (equipmentOnly) return 0;
    }
    const char* names[PRICE_COUNT];
    unsigned prices[PRICE_COUNT] = {};
    for (size_t i = 0; i < PRICE_COUNT; ++i)
    {
        CHECK(kLoadoutPrices[i].name);
        names[i] = kLoadoutPrices[i].name;
    }
    char result[256] = {};
    const std::wstring pricePath = Wide(argv[pricesOnly ? 2 : 1]);
    CHECK(ReadPrices(pricePath, names, prices, PRICE_COUNT, result, sizeof result));
    Value shipped;
    std::string shippedText, shippedError;
    CHECK(ReadFile(pricePath, &shippedText) && Parse(shippedText, &shipped, &shippedError));
    for (size_t i = 0; i < PRICE_COUNT; ++i) CHECK(prices[i] == shipped.GetNumber(names[i]));

    wchar_t temp[MAX_PATH], file[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH, temp) && GetTempFileNameW(temp, L"smr", 0, file));
    const std::wstring path = file;
    Value valid;
    valid.kind = Value::Object;
    for (size_t i = 0; i < PRICE_COUNT; ++i) valid.At(names[i]) = Value::MakeNumber(1375 + i, true);
    std::reverse(valid.members.begin(), valid.members.end());
    CHECK(WriteAtomically(path, Write(valid)));
    CHECK(ReadPrices(path, names, prices, PRICE_COUNT, result, sizeof result));
    for (size_t i = 0; i < PRICE_COUNT; ++i) CHECK(prices[i] == 1375 + i);
    valid.At(names[0]) = Value::MakeNumber(0, true);
    CHECK(WriteAtomically(path, Write(valid)));
    CHECK(ReadPrices(path, names, prices, PRICE_COUNT, result, sizeof result));
    CHECK(prices[0] == 0);
    Value duplicateZero = valid;
    duplicateZero.members.emplace_back(names[0], Value::MakeNumber(500, true));
    CHECK(WriteAtomically(path, Write(duplicateZero)));
    CHECK(!ReadPrices(path, names, prices, PRICE_COUNT, result, sizeof result));
    CHECK(strstr(result, "Duplicate item:") && prices[0] == 0);
    valid.At(names[0]) = Value::MakeNumber(1375, true);
    CHECK(WriteAtomically(path, Write(valid)));
    CHECK(ReadPrices(path, names, prices, PRICE_COUNT, result, sizeof result));
    const unsigned maximum = std::numeric_limits<unsigned>::max() / PRICE_COUNT;
    for (const char* invalid : {"-1", "1.5", "1e3", "\"500\"", "true", "null", "4294967296",
            "999999999999999999999999999999"})
    {
        Value document = valid, value;
        std::string why;
        CHECK(Parse(invalid, &value, &why));
        document.At(names[0]) = value;
        CHECK(WriteAtomically(path, Write(document)));
        CHECK(!ReadPrices(path, names, prices, PRICE_COUNT, result, sizeof result) && result[0]);
        for (size_t i = 0; i < PRICE_COUNT; ++i) CHECK(prices[i] == 1375 + i);
    }
    for (int test = 0; test < 6; ++test)
    {
        Value document = valid;
        if (test == 0) document.Erase(names[0]);
        if (test == 1) document.At("Unknown item") = Value::MakeNumber(500, true);
        if (test == 2) document.members.emplace_back(names[0], Value::MakeNumber(500, true));
        if (test == 3) document.At(names[0]) = Value::MakeNumber(maximum + 1, true);
        if (test == 4) document.kind = Value::Array;
        CHECK(WriteAtomically(path, test == 5 ? "broken json" : Write(document)));
        CHECK(!ReadPrices(path, names, prices, PRICE_COUNT, result, sizeof result) && result[0]);
        for (size_t i = 0; i < PRICE_COUNT; ++i) CHECK(prices[i] == 1375 + i);
    }
    for (size_t i = 0; i < PRICE_COUNT; ++i) valid.At(names[i]) = Value::MakeNumber(maximum, true);
    CHECK(WriteAtomically(path, Write(valid)));
    CHECK(ReadPrices(path, names, prices, PRICE_COUNT, result, sizeof result));
    unsigned long long total = 0;
    for (unsigned price : prices) total += price;
    CHECK(total <= std::numeric_limits<unsigned>::max());
    CHECK(DeleteFileW(path.c_str()));
    CHECK(!ReadPrices(path, names, prices, PRICE_COUNT, result, sizeof result) && result[0]);
    puts("PASS price list: shipped roster, custom and reordered prices, overflow limits, atomic validation, missing/invalid files and entries.");
    if (pricesOnly) return 0;
    TestUpgradeConfig(Wide(argv[2]), path);
    TestKillRewards(Wide(argv[4]), path);
    TestHelldiverConfig(Wide(argv[3]), path);
}
