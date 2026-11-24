/* Build 25480438: 0x189C250 queries this button before validating slots or changing readiness.
 * The local loadout card is [[game + 0x347CE38] + 0xB0] + 0x53A78; Ready is at card + 0x28B0.
 * Only intercept that widget, never the other buttons using the shared input function. */
enum {
    READY_INPUT = 0x1891850,
    READY_BUTTON = 0x28B0
};

static HfMod* ready_mod;
static int (*ready_input_original)(uintptr_t button, uintptr_t input);

static int ReadReadyLoadout(HfMod* mod, uintptr_t card, uint64_t* selected)
{
    uint64_t equipped = 0;
    int valid = ReadRestrictedItems(HF_API(mod), ReadCardLoadout(mod, card), &equipped), solo = DestroyerSolo(mod);
    AcquireSRWLockExclusive(&shop_lock);
    checkout_equipped = equipped;
    checkout_valid = valid;
    checkout_solo = solo;
    if (selected) *selected = WithExtraAccess(equipped, solo);
    ReleaseSRWLockExclusive(&shop_lock);
    if (!valid) ShopMessage(mod, "Ready blocked: could not read the current local loadout.");
    return valid;
}

static void RequestCheckout(HfMod* mod, uintptr_t card)
{
    if (!ReadReadyLoadout(mod, card, NULL)) return;
    RefreshExtraOptions(mod);
    ShopMessage(mod, "Review your loadout costs before readying up.");
    InterlockedCompareExchange(&checkout_gate, CHECKOUT_REQUESTED, CHECKOUT_IDLE);
}

static int ResumeReady(HfMod* mod, uintptr_t card)
{
    uint64_t selected;
    if (!ReadReadyLoadout(mod, card, &selected)) return 0;
    AcquireSRWLockShared(&shop_lock);
    unsigned total;
    int ready = selected == ready_selection && !UnpaidItems(selected, &total) && !upgrade_selected;
    ReleaseSRWLockShared(&shop_lock);
    if (!ready) ShopMessage(mod, "Automatic Ready cancelled: review the current loadout costs.");
    return ready;
}

static int ReadyInput(uintptr_t button, uintptr_t input)
{
    int activated = ready_input_original(button, input);
    if (activated != 1 && CheckoutGate() != CHECKOUT_RESUME) return activated;
    const HfApi* api = HF_API(ready_mod);
    if (api->game_state(ready_mod) != HF_STATE_SHIP) return activated;
    uintptr_t menu = BriefingMenu(api, game_base);
    if (!menu)
    {
        SmrFinishCheckout(0);
        return activated;
    }
    uintptr_t screen = Pointer(api, menu + READY_SCREEN);
    if (!screen || button != screen + READY_CARD + READY_BUTTON) return activated;
    if (InterlockedCompareExchange(&checkout_gate, CHECKOUT_IDLE, CHECKOUT_RESUME) == CHECKOUT_RESUME)
        /* Re-enter the normal Ready path once; the game still checks missing stratagem slots. */
        return ResumeReady(ready_mod, screen + READY_CARD);
    if (activated != 1) return activated;
    if (CheckoutGate() == CHECKOUT_IDLE)
        RequestCheckout(ready_mod, screen + READY_CARD);
    return 0;
}

static int InstallReadyGate(HfMod* mod)
{
    const HfApi* api = HF_API(mod);
    if (ready_input_original) return 1;
    if (!api->hook ||
        !api->code_matches(game_base + READY_INPUT,
            "40 53 48 83 EC 20 83 B9 84 2F 00 00 01 48 8B DA 4C 8B D9 0F 84 A7 00 00 00") ||
        !api->code_matches(game_base + 0x189C27F,
            "48 81 C1 B0 28 00 00 E8 C5 55 FF FF 83 F8 01") ||
        !api->code_matches(game_base + 0x146DA08,
            "48 8D 8B 78 3A 05 00 49 8B D7 E8 39 E8 42 00"))
    {
        ShopMessage(mod, "Shop unavailable: Ready validation code does not match the reviewed build.");
        return 0;
    }
    ready_mod = mod;
    if (api->hook(mod, game_base + READY_INPUT, (void*)ReadyInput, (void**)&ready_input_original))
    {
        ShopMessage(mod, "Shop unavailable: could not install the Ready access check.");
        return 0;
    }
    api->log(mod, "Ready access check installed (local loadout Ready input only).");
    return 1;
}
