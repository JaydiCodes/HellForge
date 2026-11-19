static DWORD WINAPI SpendWorker(void* unused)
{
    char error[192] = "";
    int result = SmrSpendRequisition(logs_folder, token_attachment, kLoadoutPrices[charge.item].requisition, error, sizeof error);
    AcquireSRWLockExclusive(&shop_lock);
    snprintf(charge.error, sizeof charge.error, "%s", error);
    charge.state = result > 0 ? CHARGE_SAVED : result == 0 ? CHARGE_REFUSED : CHARGE_UNKNOWN;
    ReleaseSRWLockExclusive(&shop_lock);
    return 0;
}

int SmrBuyAccess(HfMod* mod, size_t i)
{
    if (HF_API(mod)->game_state(mod) != HF_STATE_SHIP)
        return 0;
    AcquireSRWLockExclusive(&shop_lock);
    if (i >= PRICE_COUNT || access[i] || !inventory_ready || !owned_amount[i] ||
        charge.state != CHARGE_IDLE || wallet < kLoadoutPrices[i].requisition)
    {
        ReleaseSRWLockExclusive(&shop_lock);
        return 0;
    }
    charge.item = i;
    charge.state = CHARGE_QUEUED;
    ReleaseSRWLockExclusive(&shop_lock);
    ShopMessage(mod, "Saving requisition charge...");
    return 1;
}

static int ProcessPurchase(HfMod* mod, uintptr_t manager, unsigned balance, int state, size_t bought)
{
    const HfApi* api = HF_API(mod);
    if (state == CHARGE_QUEUED)
    {
        if (api->game_state(mod) != HF_STATE_SHIP)
        {
            AcquireSRWLockExclusive(&shop_lock);
            charge.state = CHARGE_IDLE;
            ReleaseSRWLockExclusive(&shop_lock);
            ShopMessage(mod, "Purchase cancelled before it was sent; return to the ship.");
        }
        else
        {
            AcquireSRWLockExclusive(&shop_lock);
            charge.state = CHARGE_RUNNING;
            ReleaseSRWLockExclusive(&shop_lock);
            HANDLE thread = CreateThread(NULL, 0, SpendWorker, NULL, 0, NULL);
            if (thread) CloseHandle(thread);
            else
            {
                AcquireSRWLockExclusive(&shop_lock);
                charge.state = CHARGE_IDLE;
                ReleaseSRWLockExclusive(&shop_lock);
                ShopMessage(mod, "Purchase worker could not start; no request was sent.");
            }
        }
    }
    else if (state == CHARGE_SAVED)
    {
        AcquireSRWLockExclusive(&shop_lock);
        access[bought] = 1;
        ReleaseSRWLockExclusive(&shop_lock);
        unsigned price = kLoadoutPrices[bought].requisition, after;
        inventory_remove(manager, REQUISITION, price);
        if (!ItemRecord(api, manager, REQUISITION, &after) || after != (balance >= price ? balance - price : 0))
        {
            AcquireSRWLockExclusive(&shop_lock);
            inventory_ready = 0;
            ReleaseSRWLockExclusive(&shop_lock);
            shop_failed = 1;
            ShopMessage(mod, "Charge saved, but the game wallet update failed verification. Shop stopped; restart to resync.");
            return 0;
        }
        AcquireSRWLockExclusive(&shop_lock);
        wallet = after;
        charge.state = CHARGE_IDLE;
        ReleaseSRWLockExclusive(&shop_lock);
        api->log(mod, "Purchase saved: %s (%u Requisition; %u remaining in game)",
            kLoadoutPrices[bought].name, price, after);
        ShopMessage(mod, "Purchase saved. One-mission access ready.");
    }
    else if (state == CHARGE_REFUSED || state == CHARGE_UNKNOWN)
    {
        char error[sizeof charge.error];
        AcquireSRWLockExclusive(&shop_lock);
        snprintf(error, sizeof error, "%s", charge.error);
        if (state == CHARGE_REFUSED) charge.state = CHARGE_IDLE;
        ReleaseSRWLockExclusive(&shop_lock);
        if (state == CHARGE_UNKNOWN)
            ShopMessage(mod, "Purchase outcome unknown. Shop disabled; check the saved profile before restarting.");
        else if (error[0]) ShopMessage(mod, error);
    }
    return 1;
}
