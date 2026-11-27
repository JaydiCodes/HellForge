#pragma once
#include "../shop/shop.h"

#define DIRECT(target) \
    {target, HF_FIELD(DamageInfo, standard_damage), "damage.standard_damage", "Direct damage"}, \
    {target, HF_FIELD(DamageInfo, durable_damage), "damage.durable_damage", "Durable damage"}, \
    {target, HF_FIELD_AT(DamageInfo, armor_penetration, 0), "damage.ap_direct", "Direct penetration"}
#define HYENA \
    DIRECT(HF_DAMAGE_R4_HYENA), \
    {HF_DAMAGE_R4_HYENA, HF_FIELD_AT(DamageInfo, armor_penetration, 1), "damage.ap_slight", "Slight-angle penetration"}
#define ERUPTOR_BLAST \
    {HF_DAMAGE_R36_ERUPTOR_IMPACT, HF_FIELD(DamageInfo, standard_damage), "explosion.damage.standard_damage", "Blast damage"}, \
    {HF_DAMAGE_R36_ERUPTOR_IMPACT, HF_FIELD(DamageInfo, durable_damage), "explosion.damage.durable_damage", "Blast durable damage"}, \
    {HF_DAMAGE_R36_ERUPTOR_IMPACT, HF_FIELD_AT(DamageInfo, armor_penetration, 1), "explosion.damage.ap_slight", "Blast slight-angle penetration"}, \
    {HF_DAMAGE_R36_ERUPTOR_IMPACT, HF_FIELD_AT(DamageInfo, armor_penetration, 2), "explosion.damage.ap_large", "Blast large-angle penetration"}, \
    {HF_DAMAGE_R36_ERUPTOR_IMPACT, HF_FIELD_AT(DamageInfo, armor_penetration, 3), "explosion.damage.ap_extreme", "Blast extreme-angle penetration"}, \
    {HF_DAMAGE_R36_ERUPTOR_IMPACT, HF_FIELD(DamageInfo, demolition), "explosion.damage.demolition", "Blast demolition"}, \
    {HF_DAMAGE_R36_ERUPTOR_IMPACT, HF_FIELD(DamageInfo, stagger), "explosion.damage.stagger", "Blast stagger"}, \
    {HF_EXPLOSION_R36_ERUPTOR_IMPACT, HF_FIELD(ExplosionSettings, inner_radius), "explosion.inner_radius", "Blast inner radius (m)"}, \
    {HF_EXPLOSION_R36_ERUPTOR_IMPACT, HF_FIELD(ExplosionSettings, outer_radius), "explosion.outer_radius", "Blast outer radius (m)"}, \
    {HF_EXPLOSION_R36_ERUPTOR_IMPACT, HF_FIELD(ExplosionSettings, shockwave_radius), "explosion.shockwave_radius", "Blast shockwave radius (m)"}, \
    {HF_EXPLOSION_R36_ERUPTOR_IMPACT, HF_FIELD(ExplosionSettings, shrapnel_count), "explosion.shrapnel_count", "Shrapnel count"}

static EquipmentEdit kFdi[] = {HYENA};
static EquipmentEdit kApi[] = {HYENA};
static EquipmentEdit kHe[] = {DIRECT(HF_DAMAGE_R36_ERUPTOR), ERUPTOR_BLAST};
static EquipmentEdit kAphe[] = {DIRECT(HF_DAMAGE_R36_ERUPTOR), ERUPTOR_BLAST};
static EquipmentEdit kApfsds[] = {DIRECT(HF_DAMAGE_R36_ERUPTOR), ERUPTOR_BLAST};
#undef ERUPTOR_BLAST
#undef HYENA
#undef DIRECT

#define WITH_COUNT(array) array, sizeof(array) / sizeof((array)[0])
static const SmrEquipmentChoice kHyenaSuper[] = {
    {"FDI", WITH_COUNT(kFdi)}, {"API", WITH_COUNT(kApi)},
};
static const SmrEquipmentChoice kEruptorSuper[] = {
    {"HE", WITH_COUNT(kHe)}, {"APHE", WITH_COUNT(kAphe)}, {"APFSDS", WITH_COUNT(kApfsds)},
};
static SmrEquipmentGroup kEquipmentGroups[] = {
    {HF_ITEM_R36_ERUPTOR, "R-36 Eruptor", SMR_TIER_SUPER, WITH_COUNT(kEruptorSuper)},
    {HF_ITEM_R4_HYENA, "R-4 Hyena", SMR_TIER_SUPER, WITH_COUNT(kHyenaSuper)},
};
#undef WITH_COUNT
enum { EQUIPMENT_GROUP_COUNT = sizeof kEquipmentGroups / sizeof kEquipmentGroups[0] };
