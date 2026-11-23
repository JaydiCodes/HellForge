#include "json.cpp"
#include "../src/config/prices.cpp"
#include "../src/config/upgrade_config.cpp"
#include "../src/access/access_items.h"
#include <algorithm>

#define CHECK(test) do { if (!(test)) { printf("FAIL line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

static void TestUpgradeConfig(const std::wstring& shippedPath, const std::wstring& tempPath)
{
    using namespace hfjson;
    char result[256] = {};
    SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT] = {};
    CHECK(ReadUpgradeConfig(shippedPath, tuning, result, sizeof result));
    const unsigned shippedPrice[CURRENCY_COUNT] = {0, 10, 5, 1, 50};
    for (int upgrade = SMR_UPGRADE_ORBITAL; upgrade < SMR_UPGRADE_COUNT; ++upgrade)
        CHECK(!memcmp(tuning[upgrade].price, shippedPrice, sizeof shippedPrice));
    CHECK(tuning[SMR_UPGRADE_ORBITAL].cooldown_percent == -66);
    CHECK(tuning[SMR_UPGRADE_EAGLE].rearm_seconds == 30 && tuning[SMR_UPGRADE_EAGLE].call_in_extra_seconds == 2 &&
        tuning[SMR_UPGRADE_EAGLE].cooldown_seconds == 20);
    CHECK(tuning[SMR_UPGRADE_DEFENSIVE].cooldown_percent == -66 && tuning[SMR_UPGRADE_SUPPORT].cooldown_percent == -66);
    CHECK(tuning[SMR_UPGRADE_DEFENSIVE].other_cooldown_percent == 100 && tuning[SMR_UPGRADE_SUPPORT].other_cooldown_percent == 100);
    CHECK(tuning[SMR_UPGRADE_EXTRA_SLOT].other_cooldown_percent == 10);
    std::string text, why;
    Value valid;
    CHECK(ReadFile(shippedPath, &text) && Parse(text, &valid, &why));
    valid.At("orbital").At("cooldown_percent") = Value::MakeNumber(-72.5, false);
    valid.At("eagle").At("forge_credits") = Value::MakeNumber(7, true);
    valid.At("eagle").At("common_samples") = Value::MakeNumber(0, true);
    valid.At("eagle").At("super_samples") = Value::MakeNumber(3, true);
    valid.At("support").At("cooldown_percent") = Value::MakeNumber(-62.5, false);
    CHECK(WriteAtomically(tempPath, Write(valid)));
    CHECK(ReadUpgradeConfig(tempPath, tuning, result, sizeof result));
    const unsigned eaglePrice[CURRENCY_COUNT] = {0, 0, 5, 3, 7};
    CHECK(tuning[SMR_UPGRADE_ORBITAL].cooldown_percent == -72.5 &&
        !memcmp(tuning[SMR_UPGRADE_EAGLE].price, eaglePrice, sizeof eaglePrice));
    CHECK(tuning[SMR_UPGRADE_SUPPORT].cooldown_percent == -62.5 && tuning[SMR_UPGRADE_DEFENSIVE].cooldown_percent == -66);
    const struct { const char* field; const char* value; } badPrices[] = {
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
    valid.Erase("extra_slot");
    CHECK(WriteAtomically(tempPath, Write(valid)));
    CHECK(ReadUpgradeConfig(tempPath, tuning, result, sizeof result));
    CHECK(tuning[SMR_UPGRADE_EXTRA_SLOT].price[FORGE_CREDITS] == 0 && tuning[SMR_UPGRADE_SUPPORT].cooldown_percent == -62.5);
    CHECK(DeleteFileW(tempPath.c_str()));
    CHECK(!ReadUpgradeConfig(tempPath, tuning, result, sizeof result) && result[0]);
    puts("PASS destroyer config: shipped defaults, custom values, strict validation, and atomic failure.");
}

int main(int argc, char** argv)
{
    using namespace hfjson;
    CHECK(argc == 3);
    const char* names[PRICE_COUNT];
    unsigned prices[PRICE_COUNT] = {};
    for (size_t i = 0; i < PRICE_COUNT; ++i) names[i] = kLoadoutPrices[i].name;
    char result[256] = {};
    CHECK(ReadPrices(Wide(argv[1]), names, prices, PRICE_COUNT, result, sizeof result));
    Value shipped;
    std::string shippedText, shippedError;
    CHECK(ReadFile(Wide(argv[1]), &shippedText) && Parse(shippedText, &shipped, &shippedError));
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
    const unsigned maximum = std::numeric_limits<unsigned>::max() / PRICE_COUNT;
    for (const char* invalid : {"0", "-1", "1.5", "1e3", "\"500\"", "true", "null", "4294967296",
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
    TestUpgradeConfig(Wide(argv[2]), path);
}
