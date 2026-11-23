#include "json.cpp"
#include "currency_ledger.h"
#include <limits>

static std::wstring ledgerPath;
static std::string ledgerProfile;

static bool WalletAmount(const hfjson::Value* amount, unsigned* out)
{
    if (!amount || amount->kind != hfjson::Value::Number || amount->text.empty() ||
        amount->text.size() > 10 || amount->text.find_first_not_of("0123456789") != std::string::npos)
        return false;
    unsigned long long value = strtoull(amount->text.c_str(), nullptr, 10);
    if (value > std::numeric_limits<unsigned>::max()) return false;
    *out = static_cast<unsigned>(value);
    return true;
}

static std::string ActiveProfile(const char* logs)
{
    using namespace hfjson;
    if (!logs || !*logs) return {};
    const std::wstring folder = Wide(logs);
    std::string text, why;
    for (const wchar_t* suffix : {L"\\backend\\ready.json", L"\\..\\backend\\ready.json"})
    {
        if (!ReadFile(folder + suffix, &text, &why)) continue;
        Value ready;
        if (Parse(text, &ready, &why)) return ready.GetString("profile");
        break;
    }
    return {};
}

static std::wstring WalletFolder()
{
    DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!needed) return {};
    std::wstring folder(needed, L'\0');
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", &folder[0], needed);
    if (!length || length >= needed) return {};
    folder.resize(length);
    // HellForge redirects this to its game user data, including the sandbox's copy-back.
    return folder + L"\\SpendMyResources";
}

static std::wstring WalletPath(const std::wstring& folder, const std::string& profile)
{
    uint64_t key = UINT64_C(14695981039346656037);
    for (unsigned char ch : profile) key = (key ^ ch) * UINT64_C(1099511628211);
    return folder + L"\\wallet-" + std::to_wstring(key) + L".json";
}

static bool WriteWallet(const std::wstring& path, const std::string& profile, const SmrWallet& wallet)
{
    using namespace hfjson;
    Value document;
    document.kind = Value::Object;
    document.At("version") = Value::MakeNumber(1, true);
    document.At("profile") = Value::MakeString(profile);
    Value& balances = document.At("balances");
    for (size_t i = 0; i < CURRENCY_COUNT; ++i)
        balances.At(std::to_string(kCurrencies[i].id)) = Value::MakeNumber(wallet.amount[i], true);
    return WriteAtomically(path, Write(document));
}

extern "C" int SmrOpenLedger(const char* logs, const SmrWallet* initial, SmrWallet* saved, char* result, size_t size)
{
    using namespace hfjson;
    ledgerPath.clear();
    ledgerProfile = ActiveProfile(logs);
    if (ledgerProfile.empty())
    {
        snprintf(result, size, "Cannot identify the active offline profile.");
        return 0;
    }
    const std::wstring folder = WalletFolder();
    if (folder.empty() || (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS))
    {
        snprintf(result, size, "Cannot open the wallet folder in HellForge's game user data.");
        return 0;
    }
    const std::wstring path = WalletPath(folder, ledgerProfile);
    std::string text, why;
    SmrWallet wallet = *initial;
    if (ReadFile(path, &text, &why))
    {
        Value document;
        unsigned version;
        if (!Parse(text, &document, &why) || document.kind != Value::Object || document.members.size() != 3 ||
            !WalletAmount(document.Find("version"), &version) || version != 1 ||
            document.GetString("profile") != ledgerProfile)
        {
            snprintf(result, size, "Wallet file is invalid or belongs to a different profile.");
            return 0;
        }
        const Value* balances = document.Find("balances", Value::Object);
        if (!balances || balances->members.size() != CURRENCY_COUNT)
        {
            snprintf(result, size, "Wallet file does not contain the five currency balances.");
            return 0;
        }
        for (size_t i = 0; i < CURRENCY_COUNT; ++i)
            if (!WalletAmount(balances->Find(std::to_string(kCurrencies[i].id)), &wallet.amount[i]))
            {
                snprintf(result, size, "Wallet file has an invalid %s balance.", kCurrencies[i].name);
                return 0;
            }
    }
    else if (why != "is missing" || !WriteWallet(path, ledgerProfile, wallet))
    {
        snprintf(result, size, "Cannot read or create the wallet file in HellForge's game user data.");
        return 0;
    }
    ledgerPath = path;
    *saved = wallet;
    return 1;
}

extern "C" int SmrWriteLedger(const SmrWallet* wallet, char* result, size_t size)
{
    if (ledgerPath.empty() || !WriteWallet(ledgerPath, ledgerProfile, *wallet))
    {
        snprintf(result, size, "Currency ledger could not be saved.");
        return 0;
    }
    snprintf(result, size, "Currency ledger saved.");
    return 1;
}
