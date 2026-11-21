#include "json.cpp"
#include "../src/prices.cpp"
#include "../src/upgrade_config.cpp"
#include "../src/access_items.h"
#include <algorithm>

#define CHECK(test) do { if (!(test)) { printf("FAIL line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

static void TestUpgradeConfig(const std::wstring& shippedPath, const std::wstring& tempPath)
{
    using namespace hfjson;
    char result[256] = {};
    SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT] = {};
    CHECK(ReadUpgradeConfig(shippedPath, tuning, result, sizeof result));
    CHECK(tuning[SMR_UPGRADE_ORBITAL].cooldown_percent == -66 && tuning[SMR_UPGRADE_ORBITAL].forge_credits == 30);
    CHECK(tuning[SMR_UPGRADE_EAGLE].rearm_seconds == 30 && tuning[SMR_UPGRADE_EAGLE].call_in_extra_seconds == 2 &&
        tuning[SMR_UPGRADE_EAGLE].cooldown_seconds == 20);
    CHECK(tuning[SMR_UPGRADE_DEFENSIVE].cooldown_seconds == 0 && tuning[SMR_UPGRADE_SUPPORT].cooldown_seconds == 0);
    std::string text, why;
    Value valid;
    CHECK(ReadFile(shippedPath, &text) && Parse(text, &valid, &why));
    valid.At("orbital").At("cooldown_percent") = Value::MakeNumber(-72.5, false);
    valid.At("eagle").At("forge_credits") = Value::MakeNumber(7, true);
    valid.At("support").At("cooldown_seconds") = Value::MakeNumber(2.5, false);
    CHECK(WriteAtomically(tempPath, Write(valid)));
    CHECK(ReadUpgradeConfig(tempPath, tuning, result, sizeof result));
    CHECK(tuning[SMR_UPGRADE_ORBITAL].cooldown_percent == -72.5 && tuning[SMR_UPGRADE_EAGLE].forge_credits == 7);
    CHECK(tuning[SMR_UPGRADE_SUPPORT].cooldown_seconds == 2.5);
    for (const char* bad : {"0", "-1", "1.5", "1e3", "\"10\"", "4294967296", "1e999"})
    {
        Value document = valid, value;
        CHECK(Parse(bad, &value, &why));
        document.At("support").At("forge_credits") = value;
        CHECK(WriteAtomically(tempPath, Write(document)));
        CHECK(!ReadUpgradeConfig(tempPath, tuning, result, sizeof result) && result[0]);
        CHECK(tuning[SMR_UPGRADE_ORBITAL].cooldown_percent == -72.5 && tuning[SMR_UPGRADE_EAGLE].forge_credits == 7);
    }
    for (int i = 0; i < 7; ++i)
    {
        Value document = valid;
        if (i == 0) document.Erase("eagle");
        if (i == 1) document.At("support").Erase("cooldown_seconds");
        if (i == 2) document.At("eagle").At("typo") = Value::MakeNumber(10, true);
        if (i == 3) document.At("eagle").members.emplace_back("forge_credits", Value::MakeNumber(10, true));
        if (i == 4) document.At("orbital").At("cooldown_percent") = Value::MakeNumber(-101, true);
        if (i == 5) document.At("defensive").At("uses") = Value::MakeNumber(10, true);
        if (i == 6) document.At("support").At("uses") = Value::MakeNumber(5, true);
        CHECK(WriteAtomically(tempPath, Write(document)));
        CHECK(!ReadUpgradeConfig(tempPath, tuning, result, sizeof result) && result[0]);
        CHECK(tuning[SMR_UPGRADE_SUPPORT].cooldown_seconds == 2.5);
    }
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
