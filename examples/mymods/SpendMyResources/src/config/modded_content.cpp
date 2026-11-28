#include "json.h"
#include "config_path.h"
#include "../shop/shop.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
struct ModContent
{
    HfModInfo info;
    std::vector<HfContent> entries;
};

static bool Qualified(const std::string& name)
{
    return name.find(':') != std::string::npos || name.find('/') != std::string::npos;
}

static std::string Identity(const char* mod, const std::string& name)
{
    return Qualified(name) ? name : std::string(mod) + ":" + name;
}

static bool ReadObject(const std::wstring& path, hfjson::Value* out, std::string* why)
{
    std::string text;
    return hfjson::ReadFile(path, &text, why) && hfjson::Parse(text, out, why) && out->kind == hfjson::Value::Object;
}

static bool WholeNumber(const hfjson::Value& value, unsigned maximum, unsigned* out)
{
    if (value.kind != hfjson::Value::Number || value.text.empty() || value.text.size() > 10 ||
        value.text.find_first_not_of("0123456789") != std::string::npos)
        return false;
    const unsigned long long parsed = strtoull(value.text.c_str(), nullptr, 10);
    if (parsed > maximum) return false;
    *out = static_cast<unsigned>(parsed);
    return true;
}

static const HfContent* Resolve(const HfApi* api, const ModContent& mod, uint32_t kind, const std::string& name)
{
    const std::string identity = Identity(mod.info.id, name);
    const uint64_t id = api->find(kind, identity.c_str());
    if (!id) return nullptr;
    for (const HfContent& entry : mod.entries)
        if (entry.kind == kind && entry.id == id && entry.mod && !strcmp(entry.mod, mod.info.id))
            return &entry;
    return nullptr;
}

static size_t ItemIndex(const char* identity, const char** keys, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (keys[i] && !strcmp(keys[i], identity)) return i;
    return count;
}

static size_t EnemyIndex(uint64_t resource, const SmrModdedEnemy* enemies, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (enemies[i].resource == resource) return i;
    return count;
}

static void ApplyEntries(HfMod* caller, const HfApi* api, const ModContent& mod, const hfjson::Value& data,
    AccessItem* items, const char** item_keys, size_t* item_count, SmrModdedEnemy* enemies, size_t* enemy_count)
{
    if (data.kind != hfjson::Value::Object)
    {
        api->log(caller, "Modded content from %s must be a JSON object; skipped.", mod.info.id);
        return;
    }

    const struct { const char* field; uint32_t kind; PriceKind price_kind; } categories[] = {
        {"weapons", HF_KIND_ITEM, PRICE_WEAPON},
        {"stratagems", HF_KIND_STRATAGEM, PRICE_STRATAGEM}
    };
    for (const auto& category : categories)
    {
        const hfjson::Value* values = data.Find(category.field);
        if (!values) continue;
        if (values->kind != hfjson::Value::Object)
        {
            api->log(caller, "Modded content %s.%s must be an object; skipped.", mod.info.id, category.field);
            continue;
        }
        for (const auto& member : values->members)
        {
            unsigned price;
            if (!WholeNumber(member.second, std::numeric_limits<unsigned>::max() / SMR_MAX_RESTRICTED_ITEMS,
                    &price) || !price)
            {
                api->log(caller, "Modded content %s.%s.%s has an invalid price; skipped.",
                    mod.info.id, category.field, member.first.c_str());
                continue;
            }
            const HfContent* content = Resolve(api, mod, category.kind, member.first);
            if (!content || (category.kind == HF_KIND_ITEM && content->id > UINT32_MAX))
            {
                api->log(caller, "Modded content %s.%s.%s was not found in that mod; skipped.",
                    mod.info.id, category.field, member.first.c_str());
                continue;
            }
            const size_t existing = ItemIndex(content->name, item_keys, *item_count);
            if (existing < *item_count)
            {
                items[existing].requisition = price;
                continue;
            }
            if (*item_count >= SMR_MAX_RESTRICTED_ITEMS)
            {
                api->log(caller, "Modded content item limit (%u) reached; %s was skipped.",
                    (unsigned)SMR_MAX_RESTRICTED_ITEMS, content->name);
                continue;
            }
            const size_t index = (*item_count)++;
            items[index] = (AccessItem){category.price_kind, static_cast<uint32_t>(content->id),
                content->title && content->title[0] ? content->title : content->name, price};
            item_keys[index] = content->name;
        }
    }

    const hfjson::Value* enemies_data = data.Find("enemies");
    if (!enemies_data) return;
    if (enemies_data->kind != hfjson::Value::Object)
    {
        api->log(caller, "Modded content %s.enemies must be an object; skipped.", mod.info.id);
        return;
    }
    for (const auto& member : enemies_data->members)
    {
        unsigned value;
        if (!WholeNumber(member.second, SMR_KILL_MAX_VALUE, &value))
        {
            api->log(caller, "Modded content %s.enemies.%s has an invalid reward; skipped.",
                mod.info.id, member.first.c_str());
            continue;
        }
        const HfContent* content = Resolve(api, mod, HF_KIND_ENTITY, member.first);
        if (!content)
        {
            api->log(caller, "Modded content %s.enemies.%s was not found in that mod; skipped.",
                mod.info.id, member.first.c_str());
            continue;
        }
        size_t index = EnemyIndex(content->id, enemies, *enemy_count);
        if (index == *enemy_count)
        {
            if (*enemy_count >= SMR_MAX_MODDED_ENEMIES)
            {
                api->log(caller, "Modded enemy limit (%u) reached; %s was skipped.",
                    (unsigned)SMR_MAX_MODDED_ENEMIES, content->name);
                continue;
            }
            index = (*enemy_count)++;
            enemies[index] = (SmrModdedEnemy){content->id, content->name,
                content->title && content->title[0] ? content->title : content->name, SMR_KILL_DEFAULT_VALUE};
        }
        enemies[index].value = value;
    }
}

