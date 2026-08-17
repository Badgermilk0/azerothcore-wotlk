# Creature scaling (fork feature)

Per-**content-tier**, per-**level** multipliers for creature health, melee damage and spell damage.
This is a fork addition, not upstream AzerothCore.

## Why it exists

The server runs `mod-individual-progression`, so characters clear Vanilla → TBC → WotLK content while
always having WotLK talents and abilities. Vanilla and TBC content therefore needs to be scaled up.

Scaling by creature level alone cannot do that, because expansions overlap in level:

| Level | Vanilla/TBC content | WotLK-era content |
|---|---|---|
| 60-63 | MC / BWL / AQ / Naxx40 bosses | Hellfire Peninsula, Ramparts, Blood Furnace |
| 70-73 | Karazhan, Black Temple, Sunwell, Zul'Aman | Borean Tundra, Howling Fjord, Utgarde Keep, Nexus |

Buffing "level 70" to make Illidan hard also buffs every Northrend levelling mob — which is exactly
where WotLK talents and gear finally match the content and no scaling is wanted.

## How the tier is decided

From the creature's own `creature_template.exp` column: `0` = Vanilla, `1` = TBC, `2` = WotLK.

That column already selects which `CreatureBaseStats::BaseHealth[]` / `BaseDamage[]` entry a creature
uses (`CreatureData.h`), so it already means "which expansion's power curve is this creature on" and
the world DB keeps it accurate. It is per-creature, which is what makes reused maps work: Naxx40's
creatures (`mod-individual-progression`, entries `351000+`) are `exp = 0` while WotLK Naxxramas is
`exp = 2`, even though both live on map 533.

Resolution order (`CreatureScalingMgr::ResolveTier`), first match wins:

1. `Rate.Creature.Tier.EntryOverrides` — `creatureEntry:tier` pairs, for surgical fixes.
2. `Rate.Creature.Tier.ZoneOverrides` — `zoneId:tier` pairs.
3. `CreatureTemplate::expansion`.
4. Fallback `CONTENT_TIER_WOTLK` — unknown content is left unscaled rather than buffed by accident.

Known quirk: the Blood Elf and Draenei starting zones are on map 530 and their creatures are
`exp = 1`, so levels 1-20 there land in the **TBC** tier. Either tune the TBC tier's low levels to
match Vanilla's, or list those zones in `ZoneOverrides`.

## Layout

- `src/server/game/Scaling/CreatureScalingMgr.{h,cpp}` — `sCreatureScalingMgr` singleton, the
  `ContentTier` / `CreatureScalingStat` enums, config loading, tier resolution, and the rate table.
  Deliberately self-contained so it does not add merge surface against upstream.
- Config keys: `Rate.Creature.Tier.<Vanilla|TBC|WotLK>.Level.<1..83|84Plus>.<HP|Damage|SpellDamage>`,
  documented in `src/server/apps/worldserver/worldserver.conf.dist`. Built in a loop, so there is no
  enum and no per-key registration block.
- Rates live in a flat `_rate[tier][level][stat]` array. Index directly; do not add branching, the
  damage rates are read on every swing and every cast.

## Rules when touching this

- **A missing key falls back to the legacy `Rate.Creature.Level.<band>.<stat>` value.** Those 19-band
  keys are retained purely as that default so an existing `worldserver.conf` keeps its tuning. Do not
  delete them, and do not reintroduce them into the `Rates` enum in `WorldConfig.h` — they are read by
  string in `LoadRates()` precisely so `WorldConfig.{h,cpp}` stay identical to upstream.
- **Always pass `showLogs = false` to `sConfigMgr->GetOption`** here. ~750 keys are normally absent;
  the default would emit a "Missing property" warning for each one on every boot and `.reload config`.
- **The tier is cached per spawn** in `Creature::_contentTier`, resolved in `Creature::UpdateEntry()`
  *before* its `SelectLevel()` call (which needs it for health). `SetMap()` and `Relocate()` both run
  before `CreateFromProto()` → `UpdateEntry()`, so map and position are valid there. A creature that
  walks across a `ZoneOverrides` boundary keeps its spawn-time tier; that is intended.
- The zone lookup is a terrain query, so it is guarded by `HasZoneOverrides()`.
- **Player pets, guardians and mind-controlled units are never scaled.** `Creature::CalculateMinMaxDamage`
  is inherited by `Pet`/`Guardian`/`Totem`, so the melee site in `StatSystem.cpp` carries an explicit
  guard, and `Pet.cpp` uses the rank-only `_GetHealthMod(rank)`.

## Where each stat is applied

| Stat | Site | Timing |
|---|---|---|
| HP | `Creature::SelectLevel`, plus the `!m_regenHealth` path in `Creature::LoadFromDB` | at spawn — needs a respawn to take effect |
| Melee damage | `Creature::CalculateMinMaxDamage` (`StatSystem.cpp`) | live |
| Spell damage | `Creature::GetSpellDamageMod`, consumed by `Unit::SpellDamageBonusDone` | live |

The rank rates (`Rate.Creature.Normal.*`, `Rate.Creature.Elite.*`) are a separate, unchanged axis and
multiply on top. Note the melee rank rate is baked into `CreatureTemplate::DamageModifier` once at DB
load (`ObjectMgr.cpp`), so it must never be re-multiplied at runtime.

Creature DoTs need no separate handling: periodic ticks route through `SpellDamageBonusDone`
(`SpellAuraEffects.cpp`), so they already inherit the spell-damage tier rate.

## Interaction with mod-individual-progression

The module scales **player** output by the **player's** progression state; this subsystem scales
**creature** stats by the **content's** tier. Both apply and multiply. When tuning, remember
`VanillaPowerAdjustment` already ramps player damage down linearly from level 11 to 60, so Vanilla
creature HP does not need to carry the whole difficulty increase.

Core must **not** include the module's `IndividualProgression.h`: `game` does not link `modules`, so
that would be a layering inversion. Nothing here needs it — the tier comes from the world DB.
