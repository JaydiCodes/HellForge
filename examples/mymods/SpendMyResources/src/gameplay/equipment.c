static unsigned equipment_applied[EQUIPMENT_GROUP_COUNT];
static int equipment_config_loaded;

static int EquipmentSelectionOpen(void)
{
    return equipment_config_loaded && checkout_valid && upgrade_funds_ready && !shop_failed && TierSelectionOpen();
}

SmrEquipmentStatus SmrEquipment(void)
{
    SmrEquipmentStatus status = {0};
    AcquireSRWLockShared(&shop_lock);
    for (size_t i = 0; i < EQUIPMENT_GROUP_COUNT; ++i)
    {
        const SmrEquipmentGroup* group = &kEquipmentGroups[i];
        if (group->primary != checkout_primary) continue;
        status.tiers[group->tier] = (SmrEquipmentTierStatus){group, group->price,
            equipment_selected[i], equipment_purchased[i], EquipmentSelectionOpen() && !equipment_purchased[i]};
    }
    memcpy(status.balance, currency_amount, sizeof status.balance);
    status.configured = equipment_config_loaded;
    ReleaseSRWLockShared(&shop_lock);
    return status;
}

int SmrSelectEquipment(HfMod* mod, const SmrEquipmentGroup* group, unsigned choice)
{
    int allowed = 0, on_ship = HF_API(mod)->game_state(mod) == HF_STATE_SHIP;
    AcquireSRWLockExclusive(&shop_lock);
    for (size_t i = 0; on_ship && EquipmentSelectionOpen() && i < EQUIPMENT_GROUP_COUNT; ++i)
        if (group == &kEquipmentGroups[i] && checkout_primary == group->primary && !equipment_purchased[i] &&
            choice <= group->count)
        {
            equipment_selected[i] = choice;
            allowed = 1;
            break;
        }
    ReleaseSRWLockExclusive(&shop_lock);
    if (!allowed) ShopMessage(mod, "Equipment upgrade selection unavailable for the current loadout.");
    return allowed;
}

static int CanApplyEquipment(HfMod* mod, uint32_t primary, const unsigned picks[EQUIPMENT_GROUP_COUNT])
{
    for (size_t i = 0; i < EQUIPMENT_GROUP_COUNT; ++i)
    {
        if (!picks[i]) continue;
        const SmrEquipmentGroup* group = &kEquipmentGroups[i];
        if (!equipment_config_loaded || primary != group->primary || picks[i] > group->count) return 0;
        const SmrEquipmentChoice* choice = &group->choices[picks[i] - 1];
        HfLiveValue now;
        for (size_t j = 0; j < choice->edit_count; ++j)
            if (!HF_API(mod)->get_live(mod, choice->edits[j].target, &choice->edits[j].field, &now)) return 0;
    }
    return 1;
}

static void UndoEquipmentEdits(HfMod* mod, const SmrEquipmentChoice* choice, size_t count)
{
    while (count)
    {
        const EquipmentEdit* edit = &choice->edits[--count];
        HF_API(mod)->set_live(mod, edit->target, &edit->field, NULL, 0);
    }
}

static void ApplyEquipment(HfMod* mod, uint32_t primary)
{
    unsigned picks[EQUIPMENT_GROUP_COUNT];
    AcquireSRWLockShared(&shop_lock);
    memcpy(picks, equipment_purchased, sizeof picks);
    ReleaseSRWLockShared(&shop_lock);
    for (size_t i = 0; i < EQUIPMENT_GROUP_COUNT; ++i)
    {
        const SmrEquipmentGroup* group = &kEquipmentGroups[i];
        if (!picks[i] || group->primary != primary || equipment_applied[i]) continue;
        const SmrEquipmentChoice* choice = &group->choices[picks[i] - 1];
        size_t j = 0;
        for (; j < choice->edit_count; ++j)
        {
            const EquipmentEdit* edit = &choice->edits[j];
            HfValue value = {edit->field.type, edit->value};
            if (!HF_API(mod)->set_live(mod, edit->target, &edit->field, &value, 0)) break;
        }
        if (j == choice->edit_count)
        {
            equipment_applied[i] = picks[i];
            HF_API(mod)->log(mod, "Equipment upgrade active: %s - %s.", group->weapon, choice->title);
        }
        else
        {
            UndoEquipmentEdits(mod, choice, j);
            ShopMessage(mod, "Equipment upgrade could not be applied; purchase retained for a later mission.");
        }
    }
}

static void FinishEquipment(HfMod* mod)
{
    for (size_t i = 0; i < EQUIPMENT_GROUP_COUNT; ++i)
    {
        if (!equipment_applied[i]) continue;
        const SmrEquipmentChoice* choice = &kEquipmentGroups[i].choices[equipment_applied[i] - 1];
        UndoEquipmentEdits(mod, choice, choice->edit_count);
        AcquireSRWLockExclusive(&shop_lock);
        equipment_purchased[i] = equipment_applied[i] = 0;
        ReleaseSRWLockExclusive(&shop_lock);
    }
}
