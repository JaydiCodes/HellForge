#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../src/shop/shop.h"
#include "../src/config/config_path.h"
#include "json.h"

struct HfMod { const HfApi* api; };
static const char kOwner[] = "author.test-pack";
static const char kEnemy[] = "author.test-pack:brute";
static const char kWeapon[] = "author.test-pack:rifle";
static const char kStratagem[] = "author.test-pack:gunship";
static std::string kFolderPath;

static HfContent MakeContent(uint32_t kind, uint64_t id, const char* name, const char* title, uint64_t entity = 0)
{
    HfContent content = {};
    content.size = sizeof content;
    content.kind = kind;
    content.id = id;
    content.mod = kOwner;
    content.name = name;
    content.title = title;
    content.entity = entity;
    return content;
}

static HfContent kContent[] = {
    MakeContent(HF_KIND_ENTITY, UINT64_C(0x12345678), kEnemy, "Brute"),
    MakeContent(HF_KIND_ITEM, UINT64_C(0xF0000001), kWeapon, "Rail Rifle", UINT64_C(0x87654321)),
    MakeContent(HF_KIND_STRATAGEM, 150, kStratagem, "Pelican Gunship")
};

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

static void Log(HfMod*, const char*, ...) {}
static int Mods(HfMod*, uint32_t index, HfModInfo* out)
{
    if (index) return 0;
    *out = {sizeof(HfModInfo), HF_MOD_CODE, kOwner, kFolderPath.c_str(), "", 1};
    return 1;
}
static int Content(HfMod*, uint32_t index, HfContent* out)
{
    if (index >= sizeof kContent / sizeof kContent[0]) return 0;
    *out = kContent[index];
    return 1;
}
static uint64_t Find(uint32_t kind, const char* name)
{
    for (const HfContent& entry : kContent)
        if (entry.kind == kind && !strcmp(entry.name, name)) return entry.id;
    return 0;
}

static std::wstring TestFolder()
{
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(nullptr, path, sizeof path / sizeof path[0]);
    CHECK(length && length < sizeof path / sizeof path[0]);
    std::wstring result(path, length);
    result.resize(result.find_last_of(L'\\'));
    result += L"\\";
    result += L"smr-mod-content-test-" + std::to_wstring(GetCurrentProcessId());
    CHECK(CreateDirectoryW(result.c_str(), nullptr));
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, result.c_str(), -1, nullptr, 0, nullptr, nullptr);
    CHECK(bytes > 1);
    kFolderPath.resize(bytes);
    CHECK(WideCharToMultiByte(CP_UTF8, 0, result.c_str(), -1, &kFolderPath[0], bytes, nullptr, nullptr) == bytes);
    kFolderPath.resize(bytes - 1);
    return result;
}

static void WriteFile(const std::wstring& path, const char* text)
{
    CHECK(hfjson::WriteAtomically(path, text));
}

int main()
{
    const std::wstring folder = TestFolder();
    const std::wstring executable_folder = folder.substr(0, folder.find_last_of(L'\\'));
    const std::wstring catalog_path = executable_folder + L"\\modded-content.json";
    const std::wstring price_path = executable_folder + L"\\prices.json";
    const std::wstring reward_path = executable_folder + L"\\kill-rewards.json";
    const std::wstring mod_path = folder + L"\\spend-my-resources.json";
    CHECK(GetFileAttributesW(catalog_path.c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW(price_path.c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW(reward_path.c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW(mod_path.c_str()) == INVALID_FILE_ATTRIBUTES);
    char result[256] = {};
    CHECK(SmrEnsureModdedContentFile(result, sizeof result));
    std::string contents;
    CHECK(hfjson::ReadFile(catalog_path, &contents) && contents == "{}\n");
    WriteFile(catalog_path, "{\"author.test-pack\":{\"enemies\":{\"brute\":25},\"weapons\":{\"rifle\":750},"
        "\"stratagems\":{\"gunship\":1500}},\"author.missing\":{\"weapons\":{\"ghost\":500}}}");
    CHECK(SmrEnsureModdedContentFile(result, sizeof result));
    CHECK(hfjson::ReadFile(catalog_path, &contents) && contents.find("\"brute\":25") != std::string::npos);
    WriteFile(price_path, "{\"author.test-pack:rifle\":900}");
    WriteFile(reward_path, "{\"author.test-pack:brute\":45}");
    WriteFile(mod_path, "{\"enemies\":{\"brute\":35},\"weapons\":{\"rifle\":850}}");

    HfApi api = {};
    api.log = Log;
    api.mods = Mods;
    api.content = Content;
    api.find = Find;
    HfMod mod = {&api};
    AccessItem items[SMR_MAX_RESTRICTED_ITEMS] = {};
    const char* keys[SMR_MAX_RESTRICTED_ITEMS] = {};
    for (size_t i = 0; i < SMR_BUILTIN_PRICE_COUNT; ++i) keys[i] = "vanilla";
    size_t item_count = SMR_BUILTIN_PRICE_COUNT;
    SmrModdedEnemy enemies[SMR_MAX_MODDED_ENEMIES] = {};
    size_t enemy_count = 0;
    CHECK(SmrLoadModdedContent(&mod, items, keys, &item_count, enemies, &enemy_count, nullptr, 0,
        result, sizeof result));
    CHECK(item_count == SMR_BUILTIN_PRICE_COUNT + 2 && enemy_count == 1);
    CHECK(!strcmp(keys[SMR_BUILTIN_PRICE_COUNT], kWeapon) &&
        items[SMR_BUILTIN_PRICE_COUNT].kind == PRICE_WEAPON &&
        items[SMR_BUILTIN_PRICE_COUNT].id == UINT32_C(0xF0000001) &&
        items[SMR_BUILTIN_PRICE_COUNT].requisition == 900);
    CHECK(!strcmp(keys[SMR_BUILTIN_PRICE_COUNT + 1], kStratagem) &&
        items[SMR_BUILTIN_PRICE_COUNT + 1].kind == PRICE_STRATAGEM &&
        items[SMR_BUILTIN_PRICE_COUNT + 1].requisition == 1500);
    CHECK(enemies[0].resource == UINT64_C(0x12345678) && !strcmp(enemies[0].key, kEnemy) &&
        enemies[0].value == 45);

    CHECK(DeleteFileW(catalog_path.c_str()) && DeleteFileW(price_path.c_str()) &&
        DeleteFileW(reward_path.c_str()) && DeleteFileW(mod_path.c_str()));
    CHECK(CreateDirectoryW(catalog_path.c_str(), nullptr));
    CHECK(!SmrEnsureModdedContentFile(result, sizeof result) && result[0]);
    CHECK(RemoveDirectoryW(catalog_path.c_str()));
    CHECK(RemoveDirectoryW(folder.c_str()));
    puts("PASS modded content: identity resolution, missing mods, layered mod and player overrides.");
    return 0;
}
