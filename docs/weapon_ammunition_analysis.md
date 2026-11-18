# Weapon Programmable Ammunition Types - Code Analysis

## Overview

The HellForge codebase provides comprehensive documentation on how weapons are assigned their programmable ammunition types. This analysis covers the data structures, code mechanisms, and configuration files that handle this system.

---

## 1. Core Concepts

### Programmable Ammunition
**Programmable ammunition** (also called "programmable ammo" or "function ammo") refers to special ammunition types that certain weapons can fire when a function is activated (typically via a support/backpack item). The projectile fired replaces the weapon's default fired projectile at **ProjectileWeapon +0x240**.

### Weapons with Programmable Ammunition
According to `mappings/references.json`, only **4 weapons** currently support programmable ammunition:
1. **AC-8 Autocannon** - fires projectile type 284
2. **GR-8 Recoilless Rifle** - fires projectile type 36
3. **RL-77 Airburst Rocket Launcher** - fires projectile type 96
4. **StA-X3 W.A.S.P. Launcher** - fires projectile type 330

---

## 2. Data Structure Definitions

### 2.1 Programmable Ammunition Mapping (`references.json`)

Location: `mappings/references.json` → `"programmable"` section

```json
"programmable": {
  "AC-8 Autocannon": {
    "projectile": 284,
    "note": "The ProgrammableAmmo fire path (while the function is on, this projectile replaces the fired one) is proven, but giving this weapon a function projectile has not been gameplay-tested."
  },
  "GR-8 Recoilless Rifle": {
    "projectile": 36,
    "note": "..."
  },
  // ... other weapons
}
```

**Structure:**
- **Key**: Weapon name (e.g., "AC-8 Autocannon")
- **projectile**: The projectile type ID that this weapon fires when programmable ammo is active
- **note**: Additional documentation about the ammunition and any caveats

---

## 3. Code Implementation

### 3.1 Schema Loading (`Stats.cs`)

**File**: `src/HellForge.Core/Stats.cs`

The `References()` function loads programmable ammunition definitions:

```csharp
static void References(StatsSchema schema)
{
    using var stream = typeof(StatsSchema).Assembly.GetManifestResourceStream("HellForge.Core.mappings.references.json")!;
    using var json = JsonDocument.Parse(stream);
    
    var programmable = json.RootElement.GetProperty("programmable");
    var ammunition = json.RootElement.GetProperty("ammunition").EnumerateArray()
        .ToDictionary(a => a.GetProperty("weapon").GetString()!, a => a.Clone());
    
    foreach (var (category, targets) in schema.Targets)
    {
        foreach (var (name, target) in targets)
        {
            if (category == "weapons")
            {
                ProjectileSwap(schema, name, target, hosts, ammunition);
                ProgrammableSwap(schema, name, target, programmable);  // ← Key function
            }
            // ...
        }
    }
}
```

**Key Points:**
1. Loads `references.json` from embedded resources
2. Extracts the `"programmable"` JSON object
3. Calls `ProgrammableSwap()` for each weapon

### 3.2 Programmable Ammunition Assignment (`Stats.cs`)

**Function**: `ProgrammableSwap()`

```csharp
static void ProgrammableSwap(StatsSchema schema, string name, StatsTarget target, JsonElement programmable)
{
    var fields = target.Fields;
    
    // Check if this weapon has programmable ammunition defined
    if (programmable.TryGetProperty(name, out _) &&
        fields.Values.Where(f => f.Kind == "component" && f.Component == "ProjectileWeaponComponentData")
            .OrderBy(f => f.Order).FirstOrDefault() is { } fed)
    {
        // Add "function_ammo.projectile" field to the target's editable fields
        AddField(target, "function_ammo.projectile", 
            new StatsField("Programmable ammo projectile", "component", fed.Component, fed.Resource, 
                fed.Record, 576, "u32",  // ← Offset 576 = ProjectileWeapon +0x240 (576 bytes)
                null, null, null, fed.Shared, true) 
            { Order = fields.Values.Where(f => f.Kind == "ProjectileSettings")
                .Select(f => f.Order).DefaultIfEmpty(0).Min() - 1 });
        
        var site = fields["function_ammo.projectile"];
        
        // Register this projectile swap with the schema
        schema.AddSwap(site, fed.Resource, "/f");  // "/f" = programmable round
    }
}
```

**What This Does:**
1. Checks if the weapon name exists in the `programmable` JSON object
2. Finds the weapon's `ProjectileWeaponComponentData` component
3. **Adds an editable field** `"function_ammo.projectile"` to the weapon's stats target
4. The field points to offset **576** (which is `ProjectileWeapon +0x240` in hex: 0x240 = 576 decimal)
5. Registers this as a **projectile swap** in the schema with tag `"/f"` (meaning "programmable/function round")

### 3.3 Editor Catalog Integration (`EditorCatalog.cs`)

**File**: `src/HellForge.Core/EditorCatalog.cs`

The editor catalog loads programmable ammunition metadata:

```csharp
static readonly Lazy<JsonNode> ReferenceData = new(() =>
{
    using var stream = typeof(EditorCatalog).Assembly.GetManifestResourceStream("HellForge.Core.mappings.references.json")!;
    return JsonNode.Parse(stream)!;
});
```

When displaying a weapon row in the editor, programmable ammunition is documented:

```csharp
if (row.Key == "function_ammo.projectile" && ReferenceData.Value["programmable"]![row.Part.Target] is { } programmed)
{
    json["note"] = "While programmable ammunition is on, this projectile replaces the fired one. " 
        + programmed["note"]!.GetValue<string>();
}
```

