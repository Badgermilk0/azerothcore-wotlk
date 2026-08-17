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

#include "CreatureScalingMgr.h"
#include "Config.h"
#include "CreatureData.h"
#include "Log.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Tokenize.h"

namespace
{
    constexpr char const* TierNames[CONTENT_TIER_MAX] = { "Vanilla", "TBC", "WotLK" };
    constexpr char const* StatNames[CREATURE_SCALING_STAT_MAX] = { "HP", "Damage", "SpellDamage" };

    // The level bands the pre-tier configuration used. They remain supported as
    // the default for every tier, so an untouched worldserver.conf keeps its
    // existing tuning after the upgrade.
    struct LegacyBand
    {
        uint8 Low;
        uint8 High;
        char const* Name;
    };

    constexpr LegacyBand LegacyBands[] =
    {
        {  1,  4, "1-4"   }, {  5,  9, "5-9"   }, { 10, 14, "10-14" }, { 15, 19, "15-19" },
        { 20, 24, "20-24" }, { 25, 29, "25-29" }, { 30, 34, "30-34" }, { 35, 39, "35-39" },
        { 40, 44, "40-44" }, { 45, 49, "45-49" }, { 50, 54, "50-54" }, { 55, 59, "55-59" },
        { 60, 60, "60"    }, { 61, 64, "61-64" }, { 65, 69, "65-69" }, { 70, 70, "70"    },
        { 71, 74, "71-74" }, { 75, 79, "75-79" }, { 80, 90, "80-90" }
    };

    // Slots above CREATURE_SCALING_MAX_LEVEL inherit the topmost band.
    char const* LegacyBandName(uint8 slot)
    {
        for (LegacyBand const& band : LegacyBands)
            if (slot >= band.Low && slot <= band.High)
                return band.Name;

        return "80-90";
    }
}

CreatureScalingMgr::CreatureScalingMgr()
{
    for (uint8 tier = 0; tier < CONTENT_TIER_MAX; ++tier)
        for (uint8 slot = 0; slot < CREATURE_SCALING_LEVEL_SLOTS; ++slot)
            for (uint8 stat = 0; stat < CREATURE_SCALING_STAT_MAX; ++stat)
                _rate[tier][slot][stat] = 1.0f;
}

CreatureScalingMgr* CreatureScalingMgr::instance()
{
    static CreatureScalingMgr instance;
    return &instance;
}

void CreatureScalingMgr::LoadConfig()
{
    LoadRates();
    LoadOverrides("Rate.Creature.Tier.ZoneOverrides", _zoneOverrides, "zone");
    LoadOverrides("Rate.Creature.Tier.EntryOverrides", _entryOverrides, "creature entry");
}

void CreatureScalingMgr::LoadRates()
{
    // Slot 0 is the level-0 creature case and is always unscaled.
    for (uint8 tier = 0; tier < CONTENT_TIER_MAX; ++tier)
        for (uint8 stat = 0; stat < CREATURE_SCALING_STAT_MAX; ++stat)
            _rate[tier][0][stat] = 1.0f;

    for (uint8 slot = 1; slot < CREATURE_SCALING_LEVEL_SLOTS; ++slot)
    {
        std::string levelToken = slot > CREATURE_SCALING_MAX_LEVEL ? "84Plus" : std::to_string(slot);

        for (uint8 stat = 0; stat < CREATURE_SCALING_STAT_MAX; ++stat)
        {
            // showLogs is off throughout: most of these ~750 keys are expected to
            // be absent, and the defaults below are the documented behaviour.
            float legacy = sConfigMgr->GetOption<float>(
                Acore::StringFormat("Rate.Creature.Level.{}.{}", LegacyBandName(slot), StatNames[stat]), 1.0f, false);

            for (uint8 tier = 0; tier < CONTENT_TIER_MAX; ++tier)
            {
                std::string const key = Acore::StringFormat("Rate.Creature.Tier.{}.Level.{}.{}",
                    TierNames[tier], levelToken, StatNames[stat]);

                _rate[tier][slot][stat] = sConfigMgr->GetOption<float>(key, legacy, false);
            }
        }
    }
}

void CreatureScalingMgr::LoadOverrides(std::string const& configKey, std::unordered_map<uint32, uint8>& target,
    char const* label)
{
    target.clear();

    std::string const value = sConfigMgr->GetOption<std::string>(configKey, "", false);
    if (value.empty())
        return;

    for (std::string_view token : Acore::Tokenize(value, ',', false))
    {
        std::vector<std::string_view> const pair = Acore::Tokenize(token, ':', false);
        if (pair.size() != 2)
        {
            LOG_ERROR("server.loading", "{}: malformed {} override '{}', expected 'id:tier'.",
                configKey, label, std::string(token));
            continue;
        }

        Optional<uint32> const id = Acore::StringTo<uint32>(pair[0]);
        Optional<uint8> const tier = Acore::StringTo<uint8>(pair[1]);

        if (!id || !tier || *tier >= CONTENT_TIER_MAX)
        {
            LOG_ERROR("server.loading", "{}: invalid {} override '{}', tier must be 0 (Vanilla), 1 (TBC) or 2 (WotLK).",
                configKey, label, std::string(token));
            continue;
        }

        target[*id] = *tier;
    }

    if (!target.empty())
        LOG_INFO("server.loading", "Creature scaling: loaded {} {} tier override(s).", target.size(), label);
}

ContentTier CreatureScalingMgr::ResolveTier(CreatureTemplate const* cInfo, uint32 zoneId) const
{
    // Unknown content stays unscaled rather than being buffed by accident.
    if (!cInfo)
        return CONTENT_TIER_WOTLK;

    if (!_entryOverrides.empty())
    {
        auto const itr = _entryOverrides.find(cInfo->Entry);
        if (itr != _entryOverrides.end())
            return ContentTier(itr->second);
    }

    if (zoneId && !_zoneOverrides.empty())
    {
        auto const itr = _zoneOverrides.find(zoneId);
        if (itr != _zoneOverrides.end())
            return ContentTier(itr->second);
    }

    return cInfo->expansion < CONTENT_TIER_MAX ? ContentTier(cInfo->expansion) : CONTENT_TIER_WOTLK;
}
