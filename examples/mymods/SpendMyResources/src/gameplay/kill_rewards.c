/* Kill rewards: every machine tallies the whole team's enemy kills and pays only its own player's share, so co-op
 * needs no messages between players. All of this runs on the game thread: kill, spawn and state events, and ShopTick. */
typedef struct SmrEnemy { uint64_t resource; const char* name; } SmrEnemy;
static const SmrEnemy kEnemies[] = {
#include "smr_enemies.inc"
};
enum { ENEMY_COUNT = sizeof kEnemies / sizeof kEnemies[0] };
static unsigned enemy_value[ENEMY_COUNT];
static int kill_rewards_ready;
static HfFn_EntityDescriptor entity_descriptor;

/* Victims seen this mission, keyed by entity so each pays once. Kept below 3/4 full for short probes. */
enum { KILL_SLOTS = 32768, KILL_LIMIT = KILL_SLOTS / 4 * 3 };
typedef struct KillRecord { uint32_t victim, amount; uint64_t creditor; } KillRecord;
static KillRecord kill_records[KILL_SLOTS];
static unsigned char kill_used[KILL_SLOTS];
static size_t kill_count;
static int kill_overflow;

/* An enemy's type when its descriptor is already gone by the time the kill is delivered. */
enum { SPAWN_SLOTS = 4096 };
static struct { uint32_t entity; uint64_t resource; } spawn_cache[SPAWN_SLOTS];

static unsigned mission_players;
static uint64_t kill_reward_due;
static uint64_t kill_team, kill_mine;
static size_t kill_mine_count;
static SmrKillNotice kill_notice; /* read by the overlay under shop_lock */
static SmrKillSummary kill_summary;

/* The mission's outcome comes from its telemetry, which may be sent after the mission state ends. A settled
 * mission's reward therefore waits for the ship, or the next mission, before it is due. */
static SmrKillSummary kill_settled;
static int kill_settled_waiting;
static volatile LONG mission_abandoned;
static HfMod* telemetry_mod;
static HfFn_SendMissionEndTelemetry send_mission_end;

typedef struct SmrKillPayout { uint64_t team, mine, share, bonus; unsigned players; } SmrKillPayout;

static SmrKillPayout KillPayout(uint64_t team, uint64_t mine, unsigned players)
{
    SmrKillPayout payout = {team, mine, 0, 0, players ? players : 1};
    payout.share = (team + payout.players - 1) / payout.players;
    /* The bonus is 5% of the team total times this player's part of it, which is 5% of their own kills' value. */
    if (payout.players >= 2) payout.bonus = (mine * SMR_KILL_BONUS_PERCENT + 99) / 100;
    return payout;
}

static int EnemyIndex(uint64_t resource)
{
    size_t low = 0, high = ENEMY_COUNT;
    while (low < high)
    {
        size_t middle = low + (high - low) / 2;
        if (kEnemies[middle].resource < resource) low = middle + 1;
        else high = middle;
    }
    if (low < ENEMY_COUNT && kEnemies[low].resource == resource) return (int)low;
    for (size_t i = 0; i < modded_enemy_count; ++i)
        if (modded_enemies[i].resource == resource) return (int)(ENEMY_COUNT + i);
    return -1;
}

static unsigned EnemyValue(int index)
{
    return index < ENEMY_COUNT ? enemy_value[index] : modded_enemies[index - ENEMY_COUNT].value;
}

static void ResetKills(void)
{
    memset(kill_used, 0, sizeof kill_used);
    memset(spawn_cache, 0, sizeof spawn_cache);
    kill_count = 0;
    kill_overflow = 0;
    mission_players = 0;
    kill_team = kill_mine = 0;
    kill_mine_count = 0;
}

void SmrLastKill(SmrKillNotice* out)
{
    AcquireSRWLockShared(&shop_lock);
    *out = kill_notice;
    ReleaseSRWLockShared(&shop_lock);
}

void SmrLastKillSummary(SmrKillSummary* out)
{
    AcquireSRWLockShared(&shop_lock);
    *out = kill_summary;
    ReleaseSRWLockShared(&shop_lock);
}

/* The player chose to leave ("abandoned"), or the host did ("host_abandoned"). */
static int AbandonedOutcome(const char* outcome)
{
    return !strcmp(outcome, "abandoned") || !strcmp(outcome, "host_abandoned");
}

static void MissionEndTelemetry(uint64_t player, const uint8_t* outcome, uint8_t r8, uint8_t r9)
{
    const HfApi* api = HF_API(telemetry_mod);
    char name[32] = "";
    /* Read a byte at a time: a short name can end just before memory that cannot be read. */
    for (size_t i = 0; outcome && i + 1 < sizeof name; ++i)
        if (api->read((uintptr_t)(outcome + i), &name[i], 1) || !name[i])
        {
            name[i] = '\0';
            break;
        }
    InterlockedExchange(&mission_abandoned, AbandonedOutcome(name));
    api->log(telemetry_mod, "Mission outcome: %s.", name[0] ? name : "(unreadable)");
    if (send_mission_end) send_mission_end(player, outcome, r8, r9);
}

