/* Build 25480438: the committed PlayerHistory loadout, as reviewed in native/mod-loader/loadout_scrub.c. */
enum {
    HISTORY_CELL = 0x347CE50,
    HISTORY_COUNT = 0x2D200,
    RECORD_STRIDE = 0x1690,
    LOADOUT = 0x38,
    WEAPON_STRIDE = 0x60,
    STRATAGEM_SLOTS = 0x188,
    STRATAGEM_STRIDE = 0x30,
    STRATAGEM_COUNT = 0x788,
    MAX_RECORDS = 32,
    MAX_STRATAGEMS = 32
};

static uintptr_t LocalLoadout(const HfApi* api, uintptr_t game, uint64_t peer)
{
    uintptr_t history;
    uint32_t count;
    if (!peer || api->read(game + HISTORY_CELL, &history, sizeof history) || !history ||
        api->read(history + HISTORY_COUNT, &count, sizeof count) || count > MAX_RECORDS)
        return 0;

    for (uint32_t i = 0; i < count; ++i)
    {
        uintptr_t record = history + (uintptr_t)i * RECORD_STRIDE;
        uint64_t record_peer;
        if (!api->read(record, &record_peer, sizeof record_peer) && record_peer == peer)
            return record + LOADOUT;
    }
    return 0;
}