static void ApplyDynamicOverrides(HfMod* caller, const HfApi* api, const std::wstring& path, bool prices,
    AccessItem* items, const char** item_keys, size_t item_count, SmrModdedEnemy* enemies, size_t enemy_count,
    const char* const* builtin_enemy_names, size_t builtin_enemy_count)
{
    hfjson::Value document;
    std::string why;
    if (!ReadObject(path, &document, &why))
    {
        if (why.find("is missing") == std::string::npos)
            api->log(caller, "Cannot read %s: %s", prices ? "prices.json modded entries" : "kill-rewards.json modded entries",
                why.c_str());
        return;
    }
    for (const auto& member : document.members)
    {
        unsigned value;
        if (!WholeNumber(member.second, prices ?
                std::numeric_limits<unsigned>::max() / SMR_MAX_RESTRICTED_ITEMS : SMR_KILL_MAX_VALUE, &value))
        {
            if (Qualified(member.first))
                api->log(caller, "Modded %s entry %s has an invalid value; skipped.",
                    prices ? "price" : "kill reward", member.first.c_str());
            continue;
        }
        bool found = false;
        if (prices)
        {
            bool builtin = false;
            for (size_t i = 0; i < SMR_BUILTIN_PRICE_COUNT; ++i)
                if (item_keys[i] && member.first == item_keys[i])
                {
                    builtin = true;
                    break;
                }
            if (builtin) continue;
            for (size_t i = SMR_BUILTIN_PRICE_COUNT; i < item_count; ++i)
                if (item_keys[i] && member.first == item_keys[i])
                {
                    items[i].requisition = value;
                    found = true;
                    break;
                }
        }
        else
        {
            bool builtin = false;
            for (size_t i = 0; i < builtin_enemy_count; ++i)
                if (builtin_enemy_names[i] && member.first == builtin_enemy_names[i])
                {
                    builtin = true;
                    break;
                }
            if (builtin) continue;
            for (size_t i = 0; i < enemy_count; ++i)
                if ((enemies[i].key && member.first == enemies[i].key) ||
                    (enemies[i].name && member.first == enemies[i].name))
                {
                    enemies[i].value = value;
                    found = true;
                    break;
                }
        }
        if (!found && Qualified(member.first))
            api->log(caller, "Modded %s entry %s is not in the installed content catalog; skipped.",
                prices ? "price" : "kill reward", member.first.c_str());
    }
}
}

extern "C" int SmrEnsureModdedContentFile(char* result, size_t size)
{
    const auto path = ConfigPath(L"modded-content.json", result, size);
    if (path.empty()) return 0;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return 1;
        }
        snprintf(result, size, "Cannot create modded-content.json (Windows error %lu).", error);
        return 0;
    }

    const char contents[] = "{}\n";
    DWORD written = 0;
    const bool wrote = WriteFile(file, contents, sizeof contents - 1, &written, nullptr) != 0;
    if (!wrote || written != sizeof contents - 1)
    {
        const DWORD error = wrote ? ERROR_WRITE_FAULT : GetLastError();
        FILE_DISPOSITION_INFO remove = {TRUE};
        const bool removed = SetFileInformationByHandle(file, FileDispositionInfo, &remove, sizeof remove) != 0;
        CloseHandle(file);
        snprintf(result, size, "Cannot write modded-content.json (Windows error %lu)%s.",
            error, removed ? "" : "; incomplete file could not be removed");
        return 0;
    }
    if (!CloseHandle(file))
    {
        snprintf(result, size, "Cannot close modded-content.json (Windows error %lu).", GetLastError());
        return 0;
    }
    return 1;
}

