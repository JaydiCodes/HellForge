#include "json.cpp"
#include "../src/prices.cpp"
#include "../src/access_items.h"
#include <algorithm>

#define CHECK(test) do { if (!(test)) { printf("FAIL line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

int main(int argc, char** argv)
{
    using namespace hfjson;
    CHECK(argc == 2);
    const char* names[PRICE_COUNT];
    unsigned prices[PRICE_COUNT] = {};
    for (size_t i = 0; i < PRICE_COUNT; ++i) names[i] = kLoadoutPrices[i].name;
    char result[256] = {};
    CHECK(ReadPrices(Wide(argv[1]), names, prices, PRICE_COUNT, result, sizeof result));
    for (size_t i = 0; i < PRICE_COUNT; ++i) CHECK(prices[i] == kLoadoutPrices[i].requisition);

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
}
