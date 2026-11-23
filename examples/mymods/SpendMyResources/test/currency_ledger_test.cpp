#include "../src/currency/currency_ledger.cpp"

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

static std::string Utf8Path(const std::wstring& path)
{
    char bytes[MAX_PATH * 3];
    CHECK(WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, bytes, sizeof bytes, nullptr, nullptr));
    return bytes;
}

static void SetProfile(const std::wstring& ready, const std::wstring& profile)
{
    hfjson::Value document;
    document.kind = hfjson::Value::Object;
    document.At("profile") = hfjson::Value::MakeString(Utf8Path(profile));
    CHECK(hfjson::WriteAtomically(ready, hfjson::Write(document)));
}

static void CheckWallet(const SmrWallet& actual, const SmrWallet& expected)
{
    for (size_t i = 0; i < CURRENCY_COUNT; ++i) CHECK(actual.amount[i] == expected.amount[i]);
}

int main()
{
    using namespace hfjson;
    wchar_t temp[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH, temp));
    const std::wstring root = std::wstring(temp) + L"smr-wallet-test-" + std::to_wstring(GetCurrentProcessId());
    const std::wstring backend = root + L"\\backend", game = root + L"\\game", local = root + L"\\Local";
    const std::wstring ready = backend + L"\\ready.json", profile = root + L"\\profil\u00e9.json";
    for (const auto& folder : {root, backend, game, local}) CHECK(CreateDirectoryW(folder.c_str(), nullptr));
    CHECK(SetEnvironmentVariableW(L"LOCALAPPDATA", local.c_str()));
    const std::string logs = Utf8Path(root), sandboxLogs = Utf8Path(game);
    const std::string original = R"({"inventory":{"items":[{"mixId":3608481516,"amount":50000}]}})";
    CHECK(WriteAtomically(profile, original));
    SetProfile(ready, profile);

    char detail[192];
    const SmrWallet initial = {{50000, 100, 50, 20, 100}};
    const SmrWallet updated = {{10000, 500, 51, 19, 110}};
    SmrWallet saved;
    CHECK(SmrOpenLedger(logs.c_str(), &initial, &saved, detail, sizeof detail));
    CheckWallet(saved, initial);
    const std::wstring first = ledgerPath;
    CHECK(first.find(local + L"\\SpendMyResources\\wallet-") == 0);
    CHECK(SmrWriteLedger(&updated, detail, sizeof detail));
    std::string text;
    CHECK(ReadFile(profile, &text) && text == original);

    // A different backend balance and the sandbox's logs-folder layout do not change the saved wallet.
    const SmrWallet backendAfter = {{50000, 500, 51, 20, 110}};
    for (int i = 0; i < 2; ++i)
    {
        CHECK(SmrOpenLedger(sandboxLogs.c_str(), &backendAfter, &saved, detail, sizeof detail));
        CheckWallet(saved, updated);
    }
    SetProfile(ready, root + L"\\second-profile.json");
    CHECK(SmrOpenLedger(logs.c_str(), &initial, &saved, detail, sizeof detail));
    CheckWallet(saved, initial);
    const std::wstring second = ledgerPath;
    CHECK(first != second);
    SetProfile(ready, profile);
    CHECK(SmrOpenLedger(logs.c_str(), &initial, &saved, detail, sizeof detail));
    CheckWallet(saved, updated);

    std::string valid, why;
    Value document;
    CHECK(ReadFile(first, &valid) && Parse(valid, &document, &why));
    const std::string req = std::to_string(kCurrencies[0].id);
    for (int test = 0; test < 6; ++test)
    {
        Value bad = document;
        if (test == 0) bad.At("version") = Value::MakeNumber(2, true);
        else if (test == 1) bad.At("profile") = Value::MakeString("wrong profile");
        else if (test == 2) bad.At("balances").Erase(req);
        else if (test == 3) bad.At("balances").At(req) = Value::MakeNumber(-1, true);
        else if (test == 4) bad.At("balances").At(req) = Value::MakeNumber(4294967296.0, true);
        else bad.At("balances").At(req) = Value::MakeNumber(1.5, false);
        const std::string invalid = Write(bad);
        CHECK(WriteAtomically(first, invalid));
        CHECK(!SmrOpenLedger(logs.c_str(), &initial, &saved, detail, sizeof detail));
        CHECK(!SmrWriteLedger(&initial, detail, sizeof detail));
        CHECK(ReadFile(first, &text) && text == invalid);
    }
    CHECK(WriteAtomically(first, valid));
    CHECK(SmrOpenLedger(logs.c_str(), &initial, &saved, detail, sizeof detail));
    HANDLE locked = CreateFileW(first.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(locked != INVALID_HANDLE_VALUE);
    CHECK(!SmrWriteLedger(&initial, detail, sizeof detail));
    CHECK(CloseHandle(locked));
    CHECK(ReadFile(first, &text) && text == valid);
    CHECK(ReadFile(profile, &text) && text == original);

    for (const auto& path : {ready, profile, first, second}) CHECK(DeleteFileW(path.c_str()));
    for (const auto& folder : {WalletFolder(), local, game, backend, root}) CHECK(RemoveDirectoryW(folder.c_str()));
    puts("PASS currency ledger: seed, reopen, sandbox path, per-profile isolation, invalid data refused, atomic save failure, profile unchanged");
}