extern "C" int SmrLoadModdedContent(HfMod* mod, AccessItem* items, const char** item_keys, size_t* item_count,
    SmrModdedEnemy* enemies, size_t* enemy_count, const char* const* builtin_enemy_names,
    size_t builtin_enemy_count, char* result, size_t size)
{
    const HfApi* api = HF_API(mod);
    if (!api->content || !api->mods || !api->find)
    {
        snprintf(result, size, "The SDK cannot enumerate mod content.");
        return 0;
    }

    std::vector<ModContent> mods;
    for (uint32_t index = 0;; ++index)
    {
        HfModInfo info = {sizeof info};
        if (!api->mods(mod, index, &info)) break;
        if (!info.loaded || !info.id) continue;
        ModContent current = {};
        current.info = info;
        for (uint32_t content_index = 0;; ++content_index)
        {
            HfContent content = {sizeof content};
            if (!api->content(mod, content_index, &content)) break;
            if (content.mod && !strcmp(content.mod, info.id))
                current.entries.push_back(content);
        }
        mods.push_back(current);
    }

    char detail[256] = {};
    const auto catalog_path = ConfigPath(L"modded-content.json", detail, sizeof detail);
    hfjson::Value catalog;
    std::string why;
    if (catalog_path.empty() || !ReadObject(catalog_path, &catalog, &why))
    {
        if (why.find("is missing") == std::string::npos)
            api->log(mod, "Cannot read modded-content.json: %s", detail[0] ? detail : why.c_str());
    }
    else
    {
        for (const auto& mod_entry : catalog.members)
        {
            auto found = std::find_if(mods.begin(), mods.end(), [&](const ModContent& candidate) {
                return mod_entry.first == candidate.info.id;
            });
            if (found != mods.end()) ApplyEntries(mod, api, *found, mod_entry.second, items, item_keys,
                item_count, enemies, enemy_count);
        }
    }

    for (const ModContent& current : mods)
    {
        if (!current.info.folder || !current.info.folder[0]) continue;
        const std::wstring path = hfjson::Wide(current.info.folder) + L"\\spend-my-resources.json";
        hfjson::Value data;
        why.clear();
        if (!ReadObject(path, &data, &why))
        {
            if (why.find("is missing") == std::string::npos)
                api->log(mod, "Cannot read spend-my-resources.json from %s: %s", current.info.id, why.c_str());
            continue;
        }
        ApplyEntries(mod, api, current, data, items, item_keys, item_count, enemies, enemy_count);
    }

    ApplyDynamicOverrides(mod, api, ConfigPath(L"prices.json", detail, sizeof detail), true,
        items, item_keys, *item_count, enemies, *enemy_count, builtin_enemy_names, builtin_enemy_count);
    ApplyDynamicOverrides(mod, api, ConfigPath(L"kill-rewards.json", detail, sizeof detail), false,
        items, item_keys, *item_count, enemies, *enemy_count, builtin_enemy_names, builtin_enemy_count);

    for (const ModContent& current : mods)
        for (const HfContent& entry : current.entries)
        {
            bool listed = false;
            if (entry.kind == HF_KIND_ITEM || entry.kind == HF_KIND_STRATAGEM)
                listed = ItemIndex(entry.name, item_keys, *item_count) < *item_count;
            else if (entry.kind == HF_KIND_ENTITY)
            {
                listed = EnemyIndex(entry.id, enemies, *enemy_count) < *enemy_count;
                for (const HfContent& weapon : current.entries)
                    if (weapon.kind == HF_KIND_ITEM && weapon.entity == entry.id) listed = true;
            }
            else continue;
            if (!listed)
                api->log(mod, "Uncatalogued mod content: %s (%s).",
                    entry.kind == HF_KIND_ENTITY ? "entity (possible enemy)" :
                    entry.kind == HF_KIND_ITEM ? "weapon" : "stratagem", entry.name);
        }

    snprintf(result, size, "Loaded %u modded items and %u modded enemies.",
        (unsigned)(*item_count - SMR_BUILTIN_PRICE_COUNT), (unsigned)*enemy_count);
    return 1;
}
