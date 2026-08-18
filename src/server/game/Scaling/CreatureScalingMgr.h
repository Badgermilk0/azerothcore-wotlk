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

#ifndef AZEROTHCORE_CREATURESCALINGMGR_H
#define AZEROTHCORE_CREATURESCALINGMGR_H

#include "Define.h"
#include "SharedDefines.h"
#include <string>
#include <unordered_map>

struct CreatureTemplate;

/*
 * CUSTOM: content-tier creature scaling.
 *
 * Difficulty is tuned per *content tier* and per *creature level*, rather than
 * by level alone. Level alone cannot separate expansions because they overlap:
 * a level 70 creature is either a TBC raid boss or a Northrend levelling mob,
 * and a level 60-63 creature is either a Vanilla raid boss or early Outland.
 *
 * The tier comes from CreatureTemplate::expansion (the creature_template.exp
 * column). That field already selects which CreatureBaseStats::BaseHealth[] /
 * BaseDamage[] entry a creature uses, so it already means "which expansion's
 * power curve is this creature on" and the world DB keeps it accurate.
 */
enum ContentTier : uint8
{
    CONTENT_TIER_VANILLA = 0,
    CONTENT_TIER_TBC     = 1,
    CONTENT_TIER_WOTLK   = 2,
    CONTENT_TIER_MAX     = 3
};

enum CreatureScalingStat : uint8
{
    CREATURE_SCALING_HP = 0,
    CREATURE_SCALING_DAMAGE,
    CREATURE_SCALING_SPELL_DAMAGE,
    CREATURE_SCALING_STAT_MAX
};

// Highest individually configurable level. Slot CREATURE_SCALING_OVERFLOW_SLOT
// is the "84Plus" failsafe and catches anything above it.
constexpr uint8 CREATURE_SCALING_MAX_LEVEL = 83;
constexpr uint8 CREATURE_SCALING_OVERFLOW_SLOT = CREATURE_SCALING_MAX_LEVEL + 1;
constexpr uint8 CREATURE_SCALING_LEVEL_SLOTS = CREATURE_SCALING_OVERFLOW_SLOT + 1;

// Indexed by CreatureEliteType: NORMAL, ELITE, RAREELITE, WORLDBOSS, RARE.
constexpr uint8 CREATURE_SCALING_RANK_SLOTS = 5;

class AC_GAME_API CreatureScalingMgr
{
public:
    static CreatureScalingMgr* instance();

    // Called from World::LoadConfigSettings(), so `.reload config` is supported.
    void LoadConfig();

    [[nodiscard]] ContentTier ResolveTier(CreatureTemplate const* cInfo, uint32 zoneId) const;

    [[nodiscard]] float GetRate(ContentTier tier, uint8 level, CreatureScalingStat stat) const
    {
        if (tier >= CONTENT_TIER_MAX || stat >= CREATURE_SCALING_STAT_MAX)
            return 1.0f;

        return _rate[tier][level > CREATURE_SCALING_OVERFLOW_SLOT ? CREATURE_SCALING_OVERFLOW_SLOT : level][stat];
    }

    // Per-era rank multiplier. Multiplies with GetRate() and with the global
    // Rate.Creature.<rank>.* family, which both stay in effect.
    [[nodiscard]] float GetRankRate(ContentTier tier, uint32 rank, CreatureScalingStat stat) const
    {
        if (tier >= CONTENT_TIER_MAX || stat >= CREATURE_SCALING_STAT_MAX)
            return 1.0f;

        // CREATURE_UNKNOWN, and any other out-of-range rank, uses the Elite slot. This matches the
        // `default:` branch of Creature::_GetHealthMod() / _GetDamageMod() / GetSpellDamageMod().
        if (rank >= CREATURE_SCALING_RANK_SLOTS)
            rank = CREATURE_ELITE_ELITE;

        return _rankRate[tier][rank][stat];
    }

    // Zone resolution needs a terrain lookup, so callers skip it when unused.
    [[nodiscard]] bool HasZoneOverrides() const { return !_zoneOverrides.empty(); }

private:
    CreatureScalingMgr();

    void LoadRates();
    void LoadRankRates();
    void LoadOverrides(std::string const& configKey, std::unordered_map<uint32, uint8>& target,
        char const* label);

    // [tier][level][stat]. Slot 0 covers level-0 creatures and stays at 1.0f.
    float _rate[CONTENT_TIER_MAX][CREATURE_SCALING_LEVEL_SLOTS][CREATURE_SCALING_STAT_MAX];

    // [tier][rank][stat].
    float _rankRate[CONTENT_TIER_MAX][CREATURE_SCALING_RANK_SLOTS][CREATURE_SCALING_STAT_MAX];

    std::unordered_map<uint32, uint8> _zoneOverrides;
    std::unordered_map<uint32, uint8> _entryOverrides;
};

#define sCreatureScalingMgr CreatureScalingMgr::instance()

#endif // AZEROTHCORE_CREATURESCALINGMGR_H
