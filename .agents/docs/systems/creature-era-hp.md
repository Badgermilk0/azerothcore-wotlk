# Creature era HP (fork feature)

A health multiplier per **era**, **content type**, **rank** and **level band**, so difficulty can be
tuned era by era alongside `mod-individual-progression`. It is a fork addition, not upstream
AzerothCore, and it scales **health only**. Melee and spell damage are stock.

```
health = base health * Rate.Creature.<rank>.HP              // upstream global rate, unchanged
                     * Rate.Creature.EraHP.<era>.<content>.<rank>[band]
```

It replaces the earlier content-tier system (`CreatureScalingMgr`, `Rate.Creature.Tier.*` and
`Rate.Creature.Level.*`). That system also scaled damage and spell damage, and it took the era from
`creature_template.exp`. All of it was removed, so those keys no longer do anything.

## Config

`worldserver.conf.dist`, section "ERA CREATURE HEALTH": 45 rows, `Rate.Creature.EraHP.<Vanilla|TBC|WotLK>.<World|Dungeon|Raid>.<Normal|Elite|RareElite|WorldBoss|Rare>`,
each holding 14 comma-separated values for the bands
`1-9, 10-19, 20-29, 30-39, 40-49, 50-59, 60, 61-63, 64-69, 70, 71-73, 74-79, 80, 81+`.
They ship at 1. A row that does not hold exactly 14 positive numbers logs an error and stays at 1.
`Rate.Creature.EraHP.EntryOverrides` (`entry:era,...`) forces the era of single creature entries.

## How a creature is classified (`CreatureEraHp::GetRate`)

| Axis | Source |
|---|---|
| Era | Entry override, else the map's `Map.dbc` expansion, with two exceptions: map 530 below level 58 is Vanilla (the Blood Elf / Draenei zones); Naxxramas 533 and Onyxia's Lair 249 are Vanilla at level 63 or less and WotLK above. |
| Content | `MapEntry::IsRaid()` -> Raid, `IsNonRaidDungeon()` -> Dungeon, else World. Battleground and arena maps return 1. |
| Rank | `creature_template.rank` of the spawned (difficulty) template. Rank 5 uses the Elite row, like `_GetHealthMod()`'s default. |
| Band | The level the creature rolled in `SelectLevel()`. |

### Why the map, not `creature_template.exp`

Checked against the live world DB on 2026-10-19: `exp` does not follow the era. About 4,700
Northrend spawns are exp 0, about 1,800 spawns at level 58+ on map 530 (Outland, SSC, Zul'Aman) are exp 0,
and the Blood Elf / Draenei zone mobs are exp 0. Naxxramas mixes exp 0, 1 and 2, and
mod-individual-progression's 40-man "Eye Stalk" (351090) is exp 2. The map's expansion is right for
every instance. The two maps that two eras share are told apart by level: their 40-man creatures are 60-63
and the WotLK ones 80-83. In `Map.dbc` Onyxia's Lair is expansion 0 and Naxxramas expansion 2, so
each needs the level rule in a different direction.

Side effects to know about: level 80+ faction leaders in Eastern Kingdoms and Kalimdor count as
Vanilla World (bands 80 / 81+), and Ebon Hold (609, expansion 2) counts as WotLK World.

## Where it is applied

| Site | Note |
|---|---|
| `Creature::SelectLevel` | Main site. Runs on spawn, respawn and `UpdateEntry`. |
| `Creature::LoadCreatureFromDB`, stored `curhealth` path | Keeps a saved current health proportional to the scaled max. |

Both use `GetRate(this, level)`. The table is reloaded by `World::LoadConfigSettings`, so
`.reload config` re-reads it, but creatures already spawned keep their health until they respawn.

## Rules when touching this

- **Never scale what a player owns.** `IsPet()` returns 1, and so does any `UNIT_MASK_MINION`
  (guardians, totems, puppets) whose `TempSummon::GetSummonerGUID()` is a player. The summoner
  is set in the `TempSummon` constructor, so it is known before the first `SelectLevel()`; the
  `UNIT_FIELD_SUMMONEDBY` owner field is not, so do not switch to `GetOwnerGUID()`.
- **Use `FindMap()`, not `GetMap()`.** If the map is unknown the rate is 1 rather than an assert.
- `GetRate` runs only at spawn, so it may branch freely. Keep it out of per-tick paths.
- Core must not include mod-individual-progression headers (`game` does not link `modules`).
  Nothing here needs them.

## Interaction with mod-individual-progression

The module scales **player** output by the player's progression (`VanillaPowerAdjustment`,
`TBCPowerAdjustment`, ...) and restores 1.12 creature stats in SQL. This table scales **creature**
health by the content's era. Both apply, and they multiply.