static int InitKillRewards(HfMod* mod)
{
    const HfApi* api = HF_API(mod);
    const char* names[ENEMY_COUNT];
    char detail[256] = "";
    for (size_t i = 0; i < ENEMY_COUNT; ++i) names[i] = kEnemies[i].name;
    if (!SmrLoadKillRewards(names, enemy_value, ENEMY_COUNT, detail, sizeof detail))
    {
        api->log(mod, "Cannot load kill-rewards.json: %s Kill rewards disabled.", detail);
        return 0;
    }
    if (hf_has_functions_(mod) && api->function) entity_descriptor = hf_fn_EntityDescriptor(mod);
    if (!entity_descriptor) api->log(mod, "EntityDescriptor unavailable; kill rewards identify enemies from spawns only.");
    telemetry_mod = mod;
    if (!hf_has_functions_(mod) || !api->hook_function ||
        hf_hook_SendMissionEndTelemetry(mod, MissionEndTelemetry, &send_mission_end, 0))
        api->log(mod, "SendMissionEndTelemetry hook refused; abandoned missions pay their full kill reward.");
    api->log(mod, "Loaded kill-rewards.json (%u enemies).", (unsigned)(ENEMY_COUNT + modded_enemy_count));
    return 1;
}

static unsigned SessionPlayers(HfMod* mod)
{
    const HfPlayerManager* players = hf_global_PlayerManager(mod);
    uint32_t count = 0;
    if (!players || HF_API(mod)->read((uintptr_t)players + offsetof(HfPlayerManager, count), &count, sizeof count) ||
        count > 4)
        return 0;
    return count;
}

static uint64_t VictimResource(HfMod* mod, uint32_t victim)
{
    uint64_t resource = 0;
    uint8_t* descriptor = entity_descriptor ? entity_descriptor(victim) : NULL;
    if (descriptor) return HF_API(mod)->read((uintptr_t)descriptor, &resource, sizeof resource) ? 0 : resource;
    size_t slot = victim & (SPAWN_SLOTS - 1);
    return spawn_cache[slot].entity == victim ? spawn_cache[slot].resource : 0;
}

static void EnemySpawned(HfMod* mod, const HfSpawnEvent* event, void* user)
{
    if (!kill_rewards_ready || EnemyIndex(event->resource) < 0) return;
    size_t slot = event->entity & (SPAWN_SLOTS - 1);
    spawn_cache[slot].entity = event->entity;
    spawn_cache[slot].resource = event->resource;
}

static uint64_t PayoutTotal(uint64_t team, uint64_t mine, unsigned players)
{
    SmrKillPayout payout = KillPayout(team, mine, players);
    return payout.share + payout.bonus;
}

static unsigned Capped(uint64_t amount, unsigned cap)
{
    uint64_t limit = cap ? cap : UINT32_MAX;
    return (unsigned)(amount < limit ? amount : limit);
}

/* Every counted kill, the local player's or a teammate's, changes this player's share, so each one is shown. */
static void PublishKill(HfMod* mod, uint64_t creditor, unsigned amount)
{
    unsigned players = mission_players ? mission_players : SessionPlayers(mod);
    uint64_t before = PayoutTotal(kill_team, kill_mine, players);
    uint64_t peer = LocalPeer(mod);
    kill_team += amount;
    if (peer && creditor == peer)
    {
        kill_mine += amount;
        ++kill_mine_count;
    }
    uint64_t after = PayoutTotal(kill_team, kill_mine, players);
    AcquireSRWLockExclusive(&shop_lock);
    uint64_t base = (uint64_t)wallet + kill_reward_due;
    unsigned cap = currency_cap[REQUISITION_SLIPS];
    kill_notice.base = Capped(base, cap);
    kill_notice.before = Capped(base + before, cap);
    kill_notice.after = Capped(base + after, cap);
    if (!++kill_notice.sequence) kill_notice.sequence = 1;
    ReleaseSRWLockExclusive(&shop_lock);
}

static void EnemyKilled(HfMod* mod, const HfKillEvent* event, void* user)
{
    if (!kill_rewards_ready || !event->creditor || HF_API(mod)->game_state(mod) != HF_STATE_MISSION) return;
    int enemy = EnemyIndex(VictimResource(mod, event->victim));
    if (enemy < 0 || !EnemyValue(enemy)) return;
    size_t slot = (event->victim * 2654435761u) & (KILL_SLOTS - 1);
    while (kill_used[slot])
    {
        if (kill_records[slot].victim == event->victim) return;
        slot = (slot + 1) & (KILL_SLOTS - 1);
    }
    if (kill_count >= KILL_LIMIT)
    {
        if (!kill_overflow) HF_API(mod)->log(mod, "Kill rewards: %u kills recorded; later kills this mission are not counted.",
            (unsigned)kill_count);
        kill_overflow = 1;
        return;
    }
    kill_used[slot] = 1;
    kill_records[slot] = (KillRecord){event->victim, EnemyValue(enemy), event->creditor};
    ++kill_count;
    PublishKill(mod, event->creditor, EnemyValue(enemy));
}

