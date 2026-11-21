#include "../src/profile_payment.cpp"
#include <stdlib.h>
#include "../src/currencies.h"

#define CHECK(test) do { if (!(test)) { printf("FAIL line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

int main()
{
    using namespace hfjson;
    wchar_t temp[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH, temp));
    const std::wstring root = std::wstring(temp) + L"smr-sample-test-" + std::to_wstring(GetCurrentProcessId());
    CHECK(CreateDirectoryW(root.c_str(), nullptr));
    CHECK(CreateDirectoryW((root + L"\\backend").c_str(), nullptr));
    const std::wstring path = root + L"\\profile.json";
    Value ready;
    ready.kind = Value::Object;
    char utf8[MAX_PATH * 3];
    CHECK(WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8, sizeof utf8, nullptr, nullptr));
    ready.At("profile") = Value::MakeString(utf8);
    CHECK(WriteAtomically(root + L"\\backend\\ready.json", Write(ready)));
    CHECK(WideCharToMultiByte(CP_UTF8, 0, root.c_str(), -1, utf8, sizeof utf8, nullptr, nullptr));
    const std::string original = R"({"inventory":{"items":[{"mixId":3992382197,"amount":3},{"mixId":3608481516,"amount":5000}]},"progression":{"experience":123},"other":"unchanged"})";
    CHECK(WriteAtomically(path, original));
    char result[192];
    CHECK(SmrDeductProfile(utf8, 3992382197u, 1, result, sizeof result));
    Value actual, expected;
    std::string text, why;
    CHECK(ReadFile(path, &text) && Parse(text, &actual, &why) && Parse(original, &expected, &why));
    expected.At("inventory").At("items").items[0].At("amount") = Value::MakeNumber(2, true);
    CHECK(Write(actual) == Write(expected));
    for (const char* invalid : {
        R"({"inventory":{"items":[{"mixId":3992382197,"amount":0}]}})",
        R"({"inventory":{"items":[{"mixId":3992382197,"amount":-1}]}})",
        R"({"inventory":{"items":[{"mixId":3992382197,"amount":3},{"mixId":3992382197,"amount":3}]}})",
        R"({"inventory":{"items":[]}})", "broken"})
    {
        CHECK(WriteAtomically(path, invalid));
        CHECK(!SmrDeductProfile(utf8, 3992382197u, 1, result, sizeof result));
        CHECK(ReadFile(path, &text) && text == invalid);
    }
    for (unsigned price : {250u, 500u, 750u, 1000u, 2000u, 2750u})
    {
        CHECK(WriteAtomically(path, original));
        CHECK(SmrDeductProfile(utf8, 3608481516u, price, result, sizeof result));
        CHECK(ReadFile(path, &text) && Parse(text, &actual, &why) && Parse(original, &expected, &why));
        expected.At("inventory").At("items").items[1].At("amount") = Value::MakeNumber(5000 - price, true);
        CHECK(Write(actual) == Write(expected));
    }
    CHECK(WriteAtomically(path, original));
    CHECK(!SmrDeductProfile(utf8, 3608481516u, 5001, result, sizeof result));
    CHECK(ReadFile(path, &text) && text == original);
    CHECK(!SmrDeductProfile(utf8, 3608481516u, 0, result, sizeof result));
    CHECK(ReadFile(path, &text) && text == original);
    Value exchangeOriginal;
    exchangeOriginal.kind = Value::Object;
    exchangeOriginal.At("other") = Value::MakeString("unchanged");
    Value& exchangeItems = exchangeOriginal.At("inventory").At("items");
    exchangeItems.kind = Value::Array;
    for (size_t i = 0; i < CURRENCY_COUNT; ++i)
    {
        Value item;
        item.kind = Value::Object;
        item.At("mixId") = Value::MakeNumber(kCurrencies[i].id, true);
        item.At("amount") = Value::MakeNumber(1000, true);
        item.At("other") = Value::MakeString("preserve item fields");
        exchangeItems.items.push_back(item);
    }
    CHECK(WriteAtomically(path, Write(exchangeOriginal)));
    CHECK(!SmrDeductProfilePair(utf8, kCurrencies[0].id, 750, kCurrencies[FORGE_CREDITS].id, 1001, result, sizeof result));
    CHECK(ReadFile(path, &text) && text == Write(exchangeOriginal));
    CHECK(SmrDeductProfilePair(utf8, kCurrencies[0].id, 750, kCurrencies[FORGE_CREDITS].id, 10, result, sizeof result));
    expected = exchangeOriginal;
    expected.At("inventory").At("items").items[0].At("amount") = Value::MakeNumber(250, true);
    expected.At("inventory").At("items").items[FORGE_CREDITS].At("amount") = Value::MakeNumber(990, true);
    CHECK(ReadFile(path, &text) && Parse(text, &actual, &why) && Write(actual) == Write(expected));
    for (size_t from = 0; from < CURRENCY_COUNT; ++from)
        for (size_t to = 0; to < CURRENCY_COUNT; ++to)
        {
            if (from == to) continue;
            CHECK(WriteAtomically(path, Write(exchangeOriginal)));
            CHECK(SmrExchangeProfile(utf8, kCurrencies[from].id, 10, kCurrencies[to].id, 20, result, sizeof result));
            expected = exchangeOriginal;
            expected.At("inventory").At("items").items[from].At("amount") = Value::MakeNumber(990, true);
            expected.At("inventory").At("items").items[to].At("amount") = Value::MakeNumber(1020, true);
            CHECK(ReadFile(path, &text) && Parse(text, &actual, &why) && Write(actual) == Write(expected));
        }
    CHECK(WriteAtomically(path, original));
    CHECK(SmrExchangeProfile(utf8, 3608481516u, 500, kCurrencies[4].id, 1, result, sizeof result));
    CHECK(ReadFile(path, &text) && Parse(text, &actual, &why));
    CHECK(actual.Find("inventory")->Find("items")->items.size() == 3);
    CHECK(actual.Find("inventory")->Find("items")->items[2].GetNumber("mixId") == kCurrencies[4].id);
    CHECK(actual.Find("inventory")->Find("items")->items[2].GetNumber("amount") == 1);
    for (const char* invalid : {
        R"({"inventory":{"items":[{"mixId":3608481516,"amount":5000},{"mixId":1815905217,"amount":-1}]}})",
        R"({"inventory":{"items":[{"mixId":3608481516,"amount":5000},{"mixId":1815905217,"amount":1.5}]}})",
        R"({"inventory":{"items":[{"mixId":3608481516,"amount":5000},{"mixId":1815905217,"amount":4294967295}]}})",
        R"({"inventory":{"items":[{"mixId":3608481516,"amount":5000},{"mixId":1815905217}]}})",
        R"({"inventory":{"items":[{"mixId":3608481516,"amount":5000},{"mixId":1815905217,"amount":0},{"mixId":1815905217,"amount":0}]}})",
        R"({"inventory":{"items":[{"mixId":3608481516,"amount":5000},{"mixId":3608481516,"amount":5000}]}})",
        R"({"inventory":{"items":[{"mixId":3608481516,"amount":499}]}})"
    })
    {
        CHECK(WriteAtomically(path, invalid));
        CHECK(!SmrExchangeProfile(utf8, 3608481516u, 500, kCurrencies[4].id, 1, result, sizeof result));
        CHECK(ReadFile(path, &text) && text == invalid);
    }
    CHECK(WriteAtomically(path, original));
    CHECK(!SmrExchangeProfile(utf8, 3608481516u, 500, 3608481516u, 1, result, sizeof result));
    CHECK(!SmrExchangeProfile(utf8, 3608481516u, 0, kCurrencies[4].id, 1, result, sizeof result));
    CHECK(!SmrExchangeProfile(utf8, 3608481516u, 500, kCurrencies[4].id, 0, result, sizeof result));
    CHECK(ReadFile(path, &text) && text == original);
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(locked != INVALID_HANDLE_VALUE);
    CHECK(!SmrExchangeProfile(utf8, 3608481516u, 500, kCurrencies[4].id, 1, result, sizeof result));
    CHECK(strstr(result, "write FAILED") && ReadFile(path, &text) && text == original);
    CHECK(CloseHandle(locked));
    const std::wstring pendingWrite = path + L".ingame.tmp";
    if (GetFileAttributesW(pendingWrite.c_str()) != INVALID_FILE_ATTRIBUTES) CHECK(DeleteFileW(pendingWrite.c_str()));
    CHECK(DeleteFileW(path.c_str()));
    CHECK(DeleteFileW((root + L"\\backend\\ready.json").c_str()));
    CHECK(RemoveDirectoryW((root + L"\\backend").c_str()));
    CHECK(RemoveDirectoryW(root.c_str()));
    puts("PASS profile payments and exchanges: arbitrary payments, all currency pairs, missing destination creation, atomic updates, unrelated fields preserved, invalid/insufficient/duplicate/overflow balances rejected.");
}
