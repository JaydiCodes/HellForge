#include "../src/profile_payment.cpp"
#include <stdlib.h>

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
    CHECK(DeleteFileW(path.c_str()));
    CHECK(DeleteFileW((root + L"\\backend\\ready.json").c_str()));
    CHECK(RemoveDirectoryW((root + L"\\backend").c_str()));
    CHECK(RemoveDirectoryW(root.c_str()));
    puts("PASS profile payments: samples and arbitrary requisition prices, other data preserved, invalid/insufficient balances rejected.");
}