/* Mission end: the players still in the session split the team's total; the leaving state may have emptied the list. */
static void SettleKills(HfMod* mod)
{
    if (!kill_rewards_ready) return;
    unsigned players = SessionPlayers(mod);
    if (!players) players = mission_players;
    if (!players && kill_count) HF_API(mod)->log(mod, "Kill rewards: player count unknown; paying the whole team total.");
    SmrKillPayout payout = KillPayout(kill_team, kill_mine, players);
    kill_settled = (SmrKillSummary){0, (unsigned)kill_count, (unsigned)kill_mine_count, payout.players,
        payout.team, payout.mine, payout.share, payout.bonus, payout.share + payout.bonus, 0};
    kill_settled_waiting = 1;
    ResetKills();
}

/* An abandoned mission pays SMR_KILL_ABANDON_PERCENT of its reward; any other outcome pays it all. */
static void FinishKills(HfMod* mod)
{
    const int abandoned = InterlockedExchange(&mission_abandoned, 0) != 0;
    if (!kill_settled_waiting) return;
    kill_settled_waiting = 0;
    SmrKillSummary summary = kill_settled;
    summary.abandoned = abandoned;
    if (abandoned) summary.earned = summary.earned * SMR_KILL_ABANDON_PERCENT / 100;
    kill_reward_due += summary.earned;
    AcquireSRWLockExclusive(&shop_lock);
    summary.sequence = kill_summary.sequence + 1;
    if (!summary.sequence) summary.sequence = 1;
    kill_summary = summary;
    ReleaseSRWLockExclusive(&shop_lock);
    if (!summary.team_kills) return;
    char penalty[64] = "";
    if (abandoned)
        snprintf(penalty, sizeof penalty, "; mission abandoned, %u%% paid", (unsigned)SMR_KILL_ABANDON_PERCENT);
    HF_API(mod)->log(mod, "Kill rewards: team %u kills worth %llu Req, split %u ways = %llu; your %u kills worth %llu "
        "earn a %llu Req team bonus%s. %llu Req is paid on the ship.", summary.team_kills,
        (unsigned long long)summary.team, summary.players, (unsigned long long)summary.share, summary.own_kills,
        (unsigned long long)summary.mine, (unsigned long long)summary.bonus, penalty,
        (unsigned long long)summary.earned);
    char message[192];
    snprintf(message, sizeof message, "Mission kills: %u team kills, %llu Req share + %llu Req bonus%s.",
        summary.team_kills, (unsigned long long)summary.share, (unsigned long long)summary.bonus, penalty);
    ShopMessage(mod, message);
}

static void KillsTick(HfMod* mod)
{
    if (!kill_rewards_ready) return;
    const uint32_t state = HF_API(mod)->game_state(mod);
    if (state == HF_STATE_SHIP && kill_settled_waiting) FinishKills(mod);
    if (state != HF_STATE_MISSION) return;
    unsigned players = SessionPlayers(mod);
    if (players) mission_players = players;
}

/* Paid like an exchange's credit: added to the live inventory, verified, then saved to the wallet. */
static void ProcessKillReward(HfMod* mod, uintptr_t manager)
{
    const HfApi* api = HF_API(mod);
    if (!kill_reward_due || !manager || api->game_state(mod) != HF_STATE_SHIP) return;
    AcquireSRWLockShared(&shop_lock);
    int idle = exchange_ready && !shop_failed && !exchange.pending && charge.state == CHARGE_IDLE;
    unsigned cap = currency_cap[REQUISITION_SLIPS];
    ReleaseSRWLockShared(&shop_lock);
    unsigned before, after;
    if (!idle || !ItemRecord(api, manager, REQUISITION, &before)) return;
    uint64_t room = cap ? (before < cap ? cap - before : 0) : UINT32_MAX - before;
    unsigned credit = (unsigned)(kill_reward_due < room ? kill_reward_due : room);
    uint64_t forfeited = kill_reward_due - credit;
    kill_reward_due = 0;
    char detail[128] = "", message[192];
    if (credit)
    {
        inventory_add(manager, REQUISITION, credit, 0, NULL, 0);
        if (!ItemRecord(api, manager, REQUISITION, &after) || after != before + credit)
            snprintf(detail, sizeof detail, "Runtime addition failed verification; ledger unchanged.");
        else if (SaveTransaction(api, manager, detail, sizeof detail))
        {
            AcquireSRWLockExclusive(&shop_lock);
            wallet = currency_amount[REQUISITION_SLIPS] = after;
            ReleaseSRWLockExclusive(&shop_lock);
            detail[0] = '\0';
        }
        if (detail[0])
        {
            snprintf(message, sizeof message, "Kill reward stopped. %s Shop disabled; do not retry.", detail);
            StopShop(mod, message);
            return;
        }
    }
    if (forfeited)
        snprintf(message, sizeof message, "Kill reward: %u Req added; %llu Req over the cap forfeited.", credit,
            (unsigned long long)forfeited);
    else
        snprintf(message, sizeof message, "Kill reward: %u Req added.", credit);
    ShopMessage(mod, message);
}
