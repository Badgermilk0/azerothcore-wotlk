/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef AZEROTHCORE_CREATUREERAHP_H
#define AZEROTHCORE_CREATUREERAHP_H

#include "Define.h"
#include <unordered_map>

class Creature;

/*
 * CUSTOM: era-oriented creature health scaling.
 *
 * Multiplies a creature's health by a rate looked up from four axes:
 *   era      - Vanilla / TBC / WotLK, from the expansion of the map the creature is on
 *   content  - open world / 5-man dungeon / raid, from the map type
 *   rank     - creature_template.rank
 *   band     - one of 14 level bands, from the level the creature rolled
 *
 * Applied at spawn on top of the upstream Rate.Creature.<rank>.HP rate.
 * Battleground/arena creatures and anything a player owns are never scaled.
 * See .agents/docs/systems/creature-era-hp.md.
 */
enum CreatureEraHpEra : uint8
{
    CREATURE_ERA_VANILLA = 0,
    CREATURE_ERA_TBC     = 1,
    CREATURE_ERA_WOTLK   = 2,
    CREATURE_ERA_MAX     = 3
};

enum CreatureEraHpContent : uint8
{
    CREATURE_ERA_CONTENT_WORLD   = 0,
    CREATURE_ERA_CONTENT_DUNGEON = 1,
    CREATURE_ERA_CONTENT_RAID    = 2,
    CREATURE_ERA_CONTENT_MAX     = 3
};

// Indexed by CreatureEliteType: NORMAL, ELITE, RAREELITE, WORLDBOSS, RARE.
constexpr uint8 CREATURE_ERA_HP_RANKS = 5;

// 1-9, 10-19, 20-29, 30-39, 40-49, 50-59, 60, 61-63, 64-69, 70, 71-73, 74-79, 80, 81+
constexpr uint8 CREATURE_ERA_HP_BANDS = 14;

class AC_GAME_API CreatureEraHp
{
public:
    static CreatureEraHp* instance();

    // Called from World::LoadConfigSettings(), so `.reload config` re-reads the table.
    // Creatures already in the world keep their health until they respawn.
    void LoadConfig();

    // Health multiplier for `creature` at `level`. 1.0 when the creature is not scaled.
    [[nodiscard]] float GetRate(Creature const* creature, uint8 level) const;

private:
    CreatureEraHp();

    void LoadRates();
    void LoadEntryOverrides();

    [[nodiscard]] static uint8 GetBand(uint8 level);

    // [era][content][rank][band]
    float _rate[CREATURE_ERA_MAX][CREATURE_ERA_CONTENT_MAX][CREATURE_ERA_HP_RANKS][CREATURE_ERA_HP_BANDS];

    // creature entry -> era
    std::unordered_map<uint32, uint8> _entryOverrides;
};

#define sCreatureEraHp CreatureEraHp::instance()

#endif // AZEROTHCORE_CREATUREERAHP_H
