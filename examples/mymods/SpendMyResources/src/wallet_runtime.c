/* The ready inventory is only populated from the backend at login. Sample the live amounts in
 * every state (including pickups during a mission); when inventory is loaded again, restore, don't sample. */
static uintptr_t ledger_manager;
static int ledger_open;
static SmrWallet ledger_wallet;

static int ReadWallet(const HfApi* api, uintptr_t manager, SmrWallet* out)
{
    for (size_t i = 0; i < CURRENCY_COUNT; ++i)
        if (!ItemRecord(api, manager, kCurrencies[i].id, &out->amount[i])) return 0;
    return 1;
}

static int SaveWallet(const SmrWallet* wallet, char* result, size_t size)
{
    if (!memcmp(wallet->amount, ledger_wallet.amount, sizeof wallet->amount)) return 1;
    if (!SmrWriteLedger(wallet, result, size)) return 0;
    ledger_wallet = *wallet;
    return 1;
}

static int SaveTransaction(const HfApi* api, uintptr_t manager, char* result, size_t size)
{
    SmrWallet wallet;
    if (!ReadWallet(api, manager, &wallet))
    {
        snprintf(result, size, "Could not read the updated inventory; ledger unchanged.");
        return 0;
    }
    return SaveWallet(&wallet, result, size);
}

/* 1 restored, 0 failed, -1 caps not readable yet (no writes attempted). */
static int RestoreWallet(const HfApi* api, uintptr_t manager, const SmrWallet* current, char* result, size_t size)
{
    for (size_t i = 0; i < CURRENCY_COUNT; ++i)
    {
        if (ledger_wallet.amount[i] <= current->amount[i]) continue;
        unsigned ignored, cap;
        uintptr_t record = ItemRecord(api, manager, kCurrencies[i].id, &ignored);
        if (!record || api->read(record + 0x18, &cap, sizeof cap)) return -1;
        if (cap && ledger_wallet.amount[i] > cap)
        {
            snprintf(result, size, "Saved %s balance exceeds the game cap.", kCurrencies[i].name);
            return 0;
        }
    }
    for (size_t i = 0; i < CURRENCY_COUNT; ++i)
    {
        uint32_t id = kCurrencies[i].id;
        unsigned before = current->amount[i], want = ledger_wallet.amount[i];
        if (before > want) inventory_remove(manager, id, before - want);
        else if (before < want) inventory_add(manager, id, want - before, 0, NULL, 0);
    }
    SmrWallet actual;
    if (!ReadWallet(api, manager, &actual) || memcmp(actual.amount, ledger_wallet.amount, sizeof actual.amount))
    {
        snprintf(result, size, "Could not restore the saved balances into the game inventory.");
        return 0;
    }
    return 1;
}

static int TrackWallet(HfMod* mod, uintptr_t manager)
{
    if (!manager)
    {
        ledger_manager = 0;
        return 0;
    }
    const HfApi* api = HF_API(mod);
    SmrWallet wallet;
    if (!ReadWallet(api, manager, &wallet)) return 0;
    char detail[128], message[192];
    if (!ledger_open)
    {
        if (!SmrOpenLedger(logs_folder, &wallet, &ledger_wallet, detail, sizeof detail)) goto failed;
        ledger_open = 1;
    }
    if (ledger_manager != manager)
    {
        int restored = RestoreWallet(api, manager, &wallet, detail, sizeof detail);
        if (restored < 0) return 0;
        if (!restored) goto failed;
        ledger_manager = manager;
        api->log(mod, "Loaded the Spend My Resources wallet for this offline profile.");
    }
    else if (!SaveWallet(&wallet, detail, sizeof detail)) goto failed;
    return 1;

failed:
    snprintf(message, sizeof message, "%s Shop disabled for this session.", detail);
    StopShop(mod, message);
    return 0;
}
