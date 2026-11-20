/* Build 25480438: 0x189C250 queries this button before validating slots or changing readiness.
 * The local loadout card is [[game + 0x347CE38] + 0xB0] + 0x53A78; Ready is at card + 0x28B0.
 * Only intercept that widget, never the other buttons using the shared input function. */
enum {
    READY_INPUT = 0x1891850,
    READY_MENU = 0x347CE38,
    READY_SCREEN = 0xB0,
    READY_CARD = 0x53A78,
    READY_BUTTON = 0x28B0,
    READY_PEER = 0x1EE00
};

static HfMod* ready_mod;
static int (*ready_input_original)(uintptr_t button, uintptr_t input);

static int ReadyAccessAllowed(HfMod* mod, uintptr_t card)
{
    const HfApi* api = HF_API(mod);
    const HfLocalUser* local = hf_global_LocalUser(mod);
    uint64_t peer = 0, card_peer = 0;
    if (!local || api->read((uintptr_t)local + offsetof(HfLocalUser, peer), &peer, sizeof peer) ||
        api->read(card + READY_PEER, &card_peer, sizeof card_peer) || !peer || card_peer != peer)
    {
        ShopMessage(mod, "Ready blocked: could not verify the local loadout player.");
        return 0;
    }
    uintptr_t loadout = LocalLoadout(api, game_base, peer);
    unsigned selected = 0;
    if (!loadout || !SelectedItems(mod, &selected))
        goto unreadable;
    unsigned total;
    AcquireSRWLockExclusive(&shop_lock);
    checkout_mask = selected;
    checkout_valid = 1;
    unsigned unpaid = UnpaidItems(selected, &total);
    ReleaseSRWLockExclusive(&shop_lock);
    if (!unpaid) return 1;
    ShopMessage(mod, "Purchase access for the equipped items to ready automatically.");
    InterlockedExchange(&checkout_requested, 1);
    return 0;

unreadable:
    ShopMessage(mod, "Ready blocked: could not read the selected loadout.");
    return 0;
}

static int ReadyInput(uintptr_t button, uintptr_t input)
{
    int activated = ready_input_original(button, input);
    int automatic = InterlockedCompareExchange(&auto_ready, 0, 0) == 2;
    if (activated != 1 && !automatic) return activated;
    const HfApi* api = HF_API(ready_mod);
    uintptr_t menu = Pointer(api, game_base + READY_MENU);
    uint32_t top = 0;
    if (!menu || api->read(menu, &top, sizeof top) || top != 11)
    {
        InterlockedExchange(&auto_ready, 0);
        return activated;
    }
    uintptr_t screen = Pointer(api, menu + READY_SCREEN);
    if (!screen || button != screen + READY_CARD + READY_BUTTON) return activated;
    if (automatic)
    {
        InterlockedExchange(&auto_ready, 0);
        unsigned selected = 0;
        if (!SelectedItems(ready_mod, &selected) || selected != auto_ready_selection)
        {
            ShopMessage(ready_mod, "Automatic Ready cancelled: loadout changed or could not be read.");
            return activated == 1 ? 0 : activated;
        }
        /* Re-enter the normal Ready path once; the game still checks missing stratagem slots. */
        return ReadyAccessAllowed(ready_mod, screen + READY_CARD) ? 1 : 0;
    }
    return ReadyAccessAllowed(ready_mod, screen + READY_CARD) ? activated : 0;
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