### 3.4 Multiplayer Coordination (`multiplayer.md`)

**Documentation Reference**: `docs/multiplayer.md`

```
Where each weapon's projectile swap is written, by its edit's site (SwapSite): 
the weapons whose shots it changes (an ammunition every weapon equipping it shares: several) 
and how the loader finds that weapon's round in co-op (native/mod-loader/shots.c):

- "" : its ProjectileWeapon +0 (default)
- "/r" : its WeaponRounds +64 (rounds feed)
- "/f" : its programmable round (ProjectileWeapon +0x240, while its function is on)
- "/<type>" : the round's stock type (an ammunition's, a magazine pattern entry's, a charge or heat level's)
```

---

## 4. Memory Layout

### ProjectileWeaponComponentData Structure

The `ProjectileWeaponComponentData` component contains:

| Offset | Field | Purpose |
|--------|-------|---------|
| +0 | Main Projectile | The weapon's default fired projectile (u32) |
| ... | ... | (other fields) |
| +0x240 (576) | Programmable Projectile | The projectile fired when programmable ammo is active (u32) |

**Key Point**: The offset **576 bytes** (0x240 in hex) is hardcoded in the weapon assignment process and represents a fixed memory location in the game's weapon component data.

---

## 5. Data Flow

```
mappings/references.json
    ↓
    └─→ { "programmable": { "weapon_name": { "projectile": id, "note": "..." }, ... } }
           ↓
           Stats.cs :: References() reads this
           ↓
           Stats.cs :: ProgrammableSwap() checks weapon name
           ↓
           IF weapon found in "programmable":
              • Adds "function_ammo.projectile" field to weapon's editable stats
              • Points to ProjectileWeaponComponentData at offset +576 (0x240)
              • Registers projectile swap with tag "/f" (function/programmable)
           ↓
           EditorCatalog.cs loads and displays this in the editor UI
           ↓
           Editor shows "Programmable ammo projectile" as an editable field
           ↓
           User can change the projectile type
           ↓
           Stats file saved with the new projectile value at +0x240
           ↓
           Game loads the new value when weapon is equipped with function active
```

---

## 6. Configuration Process

### How to Add Programmable Ammunition to a Weapon

**Step 1**: Add weapon entry to `mappings/references.json` under `"programmable"`:

```json
"programmable": {
  "My New Weapon": {
    "projectile": 999,
    "note": "My weapon's programmable ammo fires projectile 999"
  }
}
```

**Step 2**: The code automatically:
1. Detects the weapon in the references data
2. Finds the weapon's `ProjectileWeaponComponentData` component
3. Creates an editable `"function_ammo.projectile"` field
4. Registers it in the stats schema

**Step 3**: User modifies through the editor:
- Opens weapon's stats page in the editor
- Finds "Programmable ammo projectile" row
- Selects a new projectile type
- Saves the stats file

**Step 4**: The mod loader applies the change:
- Reads the new projectile ID from the stats file
- Writes it to `ProjectileWeapon +0x240` in memory
- When the function is activated in-game, the weapon fires the new projectile

---

## 7. Supporting Data: Ammunition Types

### Regular Ammunition Mapping

Location: `mappings/references.json` → `"ammunition"` section

```json
"ammunition": [
  {
    "weapon": "AR-23 Liberator",
    "item": "RIFLE 5,5x50mm. FULL METAL JACKET",
    "resource": "0x7C76E2F0B044C6F4",
    "record": 60,
    "component": 321,
    "offset": 162676,
    "projectile": 276,
    "damage": 108,
    "optionalFor": []
  },
  // ... more ammunition entries
]
```

This maps default ammunition to weapons and their projectiles, used by the `ProjectileSwap()` function.

---

## 8. Key Findings Summary

✅ **The code DOES explain how programmable ammunition is assigned:**

1. **Data Source**: `mappings/references.json` contains the master list of which weapons have programmable ammunition

2. **Assignment Mechanism**: `Stats.cs::ProgrammableSwap()` function:
   - Checks if weapon is in the programmable list
   - Locates the weapon's `ProjectileWeaponComponentData` component
   - Adds an editable `"function_ammo.projectile"` field
   - Points to fixed memory offset **+576 bytes** (0x240)

3. **Editor Integration**: The field is exposed in `EditorCatalog.cs` with documentation from the JSON "note" field

4. **Memory Model**: Uses fixed offset within `ProjectileWeaponComponentData` structure

5. **Mod Loader**: `native/mod-loader/shots.c` applies the changes with tag `"/f"` for programmable rounds

6. **Co-op Synchronization**: Schema maintains projectile swap information for multiplayer consistency

---

## 9. Related Documentation Files

- `docs/sdk-reference.md` - Device reference for `weapon_function_projectile_type` at +0x240
- `docs/multiplayer.md` - Detailed explanation of projectile swap coordination in co-op
- `docs/internals.md` - Deep dive into ammunition and weapon component data
- `docs/modding/content.md` - Modding guide for weapon customization

---

## Conclusion

The HellForge codebase provides **complete and clear** documentation of how programmable ammunition types are assigned to weapons through:

1. **Configuration Data** in `mappings/references.json`
2. **Schema Loading** in `Stats.cs::References()` and `Stats.cs::ProgrammableSwap()`
3. **Editor Integration** in `EditorCatalog.cs`
4. **Memory Layout Specification** using fixed offset +576 (0x240) in `ProjectileWeaponComponentData`
5. **Extensive Documentation** in comments, docstrings, and markdown files
