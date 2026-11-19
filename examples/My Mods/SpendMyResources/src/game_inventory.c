/* Reviewed inventory layout/functions: native/mod-loader/ownership.c, build 25480438. Only called on the game thread. */
enum {
    INVENTORY_CELL = 0x347CEF8, INVENTORY_COUNT = 0x1CE0, INVENTORY_RECORDS = 0x1CE4,
    INVENTORY_STRIDE = 0xB8, CATALOGUE_STATUS = 0xD5D48, INVENTORY_STATUS = 0xD5D4C,
    REQUISITION = 0xD7150EECu
};
static uintptr_t game_base;
static void (*inventory_remove)(uintptr_t, uint32_t, uint32_t);

static uintptr_t Pointer(const HfApi* api, uintptr_t at)
{
    uintptr_t result = 0;
    return api->read(at, &result, sizeof result) ? 0 : result;
}

static uintptr_t Inventory(const HfApi* api)
{
    uintptr_t manager = Pointer(api, game_base + INVENTORY_CELL);
    uint32_t catalogue = 0, inventory = 0;
    if (!manager || api->read(manager + CATALOGUE_STATUS, &catalogue, 4) ||
        api->read(manager + INVENTORY_STATUS, &inventory, 4) || catalogue != 2 || inventory != 2)
        return 0;
    return manager;
}

static uintptr_t ItemRecord(const HfApi* api, uintptr_t manager, uint32_t mix_id, unsigned* amount)
{
    static struct { uintptr_t manager, record; uint32_t id; } cache[PRICE_COUNT + 1];
    for (size_t i = 0; i < PRICE_COUNT + 1; ++i)
        if (cache[i].manager == manager && cache[i].id == mix_id)
        {
            uint32_t id;
            if (!api->read(cache[i].record + 4, &id, 4) && id == mix_id &&
                !api->read(cache[i].record + 0xB0, amount, 4)) return cache[i].record;
            cache[i].manager = 0;
        }
    uint32_t count;
    if (api->read(manager + INVENTORY_COUNT, &count, 4) || count > 4096) return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        uintptr_t record = manager + INVENTORY_RECORDS + (uintptr_t)i * INVENTORY_STRIDE;
        uint32_t id;
        if (api->read(record + 4, &id, 4)) return 0;
        if (id == mix_id)
        {
            if (api->read(record + 0xB0, amount, 4)) return 0;
            for (size_t k = 0; k < PRICE_COUNT + 1; ++k)
                if (!cache[k].manager || cache[k].manager != manager)
                {
                    cache[k].manager = manager;
                    cache[k].record = record;
                    cache[k].id = mix_id;
                    break;
                }
            return record;
        }
    }
    return 0;
}

static int LayoutMatches(const HfApi* api, uintptr_t game)
{
    return api->code_matches(game + 0x1361F69, "48 8B 35 E0 AE 11 02") &&
        api->code_matches(game + 0x136F5E0, "85 D2 0F 84 91 00 00 00 53 48 83 EC 20 44 8B 91 E0 1C 00 00") &&
        api->code_matches(game + 0x136E6C1,
            "48 8B 35 30 E8 10 02 33 DB 4D 89 73 18 44 8B F3 8B AE E0 1C 00 00 "
            "4C 8D AE E4 1C 00 00 89 AE 44 1D 0D 00") &&
        api->code_matches(game + 0x1362043,
            "39 9E 00 D2 02 00 76 36 0F 1F 44 00 00 8B C3 48 69 D0 90 16 00 00 48 03 D6 48 39 2A "
            "75 16 48 8D 4F 08 48 83 C2 38 41 B8 F0 09 00 00") &&
        api->code_matches(game + 0x11E8315,
            "3B BE 88 07 00 00 72 11 41 8B 04 24 3B F8 0F 83 C6 01 00 00 48 8B 44 24 70 "
            "48 8D 0C 7F 48 C1 E1 04");
}
